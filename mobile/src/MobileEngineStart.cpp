// Connect at start to the radio used last (roadmap 189, Tyler Akerman
// K0OTC: "auto connect to the last radio used ... an option to go to the
// current screen if the last available radio isn't available"). Off unless
// chosen in Setup › App. What "used last" keeps is the way in and its key:
// a radio found on this network by its serial, a saved radio by the address
// or name as entered, a SmartLink radio by its serial and account. At a
// start of the app, and only then, that radio is waited for a few seconds
// and connected to as its card would; when it does not show up the radio
// list stays, as without the choice. Never the demo radio, never a second
// time after a Disconnect, and a radio that takes one station with another
// on it still asks first (connectSmartLinkRadio's question).

#include "MobileEngine.h"

#include "core/AppSettings.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(lcMobile)

namespace AetherSDR::mobile {

namespace {
const QString kConnectAtStartKey = QStringLiteral("MobileConnectAtStart");
const QString kLastRadioKey = QStringLiteral("MobileLastRadio");
// A found radio announces itself once a second; SmartLink's list comes
// after the sign-in kept from the last time (a few seconds on a phone).
constexpr int kStartConnectWaitMs = 8000;

QString wayInWords(const QString& kind)
{
    return kind == QLatin1String("found") ? QStringLiteral("found on this network")
         : kind == QLatin1String("smartlink") ? QStringLiteral("over SmartLink")
                                              : QStringLiteral("by its address");
}
} // namespace

bool MobileEngine::connectAtStart() const
{
    return AppSettings::instance().value(kConnectAtStartKey, QStringLiteral("False")).toString() == QLatin1String("True");
}

void MobileEngine::setConnectAtStart(bool on)
{
    if (on == connectAtStart()) {
        return;
    }
    AppSettings::instance().setValue(kConnectAtStartKey, on ? QStringLiteral("True") : QStringLiteral("False"));
    AppSettings::instance().save();
    qCInfo(lcMobile) << "connect at start:" << (on ? "on" : "off");
    if (!on) {
        endStartConnect(QStringLiteral("the choice was turned off"));
    }
    emit connectAtStartChanged();
}

// The way of the connect being made; it becomes the radio used last once
// the connection is up (rememberLastRadio). An empty kind forgets it (the
// demo radio).
void MobileEngine::noteConnectWay(const QString& kind, const QString& key, const QString& account, const QString& name)
{
    m_connectWay = kind.isEmpty() ? QJsonObject()
                                  : QJsonObject{{QStringLiteral("kind"), kind},
                                                {QStringLiteral("key"), key},
                                                {QStringLiteral("account"), account},
                                                {QStringLiteral("name"), name}};
}

void MobileEngine::rememberLastRadio()
{
    if (m_connectWay.isEmpty()) {
        return;
    }
    const QString stored = QString::fromUtf8(QJsonDocument(m_connectWay).toJson(QJsonDocument::Compact));
    if (AppSettings::instance().value(kLastRadioKey).toString() != stored) {
        AppSettings::instance().setValue(kLastRadioKey, stored);
        AppSettings::instance().save();
    }
}

void MobileEngine::beginStartConnect()
{
    if (!connectAtStart() || m_connectionState != QLatin1String("idle")) {
        return;
    }
    const QJsonObject last = QJsonDocument::fromJson(AppSettings::instance().value(kLastRadioKey).toString().toUtf8()).object();
    const QString kind = last.value(QStringLiteral("kind")).toString();
    if (last.value(QStringLiteral("key")).toString().isEmpty()) {
        qCInfo(lcMobile) << "connect at start: no radio has been used yet";
        return;
    }
    m_startConnect = last;
    m_startConnectName = last.value(QStringLiteral("name")).toString();
    if (m_startConnectName.isEmpty()) {
        m_startConnectName = tr("the radio used last");
    }
    // The way, not the radio: no serial number or nickname in the log.
    qCInfo(lcMobile).noquote() << "connect at start: the radio used last," << wayInWords(kind);
    emit startConnectChanged();
    m_startConnectTimer.start(kStartConnectWaitMs);
    if (kind == QLatin1String("smartlink")) {
        // Its account made the active one, as a favourite's is.
        const QString account = last.value(QStringLiteral("account")).toString();
        if (!account.isEmpty() && account != m_smartLinkActiveId && smartLinkAccountIndex(account) >= 0) {
            useSmartLinkAccount(account);
        }
    }
    tryStartConnect();
}

// Called when the lists change: the awaited radio connects as its card would.
void MobileEngine::tryStartConnect()
{
    if (m_startConnect.isEmpty()) {
        return;
    }
    if (m_connectionState != QLatin1String("idle")) {
        endStartConnect({});
        return;
    }
    const QString kind = m_startConnect.value(QStringLiteral("kind")).toString();
    const QString key = m_startConnect.value(QStringLiteral("key")).toString();
    if (kind == QLatin1String("address")) {
        endStartConnect({});
        connectToAddress(key);
    } else if (kind == QLatin1String("found")) {
        const int row = m_radios.indexOf(QStringLiteral("flex"), key);
        if (row >= 0) {
            endStartConnect({});
            connectToRadio(row);
        }
    } else if (kind == QLatin1String("smartlink")) {
        for (qsizetype i = 0; i < m_wanRadios.size(); ++i) {
            if (m_wanRadios.at(i).serial == key) {
                endStartConnect({});
                connectSmartLinkRadio(int(i));
                return;
            }
        }
    }
}

void MobileEngine::endStartConnect(const QString& why)
{
    if (m_startConnect.isEmpty()) {
        return;
    }
    m_startConnect = QJsonObject();
    m_startConnectTimer.stop();
    if (!why.isEmpty()) {
        qCInfo(lcMobile).noquote() << "connect at start: given up," << why;
    }
    m_startConnectName.clear();
    emit startConnectChanged();
}

void MobileEngine::cancelStartConnect()
{
    endStartConnect(QStringLiteral("the operator stayed on the radio list"));
}

} // namespace AetherSDR::mobile
