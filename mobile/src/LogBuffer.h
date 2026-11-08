#pragma once

#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

class QJSEngine;
class QQmlEngine;

namespace AetherSDR::mobile {

// The most recent log lines, shown on the in-app Log page so testers can
// copy what happened (PTT releases, zoom requests, commands sent to the
// radio) without a debugger. Radio status chatter is left out. The same
// lines go to a file as they come, and a crash signal adds its name and the
// call stack there, so the next start can show what the run that crashed
// did last (a crash took the in-memory log with it: Juho Kontio, build 152).
class LogBuffer : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QStringList lines READ lines NOTIFY linesChanged)
    // The previous run's log file (its last part), empty when there is none.
    Q_PROPERTY(QString previousRun READ previousRun CONSTANT)
    // The previous run ended in a crash signal.
    Q_PROPERTY(bool previousRunCrashed READ previousRunCrashed CONSTANT)
    // Where Send to the developer mails the log (the manual's address).
    Q_PROPERTY(QString developerAddress READ developerAddress CONSTANT)

public:
    ~LogBuffer() override;
    static LogBuffer* instance();
    static LogBuffer* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    // Installs the message handler (the previous handler still receives
    // every message), moves the last run's file aside and opens this run's,
    // and installs the crash signal handlers.
    static void install();

    QStringList lines() const { return m_lines; }
    Q_INVOKABLE QString text() const { return m_lines.join(QLatin1Char('\n')); }
    // The log with a header (app version, OS) for a report.
    Q_INVOKABLE QString report() const;
    // iOS share sheet (Mail, Messages, …); copies where there is none.
    Q_INVOKABLE bool share() const;
    Q_INVOKABLE void clear();
    QString previousRun() const;
    bool previousRunCrashed() const;
    QString developerAddress() const;
    // A mail to the developer with this run's log and the previous run's in
    // one attached file, the subject naming the app, build and device.
    // Returns 1 when a mail app took it, 2 for the share sheet (no mail
    // account: the address is on the clipboard), 0 when nothing was shown.
    Q_INVOKABLE int sendToDeveloper() const;
    // GUI thread only; the message handler queues calls here.
    void append(const QString& line);

signals:
    void linesChanged();
    void lineAdded(const QString& line);

private:
    // Private so QML must go through create() and share instance().
    explicit LogBuffer(QObject* parent);

    QStringList m_lines;
};

} // namespace AetherSDR::mobile
