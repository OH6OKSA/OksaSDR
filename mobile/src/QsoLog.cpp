#include "QsoLog.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>

#include <algorithm>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

namespace AetherSDR::mobile {

namespace {
// Fields in the order they are written; anything else follows alphabetically.
const QStringList kFieldOrder = {
    QStringLiteral("CALL"), QStringLiteral("QSO_DATE"), QStringLiteral("TIME_ON"), QStringLiteral("TIME_OFF"),
    QStringLiteral("FREQ"), QStringLiteral("BAND"), QStringLiteral("MODE"), QStringLiteral("SUBMODE"),
    QStringLiteral("RST_SENT"), QStringLiteral("RST_RCVD"), QStringLiteral("TX_PWR"), QStringLiteral("NAME"),
    QStringLiteral("QTH"), QStringLiteral("GRIDSQUARE"), QStringLiteral("COUNTRY"), QStringLiteral("CONT"),
    QStringLiteral("CQZ"), QStringLiteral("SIG"), QStringLiteral("SIG_INFO"), QStringLiteral("MY_SIG"),
    QStringLiteral("MY_SIG_INFO"), QStringLiteral("STATION_CALLSIGN"), QStringLiteral("MY_GRIDSQUARE"),
    QStringLiteral("COMMENT")};
} // namespace

QString QsoLog::defaultPath()
{
    const QString name = QStringLiteral("oksasdr-log.adi");
    const QString legacy = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(name);
#if defined(Q_OS_IOS)
    const QString path = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath(name);
#elif defined(Q_OS_ANDROID)
    const QString path = legacy;
#else
    const QString path = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
                             .filePath(QStringLiteral("OksaSDR/") + name);
#endif
    // A log from before the move: copied once, the old file left as it was.
    if (path != legacy && !QFile::exists(path) && QFile::exists(legacy)) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile::copy(legacy, path);
    }
    return path;
}

QString QsoLog::defaultMirrorPath()
{
#ifdef Q_OS_ANDROID
    const QJniObject dirName = QJniObject::getStaticObjectField("android/os/Environment", "DIRECTORY_DOCUMENTS",
                                                                "Ljava/lang/String;");
    const QJniObject dir = QJniObject::callStaticObjectMethod("android/os/Environment",
                                                              "getExternalStoragePublicDirectory",
                                                              "(Ljava/lang/String;)Ljava/io/File;", dirName.object<jstring>());
    if (!dir.isValid()) {
        return {};
    }
    const QString docs = dir.callObjectMethod("getAbsolutePath", "()Ljava/lang/String;").toString();
    return docs.isEmpty() ? QString() : QDir(docs).filePath(QStringLiteral("OksaSDR/oksasdr-log.adi"));
#else
    return {};
#endif
}

void QsoLog::setMirrorPath(const QString& path)
{
    m_mirrorPath = path;
    writeMirror();
}

namespace {

// One writer at a time: an import writes from a worker while a contact can
// be logged.
QMutex& fileMutex()
{
    static QMutex mutex;
    return mutex;
}

bool copyMirror(const QString& path, const QString& mirror)
{
    if (mirror.isEmpty() || !QFile::exists(path)) {
        return false;
    }
    QDir().mkpath(QFileInfo(mirror).absolutePath());
    QFile in(path);
    QFile out(mirror);
    if (!in.open(QIODevice::ReadOnly) || !out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return out.write(in.readAll()) >= 0 && out.error() == QFileDevice::NoError;
}

} // namespace

void QsoLog::writeMirror()
{
    QMutexLocker lock(&fileMutex());
    m_mirrorOk = copyMirror(m_path, m_mirrorPath);
}

QsoLog::QsoLog(QString path, bool load)
    : m_path(std::move(path))
{
    if (m_path.isEmpty()) {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        m_path = QDir(dir).filePath(QStringLiteral("oksasdr-log.adi"));
    }
    if (load) {
        adopt(read(m_path));
    }
}

QsoLog::Loaded QsoLog::read(const QString& path)
{
    Loaded loaded;
    QByteArray data;
    {
        QMutexLocker lock(&fileMutex());
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return loaded;
        }
        data = file.readAll();
    }
    // Entries as they come, the newest full records kept.
    parseAdifEach(data, [&loaded](QVariantMap&& r) {
        loaded.entries.append(entryOf(r));
        loaded.recent.append(std::move(r));
        if (loaded.recent.size() > 2 * kRecent) {
            loaded.recent.remove(0, loaded.recent.size() - kRecent);
        }
    });
    if (loaded.recent.size() > kRecent) {
        loaded.recent.remove(0, loaded.recent.size() - kRecent);
    }
    return loaded;
}

void QsoLog::adopt(Loaded loaded)
{
    // Contacts appended while the file was being read come after it.
    loaded.entries.append(m_entries);
    loaded.recent.append(m_recent);
    m_entries = std::move(loaded.entries);
    m_recent = std::move(loaded.recent);
    if (m_recent.size() > kRecent) {
        m_recent.remove(0, m_recent.size() - kRecent);
    }
    m_loaded = true;
}

bool QsoLog::append(const QVariantMap& record)
{
    return appendAll({record});
}

// Several contacts in one write and one mirror copy: an import of 20 000
// appended and mirrored each one (on Android a copy of the whole growing
// file per contact), which held the app for minutes.
bool QsoLog::appendAll(const QList<QVariantMap>& records)
{
    if (records.isEmpty()) {
        return true;
    }
    QString adif;
    QList<QsoEntry> entries;
    for (const QVariantMap& r : records) {
        adif += adifRecord(r) + QLatin1Char('\n');
        entries.append(entryOf(r));
    }
    bool mirrorOk = false;
    if (!writeText(m_path, m_mirrorPath, adif, &mirrorOk)) {
        return false;
    }
    addWritten(entries, records, mirrorOk);
    return true;
}

bool QsoLog::writeText(const QString& path, const QString& mirror, const QString& adif, bool* mirrorOk)
{
    if (adif.isEmpty()) {
        return true;
    }
    QMutexLocker lock(&fileMutex());
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    const bool fresh = !file.exists() || file.size() == 0;
    if (!file.open(QIODevice::Append | QIODevice::Text)) {
        return false;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    if (fresh) {
        out << "OksaSDR log\n<ADIF_VER:5>3.1.4 <PROGRAMID:7>OksaSDR <EOH>\n";
    }
    out << adif;
    out.flush();
    if (file.error() != QFileDevice::NoError) {
        return false;
    }
    file.close();
    const bool copied = copyMirror(path, mirror);
    if (mirrorOk) {
        *mirrorOk = copied;
    }
    return true;
}

void QsoLog::addWritten(const QList<QsoEntry>& entries, const QList<QVariantMap>& newest, bool mirrorOk)
{
    addEntries(entries, newest);
    m_mirrorOk = mirrorOk;
}

void QsoLog::addEntries(const QList<QsoEntry>& entries, const QList<QVariantMap>& newest)
{
    m_entries.append(entries);
    m_recent.append(newest.size() > kRecent ? newest.mid(newest.size() - kRecent) : newest);
    if (m_recent.size() > kRecent) {
        m_recent.remove(0, m_recent.size() - kRecent);
    }
}

int QsoLog::workedCount(const QString& call) const
{
    const QString c = call.trimmed().toUpper();
    int n = 0;
    for (const QsoEntry& e : m_entries) {
        if (e.call == c) {
            ++n;
        }
    }
    return n;
}

bool QsoLog::isDupe(const QString& call, const QString& band, const QString& mode) const
{
    const QString c = call.trimmed().toUpper();
    const QString b = band.toLower();
    const QString group = modeGroup(mode);
    for (const QsoEntry& e : m_entries) {
        if (e.call == c && e.band == b && modeGroup(e.mode) == group) {
            return true;
        }
    }
    return false;
}

QsoEntry QsoLog::entryOf(const QVariantMap& r)
{
    // Band and mode names shared between entries (a few dozen in a log).
    static thread_local QSet<QString> pool;
    const auto shared = [](QString s) {
        const auto it = pool.constFind(s);
        if (it != pool.cend()) {
            return *it;
        }
        pool.insert(s);
        return s;
    };
    QsoEntry e;
    e.call = r.value(QStringLiteral("CALL")).toString().trimmed().toUpper();
    e.band = shared(r.value(QStringLiteral("BAND")).toString().trimmed().toLower());
    e.mode = shared(r.value(QStringLiteral("MODE")).toString().trimmed().toUpper());
    e.submode = shared(r.value(QStringLiteral("SUBMODE")).toString().trimmed().toUpper());
    e.date = r.value(QStringLiteral("QSO_DATE")).toString().trimmed().left(8).toUInt();
    e.time = r.value(QStringLiteral("TIME_ON")).toString().trimmed().left(4).toUShort();
    e.cqz = quint8(std::clamp(r.value(QStringLiteral("CQZ")).toInt(), 0, 255));
    return e;
}

QString QsoLog::dedupeKey(const QsoEntry& e)
{
    return e.call + QLatin1Char('|') + QString::number(e.date) + QLatin1Char('|')
           + QStringLiteral("%1").arg(e.time, 4, 10, QLatin1Char('0')) + QLatin1Char('|') + e.band
           + QLatin1Char('|') + e.mode;
}

QString QsoLog::adifRecord(const QVariantMap& record)
{
    QStringList keys;
    for (const QString& k : kFieldOrder) {
        if (record.contains(k)) {
            keys << k;
        }
    }
    QStringList rest;
    for (auto it = record.cbegin(); it != record.cend(); ++it) {
        if (!kFieldOrder.contains(it.key())) {
            rest << it.key();
        }
    }
    rest.sort();
    keys << rest;
    QString out;
    for (const QString& k : std::as_const(keys)) {
        const QString v = record.value(k).toString().trimmed();
        if (v.isEmpty()) {
            continue;
        }
        // The length counts the UTF-8 bytes, as ADIF readers expect.
        out += QStringLiteral("<%1:%2>%3 ").arg(k.toUpper()).arg(v.toUtf8().size()).arg(v);
    }
    return out + QStringLiteral("<EOR>");
}

QList<QVariantMap> QsoLog::parseAdif(const QString& text)
{
    QList<QVariantMap> records;
    parseAdifEach(text.toUtf8(), [&records](QVariantMap&& r) { records.append(std::move(r)); });
    return records;
}

void QsoLog::parseAdifEach(const QByteArray& bytes, const std::function<void(QVariantMap&&)>& each)
{
    qsizetype i = 0;
    const qsizetype eoh = bytes.indexOf("<EOH>") >= 0 ? bytes.indexOf("<EOH>") : bytes.indexOf("<eoh>");
    if (eoh >= 0) {
        i = eoh + 5;
    }
    QVariantMap current;
    while (i < bytes.size()) {
        const qsizetype lt = bytes.indexOf('<', i);
        if (lt < 0) {
            break;
        }
        const qsizetype gt = bytes.indexOf('>', lt);
        if (gt < 0) {
            break;
        }
        const QByteArray tag = bytes.mid(lt + 1, gt - lt - 1);
        i = gt + 1;
        if (tag.compare("EOR", Qt::CaseInsensitive) == 0) {
            if (!current.isEmpty()) {
                each(std::move(current));
            }
            current = QVariantMap();
            continue;
        }
        const QList<QByteArray> parts = tag.split(':');
        if (parts.size() < 2) {
            continue;
        }
        bool ok = false;
        const int len = parts.at(1).toInt(&ok);
        if (!ok || len < 0 || i + len > bytes.size()) {
            continue;
        }
        current.insert(QString::fromLatin1(parts.at(0)).toUpper(), QString::fromUtf8(bytes.mid(i, len)));
        i += len;
    }
}

QString QsoLog::bandOf(double mhz)
{
    struct Band { double lo, hi; const char* name; };
    static const Band kBands[] = {
        {0.1357, 0.1378, "2190m"}, {0.472, 0.479, "630m"}, {1.8, 2.0, "160m"}, {3.5, 4.0, "80m"},
        {5.06, 5.45, "60m"}, {7.0, 7.3, "40m"}, {10.1, 10.15, "30m"}, {14.0, 14.35, "20m"},
        {18.068, 18.168, "17m"}, {21.0, 21.45, "15m"}, {24.89, 24.99, "12m"}, {28.0, 29.7, "10m"},
        {40.0, 45.0, "8m"}, {50.0, 54.0, "6m"}, {70.0, 71.0, "4m"}, {144.0, 148.0, "2m"},
        {222.0, 225.0, "1.25m"}, {420.0, 450.0, "70cm"}, {902.0, 928.0, "33cm"}, {1240.0, 1300.0, "23cm"},
        {2300.0, 2450.0, "13cm"}, {10000.0, 10500.0, "3cm"}};
    for (const Band& b : kBands) {
        if (mhz >= b.lo && mhz <= b.hi) {
            return QString::fromLatin1(b.name);
        }
    }
    return {};
}

QPair<QString, QString> QsoLog::adifMode(const QString& radioMode)
{
    const QString m = radioMode.toUpper();
    if (m == QLatin1String("USB") || m == QLatin1String("LSB")) {
        return {QStringLiteral("SSB"), m};
    }
    if (m == QLatin1String("CW") || m == QLatin1String("AM") || m == QLatin1String("FM")) {
        return {m, QString()};
    }
    if (m == QLatin1String("NFM") || m == QLatin1String("DFM")) {
        return {QStringLiteral("FM"), QString()};
    }
    if (m == QLatin1String("SAM")) {
        return {QStringLiteral("AM"), QString()};
    }
    if (m == QLatin1String("RTTY")) {
        return {QStringLiteral("RTTY"), QString()};
    }
    if (m == QLatin1String("DIGU") || m == QLatin1String("DIGL")) {
        return {QStringLiteral("DATA"), QString()};
    }
    return {m, QString()};
}

QString QsoLog::modeGroup(const QString& adifMode)
{
    const QString m = adifMode.toUpper();
    if (m == QLatin1String("CW")) {
        return QStringLiteral("CW");
    }
    if (m == QLatin1String("SSB") || m == QLatin1String("AM") || m == QLatin1String("FM")
        || m == QLatin1String("USB") || m == QLatin1String("LSB") || m == QLatin1String("NFM")
        || m == QLatin1String("SAM") || m == QLatin1String("DFM")) {
        return QStringLiteral("PHONE");
    }
    return QStringLiteral("DATA");
}

} // namespace AetherSDR::mobile
