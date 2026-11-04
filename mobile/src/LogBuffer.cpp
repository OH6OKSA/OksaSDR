#include "LogBuffer.h"

#include "ShareText.h"

#include <QSysInfo>

#include <atomic>
#include <mutex>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJSEngine>
#include <QMetaObject>
#include <QStandardPaths>

#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#if defined(__ANDROID__)
#include <dlfcn.h>
#include <unwind.h>
#else
#include <execinfo.h>
#endif

namespace AetherSDR::mobile {

namespace {

constexpr int kMaxLines = 1000;
QtMessageHandler s_previousHandler = nullptr;
// False once the buffer is destroyed (application teardown): later messages,
// from any thread, must not reach the deleted object.
std::atomic<bool> s_bufferAlive{false};

// This run's log file: every kept line as it comes, from any thread (under
// s_fileMutex), and the crash signal's report (no lock: the handler only
// writes). Restarted when it grows past kMaxFileBytes.
constexpr qint64 kMaxFileBytes = 2 * 1024 * 1024;
constexpr qint64 kPreviousRunBytes = 256 * 1024;
int s_fileFd = -1;
qint64 s_fileBytes = 0;
std::mutex s_fileMutex;
QString s_previousRun;
bool s_previousRunCrashed = false;

void writeAll(int fd, const char* data, size_t size)
{
    while (size > 0) {
        const ssize_t n = ::write(fd, data, size);
        if (n <= 0) {
            return;
        }
        data += n;
        size -= size_t(n);
    }
}

void writeText(int fd, const char* text)
{
    writeAll(fd, text, std::strlen(text));
}

void fileLine(const QString& line)
{
    const QByteArray bytes = line.toUtf8() + '\n';
    std::lock_guard<std::mutex> lock(s_fileMutex);
    if (s_fileFd < 0) {
        return;
    }
    if (s_fileBytes + bytes.size() > kMaxFileBytes) {
        if (::ftruncate(s_fileFd, 0) == 0) {
            ::lseek(s_fileFd, 0, SEEK_SET);
            s_fileBytes = 0;
            static const char restarted[] = "(the log file restarted at 2 MB)\n";
            writeAll(s_fileFd, restarted, sizeof(restarted) - 1);
            s_fileBytes += qint64(sizeof(restarted) - 1);
        }
    }
    writeAll(s_fileFd, bytes.constData(), size_t(bytes.size()));
    s_fileBytes += bytes.size();
}

const char* signalName(int sig)
{
    switch (sig) {
    case SIGSEGV: return "SIGSEGV (bad memory access)";
    case SIGBUS: return "SIGBUS (bad memory access)";
    case SIGILL: return "SIGILL (illegal instruction)";
    case SIGFPE: return "SIGFPE (arithmetic error)";
    case SIGABRT: return "SIGABRT (abort)";
    case SIGTRAP: return "SIGTRAP (trap)";
    default: return "signal";
    }
}

#if defined(__ANDROID__)
struct UnwindState {
    void** frames;
    int count;
    int max;
};

_Unwind_Reason_Code unwindFrame(struct _Unwind_Context* context, void* arg)
{
    auto* state = static_cast<UnwindState*>(arg);
    const uintptr_t pc = _Unwind_GetIP(context);
    if (pc && state->count < state->max) {
        state->frames[state->count++] = reinterpret_cast<void*>(pc);
    }
    return state->count < state->max ? _URC_NO_REASON : _URC_END_OF_STACK;
}
#endif

// Writes the signal and the call stack to the log file, then lets the
// system's own handling go on (the iOS crash report, Android's tombstone).
void crashHandler(int sig, siginfo_t* info, void*)
{
    const int fd = s_fileFd;
    if (fd >= 0) {
        char line[160];
        std::snprintf(line, sizeof(line), "\nCRASH: %s at address %p\n", signalName(sig),
                      info ? info->si_addr : nullptr);
        writeText(fd, line);
        void* frames[64];
#if defined(__ANDROID__)
        UnwindState state{frames, 0, 64};
        _Unwind_Backtrace(unwindFrame, &state);
        for (int i = 0; i < state.count; ++i) {
            Dl_info dl{};
            if (dladdr(frames[i], &dl) && dl.dli_fname) {
                std::snprintf(line, sizeof(line), "#%d %s+0x%lx\n", i, dl.dli_fname,
                              static_cast<unsigned long>(reinterpret_cast<uintptr_t>(frames[i])
                                                         - reinterpret_cast<uintptr_t>(dl.dli_fbase)));
            } else {
                std::snprintf(line, sizeof(line), "#%d %p\n", i, frames[i]);
            }
            writeText(fd, line);
        }
#else
        const int count = backtrace(frames, 64);
        backtrace_symbols_fd(frames, count, fd);
#endif
        ::fsync(fd);
    }
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

void installCrashHandlers()
{
    // Its own stack, so a stack overflow can still be reported.
    static char altStack[64 * 1024];
    stack_t ss{};
    ss.ss_sp = altStack;
    ss.ss_size = sizeof(altStack);
    sigaltstack(&ss, nullptr);
    struct sigaction action{};
    action.sa_sigaction = crashHandler;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    sigemptyset(&action.sa_mask);
    for (int sig : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP}) {
        sigaction(sig, &action, nullptr);
    }
}

// Moves the last run's file aside (read back for the Log page) and opens
// this run's.
void openRunFile()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/run.log");
    QFile last(path);
    if (last.open(QIODevice::ReadOnly)) {
        if (last.size() > kPreviousRunBytes) {
            last.seek(last.size() - kPreviousRunBytes);
        }
        s_previousRun = QString::fromUtf8(last.readAll()).trimmed();
        last.close();
        // The handler's own line, at a line start (the next start's notice
        // quotes it mid-line).
        s_previousRunCrashed = s_previousRun.contains(QLatin1String("\nCRASH: "));
    }
    s_fileFd = ::open(QFile::encodeName(path).constData(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    s_fileBytes = 0;
    if (s_fileFd >= 0) {
        // Before main() sets the application version: the macro, as main() uses.
        const QByteArray head = QStringLiteral("OksaSDR %1 (build %2) — %3, started %4\n")
            .arg(QStringLiteral(AETHERSDR_VERSION), QStringLiteral(AETHER_MOBILE_BUILD_NUMBER),
                 QSysInfo::prettyProductName(),
                 QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))
            .toUtf8();
        writeAll(s_fileFd, head.constData(), size_t(head.size()));
        s_fileBytes = head.size();
    }
}

bool worthKeeping(const QMessageLogContext& context, QtMsgType type, const QString& message)
{
    // Qt's iOS accessibility bridge repeats this ~30 times a second when the
    // system queries the UI; it says nothing about the app and flooded the
    // log (and the main thread with it).
    if (message.contains(QLatin1String("a11y element")) || message.contains(QLatin1String("invalid accessible interface"))) {
        return false;
    }
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
        return true;
    }
    const QLatin1StringView category(context.category ? context.category : "");
    // The app's own categories, including aether.mobile.spectrum3d and
    // aether.mobile.audiosession.
    if (category == QLatin1StringView("aether.mobile") || category.startsWith(QLatin1StringView("aether.mobile."))) {
        return true;
    }
    // Qt's Android window: the surface coming and going (enabled in main.cpp
    // on Android), to see where the GUI thread stops in the background.
    if (category == QLatin1StringView("qt.qpa.window")) {
        // Repeated some 50 times a second for a window without a surface
        // (beta 42 and 43: 100 000 lines an evening); the rest is rare.
        return !message.startsWith(QLatin1String("Skipping create egl"));
    }
    // Commands sent to the radio, not its status stream.
    if (category == QLatin1StringView("aether.connection")) {
        return message.startsWith(QLatin1String("TX:"));
    }
    return category == QLatin1StringView("aether.protocol")
        && (message.contains(QLatin1String("xmit")) || message.contains(QLatin1String("PTT"))
            || message.contains(QLatin1String("display pan")) || message.contains(QLatin1String("refus")));
}

void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    if (s_previousHandler) {
        s_previousHandler(type, context, message);
    }
    if (!s_bufferAlive.load() || !worthKeeping(context, type, message)) {
        return;
    }
    const QString line = QStringLiteral("%1 %2 %3")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")),
             QString::fromLatin1(context.category ? context.category : "default"),
             message.left(300));
    fileLine(line);
    // Messages arrive from any thread; the buffer lives on the GUI thread.
    LogBuffer* buffer = LogBuffer::instance();
    QMetaObject::invokeMethod(buffer, [buffer, line] { buffer->append(line); }, Qt::QueuedConnection);
}

} // namespace

LogBuffer::~LogBuffer()
{
    s_bufferAlive = false;
    qInstallMessageHandler(s_previousHandler);
}

LogBuffer::LogBuffer(QObject* parent)
    : QObject(parent)
{
}

LogBuffer* LogBuffer::instance()
{
    static LogBuffer* buffer = new LogBuffer(qApp);
    return buffer;
}

LogBuffer* LogBuffer::create(QQmlEngine*, QJSEngine*)
{
    LogBuffer* buffer = instance();
    QJSEngine::setObjectOwnership(buffer, QJSEngine::CppOwnership);
    return buffer;
}

void LogBuffer::install()
{
    static_cast<void>(instance());
    openRunFile();
    installCrashHandlers();
    s_bufferAlive = true;
    s_previousHandler = qInstallMessageHandler(messageHandler);
    if (s_previousRunCrashed) {
        const qsizetype at = s_previousRun.lastIndexOf(QLatin1String("\nCRASH: ")) + 1;
        qWarning().noquote() << "The previous run ended in a crash:"
                             << s_previousRun.mid(at, s_previousRun.indexOf(QLatin1Char('\n'), at) - at)
                             << "(Log › Previous run has its log)";
    }
}

void LogBuffer::append(const QString& line)
{
    m_lines.append(line);
    emit lineAdded(line);
    if (m_lines.size() > kMaxLines) {
        m_lines.remove(0, m_lines.size() - kMaxLines);
    }
    emit linesChanged();
}

QString LogBuffer::report() const
{
    return QStringLiteral("OksaSDR %1 (build %2) — %3\n\n%4")
        .arg(QGuiApplication::applicationVersion(), QStringLiteral(AETHER_MOBILE_BUILD_NUMBER),
             QSysInfo::prettyProductName(), text());
}

void LogBuffer::copyToClipboard() const
{
    copyText(report());
}

bool LogBuffer::share() const
{
    return shareText(QStringLiteral("OksaSDR log"), report());
}

QString LogBuffer::previousRun() const
{
    return s_previousRun;
}

bool LogBuffer::previousRunCrashed() const
{
    return s_previousRunCrashed;
}

void LogBuffer::copyPreviousRun() const
{
    copyText(s_previousRun);
}

bool LogBuffer::sharePreviousRun() const
{
    return shareText(QStringLiteral("OksaSDR log, previous run"), s_previousRun);
}

void LogBuffer::clear()
{
    m_lines.clear();
    emit linesChanged();
}

} // namespace AetherSDR::mobile
