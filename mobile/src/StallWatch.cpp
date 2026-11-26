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

// Linux and Android: where the GUI and render threads are when the GUI
// thread stalls, from their stacks. Linux, the desktop build (roadmap 91 f:
// the ThinkPad's 1-5 s stalls after FT8 decodes, not seen offscreen).
// Android (roadmap 203: a OnePlus Open's outer screen turned on its side,
// beta 120: the GUI thread stopped at the turn's Expose event for good and
// the system ended the app; the log said that it stood, not where, and the
// render tool did not stand at all). There also the Android UI thread, the
// third party to a surface change, and the Java side of them all.
#if defined(Q_OS_LINUX)
#define OKSASDR_STALL_STACKS 1
#include <QFileInfo>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cxxabi.h>
#include <dlfcn.h>
#include <pthread.h>
#include <unwind.h>
#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <sys/syscall.h>
#include <unistd.h>
#endif
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
// The stacks of the stall going on (the watching thread's own), for the note
// the next start reads: the log lines wait for the GUI thread, which may
// never answer.
QString s_stallStacks;

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
#ifdef Q_OS_ANDROID
constexpr int kMaxStackReports = 6;
// Not before the main thread has stood 8 s: a slow start, connect or page is
// never walked (a tester's handset stands 4 s at every connect). It was 16 s
// while the walk, which runs in a signal handler on that thread, had not
// been seen to work on a device; it has now (the user's OnePlus Open,
// 2026-11-26: the test line stall 17000 gave 56 lines and the app went on),
// and the same day two real stops of 15.5 and 15.8 s ended just short of it
// (roadmap 221), so their place was not seen.
constexpr qint64 kStacksAfterMs = 8000;
#else
constexpr int kMaxStackReports = 12;
constexpr qint64 kStacksAfterMs = 0;
#endif
constexpr qint64 kStacksAgainMs = 16000;
struct StackWalk {
    void** frames;
    int max;
    int count;
};

_Unwind_Reason_Code onStackFrame(struct _Unwind_Context* context, void* arg)
{
    auto* walk = static_cast<StackWalk*>(arg);
    if (walk->count >= walk->max) {
        return _URC_END_OF_STACK;
    }
    if (const auto pc = _Unwind_GetIP(context)) {
        walk->frames[walk->count++] = reinterpret_cast<void*>(pc);
    }
    return _URC_NO_REASON;
}

// The calling thread's return addresses, innermost first. The unwinder of
// the C++ runtime: bionic has backtrace() only from Android 13. Always
// inside its caller, so the handler is one frame on every build (the two
// frames left out of the log are the handler and the signal frame).
[[gnu::always_inline]] inline int walkStack(void** frames, int max)
{
    StackWalk walk{frames, max, 0};
    _Unwind_Backtrace(onStackFrame, &walk);
    return walk.count;
}

void onStackSignal(int)
{
    const int saved = errno;
    s_stackDepth.store(walkStack(s_stack, 64));
    errno = saved;
}

// "libQt6Quick.so.6: QQuickWindow::event(QEvent*)+0x1c (+0x25d5fa)": the
// nearest exported name, and the place in the module for addr2line on the
// same build (a function that is not exported shows under its neighbour's
// name, a long way in).
QString frameText(void* address)
{
    Dl_info info{};
    if (!dladdr(address, &info) || !info.dli_fname) {
        return QStringLiteral("0x") + QString::number(quintptr(address), 16);
    }
    const QString module = QFileInfo(QString::fromLocal8Bit(info.dli_fname)).fileName();
    const QString inModule = QStringLiteral("(+0x") + QString::number(quintptr(address) - quintptr(info.dli_fbase), 16)
        + QLatin1Char(')');
    if (!info.dli_sname) {
        return module + QLatin1Char(' ') + inModule;
    }
    int status = 0;
    char* name = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status);
    const QString out = module + QStringLiteral(": ")
        + QString::fromLatin1(status == 0 && name ? name : info.dli_sname).left(180)
        + QStringLiteral("+0x") + QString::number(quintptr(address) - quintptr(info.dli_saddr), 16)
        + QLatin1Char(' ') + inModule;
    std::free(name);
    return out;
}

// One thread's stack into the log and the note. False when it did not come
// (the thread did not take the signal in 200 ms): no other thread is asked
// then, as a late answer would be read as the next thread's.
bool logStack(const char* which, bool sent)
{
    if (!sent) {
        return true;
    }
    for (int i = 0; i < 100 && s_stackDepth.load() < 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const int depth = s_stackDepth.load();
    if (depth <= 2) {
        const QString line = QStringLiteral("stall stack %1: none came").arg(QLatin1String(which));
        qCWarning(lcStall).noquote() << line;
        s_stallStacks += QLatin1Char('\n') + line;
        return depth >= 0;
    }
    // The first two are the handler and the signal frame.
    for (int i = 2; i < depth && i < 40; ++i) {
        const QString line = QStringLiteral("stall stack %1 #%2 %3").arg(QLatin1String(which)).arg(i - 2).arg(frameText(s_stack[i]));
        qCWarning(lcStall).noquote() << line;
        s_stallStacks += QLatin1Char('\n') + line;
    }
    return true;
}

bool logThreadStack(pthread_t thread, const char* which)
{
    s_stackDepth = -1;
    return logStack(which, pthread_kill(thread, s_stackSignal) == 0);
}

#ifdef Q_OS_ANDROID
// The Java side of the Android UI thread and Qt's threads: a thread that
// waits in a call up to Android shows it there, not in its native frames.
void logJavaStacks()
{
    const QString stacks = QJniObject::callStaticObjectMethod("fi/oh6oksa/oksasdr/Platform", "threadStacks",
                                                              "()Ljava/lang/String;").toString();
    int lines = 0;
    for (const QString& text : stacks.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        if (++lines > 60) {
            break;
        }
        const QString line = QStringLiteral("stall stack java ") + text;
        qCWarning(lcStall).noquote() << line;
        s_stallStacks += QLatin1Char('\n') + line;
    }
}
#endif

void logStacks(bool render)
{
    if (s_stacksLogged.fetch_add(1) >= kMaxStackReports || !s_guiThreadKnown.load()) {
        return;
    }
    bool came = true;
    if (!render) {
        came = logThreadStack(s_guiThread, "GUI");
    }
    const pthread_t renderThread = s_renderThread.load();
    if (came && renderThread != 0 && !pthread_equal(renderThread, s_guiThread)) {
        came = logThreadStack(renderThread, "render");
    }
#ifdef Q_OS_ANDROID
    if (came) {
        // The process's first thread is Android's UI thread (its id is the
        // process's): the surface comes and goes on it.
        s_stackDepth = -1;
        logStack("Android UI", syscall(SYS_tgkill, getpid(), getpid(), s_stackSignal) == 0);
    }
    logJavaStacks();
#endif
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
    int stacksTaken = 0;
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
                stacksTaken = 0;
                s_stallStacks.clear();
            }
            // At 1, 2, 4, 8 ... s: the log line waits for the GUI thread, the
            // file does not.
            if (silentMs >= nextReportMs) {
                const QString line = QStringLiteral("GUI thread stalled %1 ms%2: %3")
                    .arg(silentMs)
                    .arg(active ? QString() : QStringLiteral(" (app not in front)"))
                    .arg(context(now));
                qCWarning(lcStall).noquote() << line;
#ifdef OKSASDR_STALL_STACKS
                // Where, under the line that says how long (Linux: at the
                // first second; Android: at the report of 8 s), and once
                // more at the first report from 16 s on after that (at
                // 16 s): the same place twice is a thread that waits or
                // goes round in one spot, another place one that still
                // works.
                if (active && ((stacksTaken == 0 && silentMs >= kStacksAfterMs)
                               || (stacksTaken == 1 && silentMs >= kStacksAgainMs))) {
                    ++stacksTaken;
                    logStacks(false);
                }
#endif
                writeNote(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss ")) + line + s_stallStacks);
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
#if defined(OKSASDR_STALL_STACKS) && !defined(Q_OS_ANDROID)
            // (Not on Android: a walk on a render thread that is only slow
            // is not worth its risk there; the GUI thread's stall has it.)
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
        // The stall's line, then the stacks taken during it, each a line of
        // its own (a log line holds 300 characters).
        const QStringList lines = QString::fromUtf8(note.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString& line : lines) {
            qCWarning(lcStall).noquote() << "previous run:" << line;
        }
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
#ifndef Q_OS_ANDROID
        // The unwinder sets itself up on its first walk: not in the handler.
        // (Not on Android: nothing of the walk runs there before a stall of
        // 16 s.)
        void* warm[4];
        walkStack(warm, 4);
#endif
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
        // A run that ends in order leaves no note: one written for a stall
        // still counted at the quit (the app closed within a second of a
        // slow moment, before the thread's answer was read) told the next
        // start of a stall that had long been over.
        QFile::remove(notePath());
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
