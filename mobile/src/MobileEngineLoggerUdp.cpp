// A logger on the network: every QSO the app logs goes to a logging program
// as WSJT-X sends its own (roadmap 89, Christoph Steinert DM5HF: RUMlogNG on
// his Mac took nothing from the Logger32 output, raw ADIF over TCP, which it
// does not listen for). RUMlogNG (Preferences > UDP > WSJT-X, "Save QSOs to
// logbook"), MacLoggerDX, Log4OM, N1MM+, GridTracker and the like all take
// QSOs from WSJT-X's UDP messages, so one output serves them all.
//
// The messages as WSJT-X's Network/NetworkMessage.hpp defines them: the
// magic number 0xadbccbda, schema 3 (QDataStream Qt_5_4), the type, the id,
// then the fields. Each QSO goes as WSJT-X sends one: a Heartbeat (0), QSO
// Logged (5) and Logged ADIF (12), to one address (unicast: a multicast
// address would need Apple's multicast entitlement). UDP brings no answer,
// so a QSO is sent once and the status says only that it went out.

#include "MobileEngine.h"

#include "QsoLog.h"

#include "core/AppSettings.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QDateTime>
#include <QHostAddress>
#include <QHostInfo>
#include <QLoggingCategory>
#include <QTimeZone>
#include <QUdpSocket>

#include <cmath>
#include <functional>

Q_DECLARE_LOGGING_CATEGORY(lcMobile)

namespace AetherSDR::mobile {

namespace {
const QString kUdpHostKey = QStringLiteral("MobileLoggerUdpHost");
constexpr quint16 kUdpDefaultPort = 2237;  // WSJT-X's UDP Server port
constexpr quint32 kMagic = 0xadbccbda;
constexpr quint32 kSchema = 3;
constexpr quint32 kHeartbeat = 0;
constexpr quint32 kQsoLogged = 5;
constexpr quint32 kLoggedAdif = 12;
const QString kClientId = QStringLiteral("OksaSDR");

bool splitUdpHostPort(const QString& spec, QString* host, quint16* port)
{
    *host = spec;
    *port = kUdpDefaultPort;
    const qsizetype colon = spec.lastIndexOf(QLatin1Char(':'));
    if (colon > 0 && spec.indexOf(QLatin1Char(':')) == colon) {
        bool ok = false;
        const uint p = spec.mid(colon + 1).toUInt(&ok);
        if (!ok || p == 0 || p > 65535) {
            return false;
        }
        *host = spec.left(colon);
        *port = quint16(p);
    }
    return !host->isEmpty();
}

// WSJT-X's utf8 type: a QByteArray (length, bytes); an empty string as an
// empty array, as WSJT-X's own fields go.
void putUtf8(QDataStream& out, const QString& text)
{
    const QByteArray bytes = text.toUtf8();
    out << quint32(bytes.size());
    out.writeRawData(bytes.constData(), int(bytes.size()));
}

// QDateTime as Qt_5_4 writes it: Julian day, ms since midnight, 1 = UTC.
void putUtc(QDataStream& out, const QDateTime& when)
{
    const QDateTime utc = when.toUTC();
    out << qint64(utc.date().toJulianDay()) << quint32(utc.time().msecsSinceStartOfDay()) << quint8(1);
}

QByteArray message(quint32 type, const std::function<void(QDataStream&)>& fields)
{
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_5_4);
    out.setByteOrder(QDataStream::BigEndian);
    out << kMagic << kSchema << type;
    putUtf8(out, kClientId);
    fields(out);
    return bytes;
}

// "20261116" + "2233" or "223305" as UTC.
QDateTime adifUtc(const QString& date, const QString& time)
{
    const QDate d = QDate::fromString(date, QStringLiteral("yyyyMMdd"));
    QString t = time;
    while (t.size() < 6) {
        t += QLatin1Char('0');
    }
    const QTime tm = QTime::fromString(t.left(6), QStringLiteral("HHmmss"));
    return d.isValid() && tm.isValid() ? QDateTime(d, tm, QTimeZone::UTC) : QDateTime();
}
}  // namespace

// The three messages for one QSO, as WSJT-X sends them (static: the render
// tool's check reads them back).
QList<QByteArray> MobileEngine::loggerUdpMessages(const QVariantMap& r)
{
    const auto f = [&r](const char* key) { return r.value(QLatin1String(key)).toString().trimmed(); };
    const QString version = QCoreApplication::applicationVersion();
    const QByteArray heartbeat = message(kHeartbeat, [&version](QDataStream& out) {
        out << kSchema;
        putUtf8(out, version);
        putUtf8(out, QString());
    });
    QDateTime on = adifUtc(f("QSO_DATE"), f("TIME_ON"));
    if (!on.isValid()) {
        on = QDateTime::currentDateTimeUtc();
    }
    QDateTime off = adifUtc(f("QSO_DATE_OFF").isEmpty() ? f("QSO_DATE") : f("QSO_DATE_OFF"), f("TIME_OFF"));
    if (!off.isValid()) {
        off = on;
    }
    // FT4, PSK31 and the like are SUBMODEs in ADIF and WSJT-X names them in
    // Mode; SSB's USB / LSB are no mode of their own (SSB goes as SSB).
    const QString mode = f("SUBMODE").isEmpty() || f("MODE") == QLatin1String("SSB") ? f("MODE") : f("SUBMODE");
    const quint64 hz = quint64(std::llround(r.value(QStringLiteral("FREQ")).toDouble() * 1e6));
    const QByteArray logged = message(kQsoLogged, [&](QDataStream& out) {
        putUtc(out, off);
        putUtf8(out, f("CALL"));
        putUtf8(out, f("GRIDSQUARE"));
        out << hz;
        putUtf8(out, mode);
        putUtf8(out, f("RST_SENT"));
        putUtf8(out, f("RST_RCVD"));
        putUtf8(out, f("TX_PWR"));
        putUtf8(out, f("COMMENT"));
        putUtf8(out, f("NAME"));
        putUtc(out, on);
        putUtf8(out, f("OPERATOR"));
        putUtf8(out, f("STATION_CALLSIGN"));
        putUtf8(out, f("MY_GRIDSQUARE"));
        putUtf8(out, f("STX_STRING"));
        putUtf8(out, f("SRX_STRING"));
        putUtf8(out, f("PROP_MODE"));
    });
    const QString adif = QStringLiteral("<adif_ver:5>3.1.4\n<programid:%1>%2\n<EOH>\n")
                             .arg(kClientId.size())
                             .arg(kClientId)
                         + QsoLog::adifRecord(r);
    const QByteArray adifMessage = message(kLoggedAdif, [&adif](QDataStream& out) { putUtf8(out, adif); });
    return {heartbeat, logged, adifMessage};
}

QString MobileEngine::loggerUdpHost() const
{
    return AppSettings::instance().value(kUdpHostKey).toString().trimmed();
}

QVariantMap MobileEngine::loggerUdp() const
{
    return {{QStringLiteral("host"), loggerUdpHost()},
            {QStringLiteral("enabled"), !loggerUdpHost().isEmpty()},
            {QStringLiteral("status"), m_loggerUdpStatus},
            {QStringLiteral("statusKind"), m_loggerUdpStatusKind}};
}

void MobileEngine::setLoggerUdp(const QString& hostPort)
{
    const QString spec = hostPort.trimmed();
    QString host;
    quint16 port = 0;
    if (!spec.isEmpty() && !splitUdpHostPort(spec, &host, &port)) {
        m_loggerUdpStatusKind = QStringLiteral("failed");
        m_loggerUdpStatus = tr("Give the computer's address, e.g. 192.168.1.10 or 192.168.1.10:2237.");
        emit loggerUdpChanged();
        return;
    }
    AppSettings::instance().setValue(kUdpHostKey, spec);
    AppSettings::instance().save();
    if (spec.isEmpty()) {
        m_loggerUdpStatus.clear();
        m_loggerUdpStatusKind.clear();
    } else {
        // Nothing answers over UDP: what is set, and how to see it works.
        m_loggerUdpStatusKind = QStringLiteral("ok");
        m_loggerUdpStatus = tr("Each QSO goes to %1:%2 as WSJT-X sends it. Nothing answers back: "
                               "log one and look in the logging program.")
                                .arg(host)
                                .arg(port);
    }
    qCInfo(lcMobile).noquote() << "logger on the network:" << (spec.isEmpty() ? QStringLiteral("off") : spec);
    emit loggerUdpChanged();
}

void MobileEngine::loggerUdpSend(const QVariantMap& record)
{
    QString host;
    quint16 port = 0;
    if (!splitUdpHostPort(loggerUdpHost(), &host, &port)) {
        return;
    }
    const QList<QByteArray> datagrams = loggerUdpMessages(record);
    const QString call = record.value(QStringLiteral("CALL")).toString();
    const auto send = [this, datagrams, port, call](const QHostAddress& address) {
        if (!m_loggerUdpSocket) {
            m_loggerUdpSocket = new QUdpSocket(this);
        }
        bool ok = true;
        for (const QByteArray& d : datagrams) {
            ok = m_loggerUdpSocket->writeDatagram(d, address, port) == d.size() && ok;
        }
        m_loggerUdpStatusKind = ok ? QStringLiteral("ok") : QStringLiteral("failed");
        m_loggerUdpStatus = ok ? tr("%1 sent to %2:%3 at %4 UTC.")
                                     .arg(call, address.toString())
                                     .arg(port)
                                     .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("HH:mm")))
                               : tr("%1 not sent (%2).").arg(call, m_loggerUdpSocket->errorString());
        qCInfo(lcMobile).noquote() << "logger on the network:" << m_loggerUdpStatus;
        emit loggerUdpChanged();
    };
    QHostAddress address;
    if (address.setAddress(host)) {
        send(address);
        return;
    }
    QHostInfo::lookupHost(host, this, [this, send, host, call](const QHostInfo& info) {
        const QList<QHostAddress> found = info.addresses();
        if (found.isEmpty()) {
            m_loggerUdpStatusKind = QStringLiteral("failed");
            m_loggerUdpStatus = tr("%1 not sent: %2 not found.").arg(call, host);
            qCInfo(lcMobile).noquote() << "logger on the network:" << m_loggerUdpStatus;
            emit loggerUdpChanged();
            return;
        }
        // IPv4 first, as most logging programs listen there.
        QHostAddress pick = found.first();
        for (const QHostAddress& a : found) {
            if (a.protocol() == QAbstractSocket::IPv4Protocol) {
                pick = a;
                break;
            }
        }
        send(pick);
    });
}

}  // namespace AetherSDR::mobile
