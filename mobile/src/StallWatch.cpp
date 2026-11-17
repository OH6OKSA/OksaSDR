#include "StallWatch.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QMetaEnum>
#include <QQuickWindow>
#include <QStandardPaths>

#include <atomic>
#include <chrono>
#include <thread>

// Linux (the desktop build): where the GUI and render threads are when the
// GUI thread stalls, from their stacks (roadmap 91 f: the ThinkPad's 1-5 s
// stalls after FT8 decodes, not seen offscreen).
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
#define OKSASDR_STALL_STACKS 1
#include <QFileInfo>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cxxabi.h>
#include <execinfo.h>
#include <pthread.h>
#endif

Q_LOGGING_CATEGORY(lcStall, "aether.mobile.stall")

namespace AetherSDR::mobile {

namespace {

qint64 nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::atomic<bool> s_running{false};
std::atomic<bool> s_beatPending{false};
std::atomic<qint64> s_mainBeatMs{0};
std::atomic<qint64> s_frameMs{0};
std::atomic<qint64> s_eventMs{0};
std::atomic<int> s_eventType{0};
std::atomic<bool> s_expectFrames{false};
std::atomic<bool> s_transmitting{false};
// The app in front: in the background (and on iOS, suspended) a silent GUI
// thread is no stall.
std::atomic<bool> s_active{true};
// Start of the stall the GUI thread is in, 0 when it answers.
std::atomic<qint64> s_stallSinceMs{0};
std::thread s_thread;

QString notePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/stall.txt");
}

QString eventName(int type)
{
    const char* key = QMetaEnum::fromType<QEvent::Type>().valueToKey(type);
    return key ? QString::fromLatin1(key) : QString::number(type);
}

// What was going on, from the atomics only (any thread).
QString context(qint64 now)
{
    const qint64 eventMs = s_eventMs.load();
    const qint64 frameMs = s_frameMs.load();
    return QStringLiteral("transmitting %1, last window event %2 %3 ms before, last frame %4 ms before")
        .arg(s_transmitting.load() ? QStringLiteral("yes") : QStringLiteral("no"),
             eventMs ? eventName(s_eventType.load()) : QStringLiteral("none"),
             QString::number(eventMs ? now - eventMs : -1),
             QString::number(frameMs ? now - frameMs : -1));
}

#ifdef OKSASDR_STALL_STACKS
pthread_t s_guiThread{};
std::atomic<bool> s_guiThreadKnown{false};
std::atomic<pthread_t> s_renderThread{0};
int s_stackSignal = 0;
void* s_stack[64];
std::atomic<int> s_stackDepth{-1};
// A few stalls a run are enough to see where; more would flood the log.
std::atomic<int> s_stacksLogged{0};
constexpr int kMaxStackReports = 12;

void onStackSignal(int)
{
    const int saved = errno;
    s_stackDepth.store(backtrace(s_stack, 64));
    errno = saved;
}

// "lib.so(_ZN...+0x1c) [0x7f...]" -> "lib.so: Name::function(args)+0x1c".
QString frameText(const char* raw)
{
    const QString s = QString::fromLocal8Bit(raw);
    const int open = s.indexOf(QLatin1Char('('));
    const int plus = s.indexOf(QLatin1Char('+'), open);
    const int close = s.indexOf(QLatin1Char(')'), open);
    const QString module = QFileInfo(open > 0 ? s.left(open) : s.section(QLatin1Char(' '), 0, 0)).fileName();
    if (open >= 0 && plus > open + 1 && close > plus) {
        const QByteArray mangled = s.mid(open + 1, plus - open - 1).toLatin1();
        int status = 0;
        char* name = abi::__cxa_demangle(mangled.constData(), nullptr, nullptr, &status);
        const QString offset = s.mid(plus, close - plus);
        QString out = module + QStringLiteral(": ")
            + (status == 0 && name ? QString::fromLatin1(name) : QString::fromLatin1(mangled)) + offset;
        std::free(name);
        return out;
    }
    // No symbol: the module and its offset (addr2line on the same build).
    return module + QStringLiteral(" ") + (open >= 0 && close > open ? s.mid(open, close - open + 1) : s);
}

void logStack(pthread_t thread, const char* which)
{
    s_stackDepth = -1;
    if (pthread_kill(thread, s_stackSignal) != 0) {
        return;
    }
    for (int i = 0; i < 100 && s_stackDepth.load() < 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const int depth = s_stackDepth.load();
    if (depth <= 0) {
        qCWarning(lcStall).noquote() << "stall stack" << which << ": none came";
        return;
    }
    char** symbols = backtrace_symbols(s_stack, depth);
    if (!symbols) {
        return;
    }
    // The first two are the handler and the signal frame.
    for (int i = 2; i < depth && i < 40; ++i) {
        qCWarning(lcStall).noquote() << QStringLiteral("stall stack %1 #%2 %3").arg(QLatin1String(which)).arg(i - 2).arg(frameText(symbols[i]));
    }
    std::free(symbols);
}

void logStacks(bool render)
{
    if (s_stacksLogged.fetch_add(1) >= kMaxStackReports || !s_guiThreadKnown.load()) {
        return;
    }
    if (!render) {
        logStack(s_guiThread, "GUI");
    }
    const pthread_t renderThread = s_renderThread.load();
    if (renderThread != 0 && !pthread_equal(renderThread, s_guiThread)) {
        logStack(renderThread, "render");
    }
}
#endif

void writeNote(const QString& line)
{
    QFile file(notePath());
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(line.toUtf8());
    }
}

void beatArrived()
{
    const qint64 now = nowMs();
    s_mainBeatMs = now;
    s_beatPending = false;
    if (const qint64 since = s_stallSinceMs.exchange(0)) {
        qCInfo(lcStall).noquote() << QStringLiteral("GUI thread answers again after %1 ms").arg(now - since);
        QFile::remove(notePath());
    }
}

void run()
{
    qint64 nextReportMs = 1000;
    bool frameGapLogged = false;
    while (s_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        const qint64 now = nowMs();
        const bool active = s_active.load();
#ifdef Q_OS_ANDROID
        // Android keeps the GUI thread running in the background (receive,
        // the radio connection): a stall there is watched too, from 4 s (beta
        // 41: it stood still while another app was in front, and the radio
        // dropped the SmartLink connection).
        const qint64 stallMs = active ? 1000 : 4000;
        if (!active) {
            s_frameMs = 0;
        }
#else
        const qint64 stallMs = 1000;
        if (!active) {
            s_mainBeatMs = now;
            s_frameMs = 0;
            continue;
        }
#endif
        if (!s_beatPending.exchange(true)) {
            QMetaObject::invokeMethod(qApp, [] { beatArrived(); }, Qt::QueuedConnection);
        }
        const qint64 silentMs = now - s_mainBeatMs.load();
        if (silentMs >= stallMs) {
            if (!s_stallSinceMs.load()) {
                s_stallSinceMs = s_mainBeatMs.load();
                nextReportMs = stallMs;
#ifdef OKSASDR_STALL_STACKS
                if (active) {
                    logStacks(false);
                }
#endif
            }
            // At 1, 2, 4, 8 ... s: the log line waits for the GUI thread, the
            // file does not.
            if (silentMs >= nextReportMs) {
                const QString line = QStringLiteral("GUI thread stalled %1 ms%2: %3")
                    .arg(silentMs)
                    .arg(active ? QString() : QStringLiteral(" (app not in front)"))
                    .arg(context(now));
                qCWarning(lcStall).noquote() << line;
                writeNote(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss ")) + line);
                nextReportMs *= 2;
            }
            continue;
        }
        const qint64 frameMs = s_frameMs.load();
        const bool frameGap = s_expectFrames.load() && frameMs && now - frameMs >= 2000;
        if (frameGap && !frameGapLogged) {
            qCWarning(lcStall).noquote()
                << QStringLiteral("no frame drawn for %1 ms while the GUI thread answers: %2")
                       .arg(now - frameMs).arg(context(now));
#ifdef OKSASDR_STALL_STACKS
            logStacks(true);
#endif
        }
        frameGapLogged = frameGap;
    }
}

class EventSpy : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject*, QEvent* event) override
    {
        s_eventType = int(event->type());
        s_eventMs = nowMs();
        return false;
    }
};

} // namespace

void StallWatch::start()
{
    QFile note(notePath());
    if (note.open(QIODevice::ReadOnly)) {
        qCWarning(lcStall).noquote() << "previous run:" << QString::fromUtf8(note.readAll());
        note.close();
        note.remove();
    }
    if (s_running.exchange(true)) {
        return;
    }
    s_mainBeatMs = nowMs();
    s_active = qApp->applicationState() == Qt::ApplicationActive;
#ifdef OKSASDR_STALL_STACKS
    s_guiThread = pthread_self();
    s_stackSignal = SIGRTMIN + 5;
    struct sigaction action{};
    action.sa_handler = onStackSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    if (sigaction(s_stackSignal, &action, nullptr) == 0) {
        // backtrace() loads libgcc on its first call: not in the handler.
        void* warm[2];
        backtrace(warm, 2);
        s_guiThreadKnown = true;
    }
#endif
    s_thread = std::thread(run);
    QObject::connect(qApp, &QCoreApplication::aboutToQuit, qApp, [] { stop(); });
    QObject::connect(qApp, &QGuiApplication::applicationStateChanged, qApp, [](Qt::ApplicationState state) {
        s_mainBeatMs = nowMs();
        s_active = state == Qt::ApplicationActive;
    });
}

void StallWatch::stop()
{
    if (s_running.exchange(false) && s_thread.joinable()) {
        s_thread.join();
    }
}

void StallWatch::watch(QQuickWindow* window)
{
    window->installEventFilter(new EventSpy(window));
    QObject::connect(window, &QQuickWindow::frameSwapped, window, [] { s_frameMs = nowMs(); },
                     Qt::DirectConnection);
#ifdef OKSASDR_STALL_STACKS
    // The render thread (the threaded render loop's; the GUI thread with the
    // basic one): its stack beside the GUI thread's.
    QObject::connect(window, &QQuickWindow::beforeRendering, window, [] { s_renderThread = pthread_self(); },
                     Qt::DirectConnection);
#endif
}

void StallWatch::setExpectFrames(bool on)
{
    if (on && !s_expectFrames.load()) {
        // A new period: the gap starts now, not at the last frame before it.
        s_frameMs = nowMs();
    }
    s_expectFrames = on;
}

void StallWatch::setTransmitting(bool on)
{
    s_transmitting = on;
}

} // namespace AetherSDR::mobile
