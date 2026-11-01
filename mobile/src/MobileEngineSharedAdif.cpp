// A shared ADIF file, both ways, for loggers without a cloud API (Log4OM,
// N1MM, DXKeeper, MacLoggerDX...): an ADIF file in iCloud Drive, Google
// Drive, OneDrive, Dropbox or Nextcloud, picked once in the system's file
// picker. Its QSOs are read at start (and on Read now) and count as worked
// before beside the app's own log; every QSO the app logs is added to its
// end, queued while the file cannot be reached. The file stays the
// operator's: the app only appends.
//
// Access: iOS (and the iPad app on a Mac) opens the picked file in place,
// and Qt keeps a security-scoped bookmark of it (QDarwinSecurityScopedFile-
// Engine), so a plain QFile reaches it after a restart. Android hands a
// content:// URI: the app takes a lasting read and write grant itself (Qt's
// picker takes read only), reads through Qt's content file engine, and
// writes through the ContentResolver ("wa" appends; Qt's engine opens every
// write with "w", which would empty the file), rewriting the whole file
// where a provider cannot append.

#include "MobileEngine.h"

#include "QsoLog.h"

#include "core/AppSettings.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QUrl>

#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QJniEnvironment>
#include <QJniObject>
#endif

Q_DECLARE_LOGGING_CATEGORY(lcMobile)

namespace AetherSDR::mobile {

namespace {
constexpr int kSharedMaxPending = 500;

QMutex& sharedFileMutex()
{
    static QMutex mutex;
    return mutex;
}

// The file as QFile opens it: a local path, or the content:// URI as it is.
QString sharedPath(const QString& url)
{
    const QUrl u(url);
    return u.isLocalFile() ? u.toLocalFile() : url;
}

#ifdef Q_OS_ANDROID
QJniObject contentResolver()
{
    const QJniObject context(QNativeInterface::QAndroidApplication::context().object());
    return context.callObjectMethod("getContentResolver", "()Landroid/content/ContentResolver;");
}

QJniObject parseUri(const QString& uri)
{
    return QJniObject::callStaticObjectMethod("android/net/Uri", "parse", "(Ljava/lang/String;)Landroid/net/Uri;",
                                              QJniObject::fromString(uri).object<jstring>());
}

// A lasting grant: read and write, else read only. False when neither.
bool takeGrant(const QString& uri, bool* writable)
{
    QJniEnvironment env;
    const jint read = QJniObject::getStaticField<jint>("android/content/Intent", "FLAG_GRANT_READ_URI_PERMISSION");
    const jint write = QJniObject::getStaticField<jint>("android/content/Intent", "FLAG_GRANT_WRITE_URI_PERMISSION");
    QJniObject resolver = contentResolver();
    QJniObject u = parseUri(uri);
    resolver.callMethod<void>("takePersistableUriPermission", "(Landroid/net/Uri;I)V", u.object(), read | write);
    if (!env.checkAndClearExceptions()) {
        *writable = true;
        return true;
    }
    *writable = false;
    resolver.callMethod<void>("takePersistableUriPermission", "(Landroid/net/Uri;I)V", u.object(), read);
    return !env.checkAndClearExceptions();
}

void releaseGrant(const QString& uri)
{
    QJniEnvironment env;
    const jint flags = QJniObject::getStaticField<jint>("android/content/Intent", "FLAG_GRANT_READ_URI_PERMISSION")
                       | QJniObject::getStaticField<jint>("android/content/Intent", "FLAG_GRANT_WRITE_URI_PERMISSION");
    contentResolver().callMethod<void>("releasePersistableUriPermission", "(Landroid/net/Uri;I)V", parseUri(uri).object(), flags);
    env.checkAndClearExceptions();
}

// Writes bytes through the ContentResolver in mode ("wa", "wt", "w").
bool writeContent(const QString& uri, const QByteArray& data, const char* mode)
{
    QJniEnvironment env;
    QJniObject stream = contentResolver().callObjectMethod(
        "openOutputStream", "(Landroid/net/Uri;Ljava/lang/String;)Ljava/io/OutputStream;", parseUri(uri).object(),
        QJniObject::fromString(QString::fromLatin1(mode)).object<jstring>());
    if (env.checkAndClearExceptions() || !stream.isValid()) {
        return false;
    }
    jbyteArray bytes = env->NewByteArray(jsize(data.size()));
    env->SetByteArrayRegion(bytes, 0, jsize(data.size()), reinterpret_cast<const jbyte*>(data.constData()));
    stream.callMethod<void>("write", "([B)V", bytes);
    bool ok = !env.checkAndClearExceptions();
    stream.callMethod<void>("close");
    ok = !env.checkAndClearExceptions() && ok;
    env->DeleteLocalRef(bytes);
    return ok;
}
#endif

// Adds ADIF records to the end of the file (a header first in an empty
// one). From a worker; one writer at a time.
bool appendToShared(const QString& url, const QString& records, QString* error)
{
    QMutexLocker lock(&sharedFileMutex());
    const QString path = sharedPath(url);
    const bool local = QUrl(url).isLocalFile() || !url.contains(QLatin1String("://"));
    // Whether the file is empty and ends in a newline: a local file from its
    // last byte; a content:// one is read whole (a rewrite may need it).
    QByteArray old;
    bool empty = true;
    bool endsInNewline = true;
    {
        QFile in(path);
        if (in.open(QIODevice::ReadOnly)) {
            if (local) {
                const qint64 size = in.size();
                empty = size == 0;
                endsInNewline = size == 0 || (in.seek(size - 1) && in.read(1) == "\n");
            } else {
                old = in.readAll();
                empty = old.trimmed().isEmpty();
                endsInNewline = old.isEmpty() || old.endsWith('\n');
            }
        }
    }
    QByteArray add = records.toUtf8();
    if (empty) {
        add.prepend("OksaSDR log\n<ADIF_VER:5>3.1.4 <PROGRAMID:7>OksaSDR <EOH>\n");
    } else if (!endsInNewline) {
        add.prepend('\n');
    }
#ifdef Q_OS_ANDROID
    if (!QUrl(url).isLocalFile()) {
        if (writeContent(url, add, "wa")) {
            return true;
        }
        // A provider that cannot append: the whole file again.
        if (!old.isEmpty() && (writeContent(url, old + add, "wt") || writeContent(url, old + add, "w"))) {
            qCInfo(lcMobile) << "shared ADIF: appended by rewriting the file (the provider cannot append)";
            return true;
        }
        *error = QCoreApplication::translate("MobileEngine", "the file could not be written");
        return false;
    }
#endif
    // Never a new file in its place: an iCloud file not yet on the device
    // is a placeholder under another name, and a new one would make a
    // conflicting copy. The QSOs wait for the file instead.
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::ExistingOnly)) {
        *error = out.errorString();
        return false;
    }
    if (out.write(add) != add.size()) {
        *error = out.errorString();
        return false;
    }
    return true;
}

QString displayName(const QString& url)
{
    const QUrl u(url);
    if (u.isLocalFile()) {
        return QFileInfo(u.toLocalFile()).fileName();
    }
    // content://.../document/primary%3ADocuments%2Flog.adi: the last part.
    const QString name = QFileInfo(url).fileName();
    if (!name.isEmpty() && !name.contains(QLatin1Char('%'))) {
        return name;
    }
    QString tail = QUrl::fromPercentEncoding(url.section(QLatin1Char('/'), -1).toUtf8());
    return tail.section(QLatin1Char('/'), -1).section(QLatin1Char(':'), -1);
}
}  // namespace

void MobileEngine::setupSharedAdif()
{
    m_sharedAdifUrl = AppSettings::instance().value(QStringLiteral("MobileSharedAdifUrl")).toString();
    m_sharedWritable = AppSettings::instance().value(QStringLiteral("MobileSharedAdifWritable"), QStringLiteral("True")).toString()
                       == QLatin1String("True");
    const QString pending = AppSettings::instance().value(QStringLiteral("MobileSharedAdifPending")).toString();
    if (!pending.isEmpty()) {
        m_sharedPending = pending.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }
    if (m_sharedAdifUrl.isEmpty()) {
        return;
    }
    // What the file held last time, then the file itself (it may be away:
    // a cloud file not yet on the device).
    QThread* reader = QThread::create([this] {
        QList<QsoEntry> known = KnownLog::read(KnownLog::sharedPath());
        QMetaObject::invokeMethod(this, [this, known = std::move(known)]() mutable {
            if (m_fileKnown.isEmpty()) {
                m_fileKnown = std::move(known);
                m_wkDirty = true;
                emit qsoLogChanged();
            }
            sharedAdifRead();
        }, Qt::QueuedConnection);
    });
    connect(reader, &QThread::finished, reader, &QObject::deleteLater);
    reader->start();
}

QVariantMap MobileEngine::sharedAdif() const
{
    return {{QStringLiteral("set"), !m_sharedAdifUrl.isEmpty()},
            {QStringLiteral("name"), m_sharedAdifUrl.isEmpty() ? QString() : displayName(m_sharedAdifUrl)},
            {QStringLiteral("known"), int(m_fileKnown.size())},
            {QStringLiteral("pending"), m_sharedPending.size()},
            {QStringLiteral("reading"), m_sharedReading},
            {QStringLiteral("status"), m_sharedStatus},
            {QStringLiteral("statusKind"), m_sharedStatusKind}};
}

void MobileEngine::setSharedAdif(const QUrl& url)
{
    const QString u = url.isLocalFile() ? url.toString() : url.toString(QUrl::FullyEncoded);
    if (u.isEmpty()) {
        return;
    }
    if (!m_sharedAdifUrl.isEmpty() && m_sharedAdifUrl != u) {
        clearSharedAdif();
    }
    bool writable = true;
#ifdef Q_OS_ANDROID
    if (!url.isLocalFile() && !takeGrant(u, &writable)) {
        qCWarning(lcMobile) << "shared ADIF: no lasting grant for" << u;
    }
#endif
    m_sharedAdifUrl = u;
    m_sharedWritable = writable;
    AppSettings::instance().setValue(QStringLiteral("MobileSharedAdifUrl"), u);
    AppSettings::instance().setValue(QStringLiteral("MobileSharedAdifWritable"), writable ? QStringLiteral("True") : QStringLiteral("False"));
    AppSettings::instance().save();
    qCInfo(lcMobile).noquote() << "shared ADIF:" << displayName(u) << (writable ? "(read and write)" : "(read only)");
    sharedAdifRead();
}

void MobileEngine::clearSharedAdif()
{
    // The file itself stays as it is; the app forgets it.
#ifdef Q_OS_ANDROID
    if (!m_sharedAdifUrl.isEmpty() && !QUrl(m_sharedAdifUrl).isLocalFile()) {
        releaseGrant(m_sharedAdifUrl);
    }
#endif
    m_sharedAdifUrl.clear();
    m_sharedPending.clear();
    m_fileKnown.clear();
    m_sharedStatus.clear();
    m_sharedStatusKind.clear();
    ++m_sharedGeneration;
    AppSettings::instance().setValue(QStringLiteral("MobileSharedAdifUrl"), QString());
    AppSettings::instance().setValue(QStringLiteral("MobileSharedAdifPending"), QString());
    AppSettings::instance().save();
    KnownLog::remove(KnownLog::sharedPath());
    m_wkDirty = true;
    emit qsoLogChanged();
    emit sharedAdifChanged();
}

void MobileEngine::sharedAdifRead()
{
    if (m_sharedAdifUrl.isEmpty() || m_sharedReading) {
        return;
    }
    m_sharedReading = true;
    m_sharedStatusKind = QStringLiteral("checking");
    m_sharedStatus = tr("Reading the file…");
    emit sharedAdifChanged();
    const QString url = m_sharedAdifUrl;
    const int generation = m_sharedGeneration;
    QThread* worker = QThread::create([this, url, generation] {
        QByteArray data;
        QString error;
        {
            QMutexLocker lock(&sharedFileMutex());
            QFile file(sharedPath(url));
            if (file.open(QIODevice::ReadOnly)) {
                data = file.readAll();
            } else {
                error = file.errorString();
            }
        }
        QList<QsoEntry> entries;
        QsoLog::parseAdifEach(data, [&entries](QVariantMap&& r) {
            if (!r.contains(QStringLiteral("BAND")) && r.contains(QStringLiteral("FREQ"))) {
                r.insert(QStringLiteral("BAND"), QsoLog::bandOf(r.value(QStringLiteral("FREQ")).toDouble()));
            }
            QsoEntry e = QsoLog::entryOf(r);
            if (!e.call.isEmpty()) {
                entries.append(std::move(e));
            }
        });
        const bool readOk = error.isEmpty();
        if (readOk) {
            KnownLog::write(KnownLog::sharedPath(), entries);
        }
        QMetaObject::invokeMethod(this, [this, entries = std::move(entries), error, readOk, generation]() mutable {
            m_sharedReading = false;
            if (generation != m_sharedGeneration) {
                emit sharedAdifChanged();
                return;
            }
            if (!readOk) {
                m_sharedStatusKind = QStringLiteral("failed");
                m_sharedStatus = m_fileKnown.isEmpty()
                                     ? tr("The file could not be read (%1). Open it once in the Files app, or pick it again.").arg(error)
                                     : tr("The file could not be read (%1); its %2 QSOs from the last read still count.")
                                           .arg(error).arg(m_fileKnown.size());
                qCWarning(lcMobile).noquote() << "shared ADIF:" << m_sharedStatus;
                emit sharedAdifChanged();
                return;
            }
            m_fileKnown = std::move(entries);
            m_wkDirty = true;
            m_sharedStatusKind = QStringLiteral("ok");
            m_sharedStatus = m_sharedWritable
                                 ? tr("%1 QSOs in the file count as worked before; each QSO you log is added to it.").arg(m_fileKnown.size())
                                 : tr("%1 QSOs in the file count as worked before; the file is read-only here, so QSOs are not added.")
                                       .arg(m_fileKnown.size());
            qCInfo(lcMobile).noquote() << "shared ADIF:" << m_fileKnown.size() << "QSOs read";
            emit qsoLogChanged();
            emit sharedAdifChanged();
            sharedAdifFlush();
        }, Qt::QueuedConnection);
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

void MobileEngine::devSharedInsert(const QString& call)
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    sharedAdifQueue(QsoLog::adifRecord({{QStringLiteral("CALL"), call.toUpper()},
                                        {QStringLiteral("QSO_DATE"), now.toString(QStringLiteral("yyyyMMdd"))},
                                        {QStringLiteral("TIME_ON"), now.toString(QStringLiteral("HHmm"))},
                                        {QStringLiteral("BAND"), QStringLiteral("17m")},
                                        {QStringLiteral("MODE"), QStringLiteral("CW")}}));
}

void MobileEngine::sharedAdifQueue(const QString& adif)
{
    if (m_sharedAdifUrl.isEmpty() || !m_sharedWritable) {
        return;
    }
    m_sharedPending.append(adif);
    while (m_sharedPending.size() > kSharedMaxPending) {
        m_sharedPending.removeFirst();
    }
    AppSettings::instance().setValue(QStringLiteral("MobileSharedAdifPending"), m_sharedPending.join(QLatin1Char('\n')));
    AppSettings::instance().save();
    sharedAdifFlush();
}

void MobileEngine::sharedAdifFlush()
{
    if (m_sharedWriting || m_sharedPending.isEmpty() || m_sharedAdifUrl.isEmpty() || !m_sharedWritable) {
        return;
    }
    m_sharedWriting = true;
    const QStringList batch = m_sharedPending;
    const QString url = m_sharedAdifUrl;
    QThread* worker = QThread::create([this, batch, url] {
        QString error;
        const bool ok = appendToShared(url, batch.join(QLatin1Char('\n')) + QLatin1Char('\n'), &error);
        QMetaObject::invokeMethod(this, [this, batch, ok, error, url] {
            m_sharedWriting = false;
            if (url != m_sharedAdifUrl) {
                return;
            }
            if (ok) {
                // The file's entries (and their copy for a start without the
                // file) follow without a re-read.
                QList<QsoEntry> added;
                for (const QString& r : batch) {
                    m_sharedPending.removeOne(r);
                    for (const QVariantMap& m : QsoLog::parseAdif(r)) {
                        added.append(QsoLog::entryOf(m));
                    }
                }
                m_fileKnown.append(added);
                KnownLog::append(KnownLog::sharedPath(), added);
                AppSettings::instance().setValue(QStringLiteral("MobileSharedAdifPending"), m_sharedPending.join(QLatin1Char('\n')));
                AppSettings::instance().save();
                m_sharedStatusKind = QStringLiteral("ok");
                m_sharedStatus = tr("QSO added to the file (%1 QSOs in it).").arg(m_fileKnown.size());
                qCInfo(lcMobile) << "shared ADIF:" << batch.size() << "QSO(s) added";
                sharedAdifFlush();
            } else {
                // Kept: tried again with the next QSO, start or Read now.
                m_sharedStatusKind = QStringLiteral("failed");
                m_sharedStatus = tr("The QSO could not be added to the file (%1): %2 wait").arg(error).arg(m_sharedPending.size());
                qCWarning(lcMobile).noquote() << "shared ADIF:" << m_sharedStatus;
            }
            emit sharedAdifChanged();
        }, Qt::QueuedConnection);
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

}  // namespace AetherSDR::mobile
