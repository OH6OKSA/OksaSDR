#include "QsoLog.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>

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

QsoLog::QsoLog(QString path)
    : m_path(std::move(path))
{
    if (m_path.isEmpty()) {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        m_path = QDir(dir).filePath(QStringLiteral("oksasdr-log.adi"));
    }
    load();
}

void QsoLog::load()
{
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    m_records = parseAdif(QString::fromUtf8(file.readAll()));
}

bool QsoLog::append(const QVariantMap& record)
{
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QFile file(m_path);
    const bool fresh = !file.exists() || file.size() == 0;
    if (!file.open(QIODevice::Append | QIODevice::Text)) {
        return false;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    if (fresh) {
        out << "OksaSDR log\n<ADIF_VER:5>3.1.4 <PROGRAMID:7>OksaSDR <EOH>\n";
    }
    out << adifRecord(record) << "\n";
    out.flush();
    if (file.error() != QFileDevice::NoError) {
        return false;
    }
    m_records.append(record);
    return true;
}

int QsoLog::workedCount(const QString& call) const
{
    const QString c = call.trimmed().toUpper();
    int n = 0;
    for (const QVariantMap& r : m_records) {
        if (r.value(QStringLiteral("CALL")).toString().toUpper() == c) {
            ++n;
        }
    }
    return n;
}

bool QsoLog::isDupe(const QString& call, const QString& band, const QString& mode) const
{
    const QString c = call.trimmed().toUpper();
    const QString group = modeGroup(mode);
    for (const QVariantMap& r : m_records) {
        if (r.value(QStringLiteral("CALL")).toString().toUpper() == c
            && r.value(QStringLiteral("BAND")).toString().compare(band, Qt::CaseInsensitive) == 0
            && modeGroup(r.value(QStringLiteral("MODE")).toString()) == group) {
            return true;
        }
    }
    return false;
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
    int pos = 0;
    const int eoh = text.indexOf(QStringLiteral("<EOH>"), 0, Qt::CaseInsensitive);
    if (eoh >= 0) {
        pos = eoh + 5;
    }
    const QByteArray bytes = text.mid(pos).toUtf8();
    QVariantMap current;
    qsizetype i = 0;
    while (i < bytes.size()) {
        const qsizetype lt = bytes.indexOf('<', i);
        if (lt < 0) {
            break;
        }
        const qsizetype gt = bytes.indexOf('>', lt);
        if (gt < 0) {
            break;
        }
        const QString tag = QString::fromUtf8(bytes.mid(lt + 1, gt - lt - 1));
        i = gt + 1;
        if (tag.compare(QStringLiteral("EOR"), Qt::CaseInsensitive) == 0) {
            if (!current.isEmpty()) {
                records.append(current);
            }
            current.clear();
            continue;
        }
        const QStringList parts = tag.split(QLatin1Char(':'));
        if (parts.size() < 2) {
            continue;
        }
        bool ok = false;
        const int len = parts.at(1).toInt(&ok);
        if (!ok || len < 0 || i + len > bytes.size()) {
            continue;
        }
        current.insert(parts.at(0).toUpper(), QString::fromUtf8(bytes.mid(i, len)));
        i += len;
    }
    return records;
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
