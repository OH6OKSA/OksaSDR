#include "StallWatch.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QLoggingCategory>
#include <QMetaEnum>
#include <QQuickWindow>
#include <QStandardPaths>

#include <atomic>
#include <chrono>
#include <thread>

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
        if (!s_beatPending.exchange(true)) {
            QMetaObject::invokeMethod(qApp, [] { beatArrived(); }, Qt::QueuedConnection);
        }
        const qint64 silentMs = now - s_mainBeatMs.load();
        if (silentMs >= 1000) {
            if (!s_stallSinceMs.load()) {
                s_stallSinceMs = s_mainBeatMs.load();
                nextReportMs = 1000;
            }
            // At 1, 2, 4, 8 ... s: the log line waits for the GUI thread, the
            // file does not.
            if (silentMs >= nextReportMs) {
                const QString line = QStringLiteral("GUI thread stalled %1 ms: %2").arg(silentMs).arg(context(now));
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
    s_thread = std::thread(run);
    QObject::connect(qApp, &QCoreApplication::aboutToQuit, qApp, [] { stop(); });
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
