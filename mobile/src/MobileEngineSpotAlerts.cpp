// Spot alerts and the spotter's place (roadmap 132; the user, 2026-11-22:
// "metsästää tiettyä asemaa spottina ... kun vk9xy on äänessä tulisi siitä
// spotti häly tai jos metsästän puuttuvaa dxcc", and spots "OH:sta annettu,
// koska silloin se on mahdollista kuulla").
//
// Every spot is the radio's (spotSources, 131). Spotted from (the View tab)
// keeps the display to spots whose spotter is anywhere, on this station's
// continent or in its country, by the cty lookup of the spotter's call (a
// skimmer's -# and an SSID dropped); a spotter the lookup does not know
// passes. Alerts (Setup › Spots) take each new spot through the same place
// filter: a call on the watch list (whole calls, * for any characters:
// VK9XY, 3Y0*) or a DXCC not yet in the log (on the spot's band, or at all;
// the FTx tab's New DXCC, digiNewDxcc) pops up in the middle of the
// screen (Tune goes there), a sound, and, while the app is away from the
// front, a notification. A call alerts once per band in 30 minutes.

#include "MobileEngine.h"

#include "LocalNotify.h"
#include "QsoLog.h"

#include "core/AppSettings.h"
#include "core/CtyDatParser.h"
#include "models/RadioModel.h"
#include "models/SpotModel.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QSoundEffect>
#include <QTimer>

Q_DECLARE_LOGGING_CATEGORY(lcMobile)

namespace AetherSDR::mobile {

namespace {
const QString kFromKey = QStringLiteral("MobileSpotFrom");
const QString kWatchKey = QStringLiteral("MobileSpotWatch");
const QString kDxccKey = QStringLiteral("MobileSpotAlertDxcc");
const QString kSoundKey = QStringLiteral("MobileSpotAlertSound");
const QString kNotifyKey = QStringLiteral("MobileSpotAlertNotify");
constexpr qint64 kAlertAgainMs = 30LL * 60 * 1000;
constexpr int kQueueMax = 20;

// A callsign as the cty lookup takes it: a skimmer's "-#" and an SSID's
// "-2" dropped, upper case.
QString baseCall(QString call)
{
    call = call.trimmed().toUpper();
    const int dash = call.indexOf(QLatin1Char('-'));
    return dash > 0 ? call.left(dash) : call;
}

bool on(const QString& key, bool fallback)
{
    const QVariant v = AppSettings::instance().value(key);
    if (!v.isValid() || v.isNull()) {
        return fallback;
    }
    return v.toString() == QLatin1String("True");
}
} // namespace

void MobileEngine::setupSpotAlerts()
{
    SpotModel& spots = m_session.radioModel().spotModel();
    connect(&spots, &SpotModel::spotAdded, this, [this](const SpotData& s) { checkSpotAlert(s); });
}

QString MobileEngine::spotFrom() const
{
    const QString v = AppSettings::instance().value(kFromKey, QStringLiteral("anywhere")).toString();
    return v == QLatin1String("continent") || v == QLatin1String("country") ? v : QStringLiteral("anywhere");
}

void MobileEngine::setSpotFrom(const QString& from)
{
    const QString v = from == QLatin1String("continent") || from == QLatin1String("country") ? from : QStringLiteral("anywhere");
    if (v == spotFrom()) {
        return;
    }
    AppSettings::instance().setValue(kFromKey, v);
    AppSettings::instance().save();
    qCInfo(lcMobile).noquote() << "spots: spotted from" << v;
    applyRbnToCluster();   // the cluster's skimmer filter follows (136)
    emit spotsChanged();
    emit spotAlertSettingsChanged();
}

// This station's own place: the radio's callsign (or the cluster login).
QVariantMap MobileEngine::ownPlace() const
{
    const DxccEntity* e = m_cty ? m_cty->entityForCallsign(baseCall(dxCallsign())) : nullptr;
    if (!e) {
        return {};
    }
    return {{QStringLiteral("prefix"), e->primaryPrefix}, {QStringLiteral("country"), e->name},
            {QStringLiteral("continent"), e->continent}};
}

bool MobileEngine::spotFromOk(const QString& spotter) const
{
    const QString from = spotFrom();
    if (from == QLatin1String("anywhere") || !m_cty) {
        return true;
    }
    const DxccEntity* mine = m_cty->entityForCallsign(baseCall(dxCallsign()));
    const DxccEntity* theirs = m_cty->entityForCallsign(baseCall(spotter));
    if (!mine || !theirs) {
        return true;  // a place not known: not kept out
    }
    return from == QLatin1String("country") ? theirs->primaryPrefix == mine->primaryPrefix
                                             : theirs->continent == mine->continent;
}

QVariantMap MobileEngine::spotAlertSettings() const
{
    const QVariantMap own = ownPlace();
    return {{QStringLiteral("watch"), AppSettings::instance().value(kWatchKey).toString()},
            {QStringLiteral("dxcc"), AppSettings::instance().value(kDxccKey, QStringLiteral("off")).toString()},
            {QStringLiteral("sound"), on(kSoundKey, true)},
            {QStringLiteral("notify"), on(kNotifyKey, false)},
            {QStringLiteral("from"), spotFrom()},
            {QStringLiteral("ownPrefix"), own.value(QStringLiteral("prefix"))},
            {QStringLiteral("ownContinent"), own.value(QStringLiteral("continent"))},
            {QStringLiteral("ownCountry"), own.value(QStringLiteral("country"))},
            {QStringLiteral("active"), spotAlertsActive()}};
}

bool MobileEngine::spotAlertsActive() const
{
    return !AppSettings::instance().value(kWatchKey).toString().trimmed().isEmpty()
        || AppSettings::instance().value(kDxccKey, QStringLiteral("off")).toString() != QLatin1String("off");
}

void MobileEngine::setSpotAlertSetting(const QString& key, const QVariant& value)
{
    if (key == QLatin1String("watch")) {
        // Calls and patterns, upper case, one space after each comma.
        const QStringList parts = value.toString().toUpper().split(QRegularExpression(QStringLiteral("[,;\\s]+")),
                                                                   Qt::SkipEmptyParts);
        AppSettings::instance().setValue(kWatchKey, parts.join(QStringLiteral(", ")));
        qCInfo(lcMobile).noquote() << "spot alerts: watch list" << (parts.isEmpty() ? QStringLiteral("(none)") : parts.join(QLatin1Char(' ')));
    } else if (key == QLatin1String("dxcc")) {
        const QString v = value.toString();
        AppSettings::instance().setValue(kDxccKey, v == QLatin1String("band") || v == QLatin1String("any") ? v : QStringLiteral("off"));
        qCInfo(lcMobile).noquote() << "spot alerts: needed DXCC" << AppSettings::instance().value(kDxccKey).toString();
    } else if (key == QLatin1String("sound")) {
        AppSettings::instance().setValue(kSoundKey, value.toBool() ? QStringLiteral("True") : QStringLiteral("False"));
    } else if (key == QLatin1String("notify")) {
        AppSettings::instance().setValue(kNotifyKey, value.toBool() ? QStringLiteral("True") : QStringLiteral("False"));
        if (value.toBool()) {
            requestLocalNotifications();
        }
    } else {
        return;
    }
    AppSettings::instance().save();
    emit spotAlertSettingsChanged();
}

bool MobileEngine::watchMatch(const QString& call) const
{
    const QStringList list = AppSettings::instance().value(kWatchKey).toString().split(QStringLiteral(", "), Qt::SkipEmptyParts);
    for (const QString& entry : list) {
        if (entry.contains(QLatin1Char('*')) || entry.contains(QLatin1Char('?'))) {
            // The whole call: 3Y0* is 3Y0J, not K3Y0X.
            const QRegularExpression re(QRegularExpression::wildcardToRegularExpression(entry),
                                        QRegularExpression::CaseInsensitiveOption);
            if (re.match(call).hasMatch()) {
                return true;
            }
        } else if (entry.compare(call, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

// Why a spot would alert (the watch list, a needed DXCC), empty if not;
// also asked before a busy feed's spot is dropped (roadmap 136).
QString MobileEngine::spotAlertReason(const QString& rawCall, double mhz, const QString& spotter)
{
    if (!m_connected || !spotAlertsActive() || rawCall.isEmpty() || mhz <= 0.0) {
        return {};
    }
    const QString call = rawCall.trimmed().toUpper();
    if (call == baseCall(dxCallsign()) || !spotFromOk(spotter)) {
        return {};
    }
    const QString band = QsoLog::bandOf(mhz);
    if (watchMatch(call)) {
        return tr("on your watch list");
    }
    const QString dxcc = AppSettings::instance().value(kDxccKey, QStringLiteral("off")).toString();
    if (dxcc != QLatin1String("off")) {
        const int level = digiNewDxcc(call, true, band);
        if (level == 2) {
            return tr("a new DXCC");
        }
        if (level == 1 && dxcc == QLatin1String("band")) {
            return tr("a new DXCC on %1").arg(band);
        }
    }
    return {};
}

void MobileEngine::checkSpotAlert(const SpotData& s)
{
    const QString why = spotAlertReason(s.callsign, s.rxFreqMhz, s.spotterCallsign);
    if (why.isEmpty()) {
        return;
    }
    const QString call = s.callsign.trimmed().toUpper();
    const QString band = QsoLog::bandOf(s.rxFreqMhz);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QString key = call + QLatin1Char('|') + band;
    const auto seen = m_spotAlertSeen.constFind(key);
    if (seen != m_spotAlertSeen.constEnd() && now - *seen < kAlertAgainMs) {
        return;
    }
    m_spotAlertSeen.insert(key, now);
    const DxccEntity* e = m_cty ? m_cty->entityForCallsign(call) : nullptr;
    const QVariantMap alert{{QStringLiteral("call"), call},
                            {QStringLiteral("mhz"), s.rxFreqMhz},
                            {QStringLiteral("mode"), s.mode},
                            {QStringLiteral("band"), band},
                            {QStringLiteral("country"), e ? e->name : QString()},
                            {QStringLiteral("why"), why},
                            {QStringLiteral("spotter"), s.spotterCallsign},
                            {QStringLiteral("index"), s.index},
                            {QStringLiteral("time"), QDateTime::currentDateTimeUtc().toString(QStringLiteral("HH:mm"))}};
    if (!m_spotAlert.isEmpty()) {
        m_spotAlertQueue.prepend(m_spotAlert);
        while (m_spotAlertQueue.size() > kQueueMax) {
            m_spotAlertQueue.removeLast();
        }
    }
    m_spotAlert = alert;
    const QString line = QStringLiteral("%1 %2 MHz %3%4: %5").arg(call, QString::number(s.rxFreqMhz, 'f', 3), s.mode,
                                                                 e ? QStringLiteral(" (%1)").arg(e->name) : QString(), why);
    qCInfo(lcMobile).noquote() << "spot alert:" << line << "spotted by" << s.spotterCallsign;
    emit spotAlertChanged();
    if (on(kSoundKey, true)) {
        playSpotAlertSound();
    }
    if (on(kNotifyKey, false) && QGuiApplication::applicationState() != Qt::ApplicationActive) {
        postLocalNotification(tr("Spot: %1").arg(call),
                              tr("%1 MHz %2%3, %4. Spotted by %5.")
                                  .arg(QString::number(s.rxFreqMhz, 'f', 3), s.mode,
                                       e ? QStringLiteral(", ") + e->name : QString(), why, s.spotterCallsign));
    }
}

// The spot alert's sound (roadmap 237). Qt's sound player follows the
// default output for as long as it exists: when the route changed (a
// Bluetooth headset's media audio going off) it opened a new audio stream
// at once, even long after its sound, and on Android that start waited
// 12.5 s in AudioTrack::start on the GUI thread while the headset went: the
// app stood still (the user's phone, beta 123, 3 s after an alert's sound).
// The player was kept for good. Now each alert's sound has a player of its
// own, let go once it has played, so between alerts nothing follows the
// route. (A player on a thread of its own crashed in Qt 6.12's sound engine
// on the desktop, at the second alert: not used.)
void MobileEngine::playSpotAlertSound()
{
    // One sound at a time: alerts close together give one sound.
    if (m_alertSound) {
        return;
    }
    auto* sound = new QSoundEffect(this);
    m_alertSound = sound;
    QElapsedTimer started;
    started.start();
    // Let go: played, failed, or 10 s gone by (a sound that never started).
    const auto release = [this, sound, started](const char* why) {
        if (m_alertSound != sound) {
            return;
        }
        m_alertSound = nullptr;
        qCInfo(lcMobile).noquote() << "spot alert sound:" << why << "after" << started.elapsed() << "ms, player let go";
        sound->deleteLater();
    };
    connect(sound, &QSoundEffect::playingChanged, sound, [sound, release] {
        if (!sound->isPlaying()) {
            release("played");
        }
    });
    connect(sound, &QSoundEffect::statusChanged, sound, [sound, release] {
        if (sound->status() == QSoundEffect::Error) {
            release("failed");
        }
    });
    QTimer::singleShot(10000, sound, [release] { release("not played in 10 s"); });
    sound->setSource(QUrl(QStringLiteral("qrc:/sounds/spot-alert.wav")));
    sound->setVolume(0.8f);
    sound->play();
}

QVariantMap MobileEngine::spotAlert() const
{
    if (m_spotAlert.isEmpty()) {
        return {};
    }
    QVariantMap a = m_spotAlert;
    a.insert(QStringLiteral("more"), int(m_spotAlertQueue.size()));
    return a;
}

void MobileEngine::dismissSpotAlert()
{
    m_spotAlert = m_spotAlertQueue.isEmpty() ? QVariantMap() : m_spotAlertQueue.takeFirst();
    emit spotAlertChanged();
}

void MobileEngine::dismissAllSpotAlerts()
{
    m_spotAlert.clear();
    m_spotAlertQueue.clear();
    emit spotAlertChanged();
}

void MobileEngine::tuneSpotAlert()
{
    if (m_spotAlert.isEmpty()) {
        return;
    }
    const int index = m_spotAlert.value(QStringLiteral("index")).toInt();
    const double mhz = m_spotAlert.value(QStringLiteral("mhz")).toDouble();
    // The spot may have run out meanwhile: then its frequency.
    if (m_session.radioModel().spotModel().spots().contains(index)) {
        // The alert's Tune opens the mode's Controls tab too (roadmap 137).
        m_spotTuneOpensTab = true;
        tuneToSpot(index);
    } else if (mhz > 0.0) {
        tuneTo(mhz);
    }
    qCInfo(lcMobile).noquote() << "spot alert: tuned to" << m_spotAlert.value(QStringLiteral("call")).toString();
    dismissSpotAlert();
}

} // namespace AetherSDR::mobile
