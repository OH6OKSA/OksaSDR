// SmartLink accounts and favourite radios (roadmap 62, Ian Gilchrist VK1HF's
// own build): several FlexRadio accounts signed in on one device, one active
// at a time (one SmartLinkClient, one session to the server), switched with
// a tap; favourite radios of any account connect in one tap, switching the
// account first when it is another's. Each account's sign-in (the refresh
// token) is in the platform's secure store; the list, the e-mails and the
// favourites in AppSettings, on this device only (not in exported setups).

#include "MobileEngine.h"

#include "SecureStore.h"

#include "core/AppSettings.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QUuid>

Q_DECLARE_LOGGING_CATEGORY(lcMobile)

namespace AetherSDR::mobile {

namespace {
const QString kAccountsKey = QStringLiteral("MobileSmartLinkAccounts");
const QString kActiveKey = QStringLiteral("MobileSmartLinkActive");
const QString kFavouritesKey = QStringLiteral("MobileSmartLinkFavourites");
const QString kTokenKey = QStringLiteral("smartlink_refresh_token");
constexpr int kMaxAccounts = 5;
constexpr int kMaxFavourites = 20;
constexpr int kFavouriteWaitMs = 20000;

QJsonArray loadArray(const QString& key)
{
    return QJsonDocument::fromJson(AppSettings::instance().value(key).toString().toUtf8()).array();
}

void storeArray(const QString& key, const QJsonArray& array)
{
    AppSettings::instance().setValue(key, QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
    AppSettings::instance().save();
}

// An account's name on the buttons: the server's callsign and name, else
// its e-mail, else its place on the list.
QString label(const QJsonObject& account, qsizetype index)
{
    QString l = account.value(QStringLiteral("label")).toString();
    if (l.isEmpty()) {
        l = account.value(QStringLiteral("email")).toString();
    }
    return l.isEmpty() ? MobileEngine::tr("Account %1").arg(index + 1) : l;
}

// The e-mail in a sign-in's id token (a JWT: its middle part, Base64url
// JSON), for an account kept from before the list had e-mails.
QString emailOf(const QString& idToken)
{
    const QStringList parts = idToken.split(QLatin1Char('.'));
    if (parts.size() < 2) {
        return {};
    }
    const QByteArray json = QByteArray::fromBase64(parts.at(1).toLatin1(),
                                                   QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    return QJsonDocument::fromJson(json).object().value(QStringLiteral("email")).toString();
}
} // namespace

QString MobileEngine::smartLinkTokenKey(const QString& id)
{
    return id.isEmpty() ? kTokenKey : kTokenKey + QLatin1Char('_') + id;
}

QJsonArray MobileEngine::loadSmartLinkAccounts() const
{
    return loadArray(kAccountsKey);
}

void MobileEngine::storeSmartLinkAccounts(const QJsonArray& accounts)
{
    storeArray(kAccountsKey, accounts);
    emit smartLinkChanged();
}

int MobileEngine::smartLinkAccountIndex(const QString& id) const
{
    const QJsonArray accounts = loadSmartLinkAccounts();
    for (qsizetype i = 0; i < accounts.size(); ++i) {
        if (accounts.at(i).toObject().value(QStringLiteral("id")).toString() == id) {
            return int(i);
        }
    }
    return -1;
}

// Before accounts there was one sign-in under the single key and its e-mail
// (Base64) in SmartLinkEmail: that becomes the first account, its token
// left where it is.
void MobileEngine::migrateSmartLinkAccounts()
{
    m_smartLinkActiveId = AppSettings::instance().value(kActiveKey).toString();
    if (!loadSmartLinkAccounts().isEmpty() || loadSecret(kTokenKey).isEmpty()) {
        return;
    }
    const QString email = QString::fromUtf8(
        QByteArray::fromBase64(AppSettings::instance().value(QStringLiteral("SmartLinkEmail")).toByteArray()));
    storeSmartLinkAccounts(QJsonArray{QJsonObject{{QStringLiteral("id"), QString()},
                                                  {QStringLiteral("email"), email},
                                                  {QStringLiteral("label"), QString()}}});
    m_smartLinkActiveId.clear();
    AppSettings::instance().setValue(kActiveKey, m_smartLinkActiveId);
    qCInfo(lcMobile) << "SmartLink: the kept sign-in became the first account";
}

QVariantList MobileEngine::smartLinkAccounts() const
{
    QVariantList out;
    for (const QJsonValue& v : loadSmartLinkAccounts()) {
        const QJsonObject a = v.toObject();
        const QString id = a.value(QStringLiteral("id")).toString();
        out << QVariantMap{{QStringLiteral("label"), label(a, out.size())},
                           {QStringLiteral("email"), a.value(QStringLiteral("email")).toString()},
                           {QStringLiteral("active"), id == m_smartLinkActiveId},
                           {QStringLiteral("signedIn"), !loadSecret(smartLinkTokenKey(id)).isEmpty()}};
    }
    return out;
}

// After a password sign-in: the account of that e-mail, or a new one (the
// first takes the single key of the builds before), made active.
void MobileEngine::adoptSmartLinkSignIn()
{
    const QString email = m_smartLinkPendingEmail;
    m_smartLinkPendingEmail.clear();
    m_smartLinkFallbackId.clear();
    m_smartLinkHasFallback = false;
    if (email.isEmpty()) {
        return;
    }
    QJsonArray accounts = loadSmartLinkAccounts();
    QString id;
    bool found = false;
    for (const QJsonValue& v : std::as_const(accounts)) {
        const QJsonObject a = v.toObject();
        if (a.value(QStringLiteral("email")).toString().compare(email, Qt::CaseInsensitive) == 0) {
            id = a.value(QStringLiteral("id")).toString();
            found = true;
            break;
        }
    }
    if (!found) {
        const bool firstFree = smartLinkAccountIndex(QString()) < 0;
        id = firstFree ? QString() : QUuid::createUuid().toString(QUuid::Id128).left(12);
        if (accounts.size() >= kMaxAccounts) {
            const QString oldest = accounts.first().toObject().value(QStringLiteral("id")).toString();
            removeSecret(smartLinkTokenKey(oldest));
            accounts.removeFirst();
        }
        accounts.append(QJsonObject{{QStringLiteral("id"), id},
                                    {QStringLiteral("email"), email},
                                    {QStringLiteral("label"), QString()}});
        storeSmartLinkAccounts(accounts);
        qCInfo(lcMobile) << "SmartLink: account added," << accounts.size() << "on this device";
    }
    m_smartLinkActiveId = id;
    AppSettings::instance().setValue(kActiveKey, id);
    AppSettings::instance().save();
}

// The active account's name as the server gives it (callsign and name),
// and its e-mail from the sign-in when the list lacks it, kept for the
// account buttons while another account is active.
void MobileEngine::noteSmartLinkAccountLabel()
{
    QJsonArray accounts = loadSmartLinkAccounts();
    const int i = smartLinkAccountIndex(m_smartLinkActiveId);
    if (i < 0) {
        return;
    }
    QJsonObject a = accounts.at(i).toObject();
    bool changed = false;
    if (const QString now = smartLinkAccount(); !now.isEmpty() && a.value(QStringLiteral("label")).toString() != now) {
        a.insert(QStringLiteral("label"), now);
        changed = true;
    }
    if (a.value(QStringLiteral("email")).toString().isEmpty()) {
        if (const QString email = emailOf(m_smartLink.idToken()); !email.isEmpty()) {
            a.insert(QStringLiteral("email"), email);
            changed = true;
        }
    }
    if (changed) {
        accounts.replace(i, a);
        storeSmartLinkAccounts(accounts);
        emit smartLinkFavouritesChanged();
    }
}

// Signed out: the account, its sign-in and its favourites go.
void MobileEngine::forgetSmartLinkAccount(const QString& id)
{
    removeSecret(smartLinkTokenKey(id));
    QJsonArray accounts = loadSmartLinkAccounts();
    if (const int i = smartLinkAccountIndex(id); i >= 0) {
        accounts.removeAt(i);
        storeSmartLinkAccounts(accounts);
    }
    QJsonArray favourites = loadArray(kFavouritesKey);
    for (qsizetype i = favourites.size() - 1; i >= 0; --i) {
        if (favourites.at(i).toObject().value(QStringLiteral("account")).toString() == id) {
            favourites.removeAt(i);
        }
    }
    storeArray(kFavouritesKey, favourites);
    emit smartLinkFavouritesChanged();
}

// The account made active: the session of the one before closed (its
// sign-in kept), then this one's kept sign-in, or the form with its e-mail.
void MobileEngine::useSmartLinkAccount(const QString& id)
{
    m_wanReconnectTimer.stop();
    m_smartLinkRetryTimer.stop();
    m_smartLinkRetryMs = 0;
    m_smartLinkRetried = false;
    m_smartLink.logout();
    m_wanRadios.clear();
    emit smartLinkRadiosChanged();
    m_smartLinkActiveId = id;
    AppSettings::instance().setValue(kActiveKey, id);
    AppSettings::instance().save();
    const QString token = loadSecret(smartLinkTokenKey(id));
    qCInfo(lcMobile).noquote() << "SmartLink: account" << smartLinkAccountIndex(id) + 1 << "active,"
                               << (token.isEmpty() ? QStringLiteral("no sign-in kept") : QStringLiteral("signing in"));
    if (token.isEmpty()) {
        setSmartLinkState(QStringLiteral("signedOut"));
        return;
    }
    setSmartLinkState(QStringLiteral("signingIn"));
    m_smartLink.loginWithRefreshToken(token);
}

void MobileEngine::switchSmartLinkAccount(int index)
{
    const QJsonArray accounts = loadSmartLinkAccounts();
    if (index < 0 || index >= accounts.size()) {
        return;
    }
    const QString id = accounts.at(index).toObject().value(QStringLiteral("id")).toString();
    if (id == m_smartLinkActiveId && m_smartLinkState != QLatin1String("signedOut")) {
        return;
    }
    if (m_wanActive || remoteSession() || m_connectionState != QLatin1String("idle")) {
        setLastError(tr("Disconnect from the radio first, then change the SmartLink account."));
        return;
    }
    m_favouriteSerial.clear();
    m_favouriteTimer.stop();
    setLastError({});
    useSmartLinkAccount(id);
}

QVariantList MobileEngine::smartLinkFavourites() const
{
    QVariantList out;
    const QJsonArray accounts = loadSmartLinkAccounts();
    for (const QJsonValue& v : loadArray(kFavouritesKey)) {
        const QJsonObject f = v.toObject();
        const QString account = f.value(QStringLiteral("account")).toString();
        const QString serial = f.value(QStringLiteral("serial")).toString();
        const int a = smartLinkAccountIndex(account);
        const bool active = account == m_smartLinkActiveId;
        const bool online = active && std::any_of(m_wanRadios.cbegin(), m_wanRadios.cend(), [&serial](const WanRadioInfo& r) {
            return r.serial == serial && r.publicTlsPort > 0 && !r.publicIp.isEmpty();
        });
        out << QVariantMap{
            {QStringLiteral("name"), f.value(QStringLiteral("name")).toString()},
            {QStringLiteral("account"), a >= 0 ? label(accounts.at(a).toObject(), a) : QString()},
            {QStringLiteral("serial"), serial},
            {QStringLiteral("active"), active},
            {QStringLiteral("online"), online},
            {QStringLiteral("canConnect"), a >= 0 && !loadSecret(smartLinkTokenKey(account)).isEmpty()},
        };
    }
    return out;
}

QStringList MobileEngine::smartLinkFavouriteSerials() const
{
    QStringList serials;
    for (const QJsonValue& v : loadArray(kFavouritesKey)) {
        const QJsonObject f = v.toObject();
        if (f.value(QStringLiteral("account")).toString() == m_smartLinkActiveId) {
            serials << f.value(QStringLiteral("serial")).toString();
        }
    }
    return serials;
}

void MobileEngine::toggleSmartLinkFavourite(int index)
{
    if (index < 0 || index >= m_wanRadios.size()) {
        return;
    }
    const WanRadioInfo& radio = m_wanRadios.at(index);
    QJsonArray favourites = loadArray(kFavouritesKey);
    for (qsizetype i = 0; i < favourites.size(); ++i) {
        const QJsonObject f = favourites.at(i).toObject();
        if (f.value(QStringLiteral("account")).toString() == m_smartLinkActiveId
            && f.value(QStringLiteral("serial")).toString() == radio.serial) {
            favourites.removeAt(i);
            storeArray(kFavouritesKey, favourites);
            emit smartLinkFavouritesChanged();
            emit smartLinkRadiosChanged();
            return;
        }
    }
    if (favourites.size() >= kMaxFavourites) {
        setLastError(tr("Up to %1 favourites: remove one first.").arg(kMaxFavourites));
        return;
    }
    const QString nickname = radio.nickname.trimmed();
    const QString model = radio.model.trimmed();
    const QString name = nickname.isEmpty() ? model
                       : model.isEmpty()    ? nickname
                                            : nickname + QStringLiteral(" — ") + model;
    favourites.append(QJsonObject{{QStringLiteral("account"), m_smartLinkActiveId},
                                  {QStringLiteral("serial"), radio.serial},
                                  {QStringLiteral("name"), name}});
    storeArray(kFavouritesKey, favourites);
    qCInfo(lcMobile) << "SmartLink: favourite added," << favourites.size() << "in all";
    emit smartLinkFavouritesChanged();
    emit smartLinkRadiosChanged();
}

void MobileEngine::removeSmartLinkFavourite(int index)
{
    QJsonArray favourites = loadArray(kFavouritesKey);
    if (index < 0 || index >= favourites.size()) {
        return;
    }
    favourites.removeAt(index);
    storeArray(kFavouritesKey, favourites);
    emit smartLinkFavouritesChanged();
    emit smartLinkRadiosChanged();
}

void MobileEngine::connectSmartLinkFavourite(int index)
{
    const QJsonArray favourites = loadArray(kFavouritesKey);
    if (index < 0 || index >= favourites.size() || m_connectionState != QLatin1String("idle")) {
        return;
    }
    const QJsonObject f = favourites.at(index).toObject();
    const QString account = f.value(QStringLiteral("account")).toString();
    const QString serial = f.value(QStringLiteral("serial")).toString();
    const int a = smartLinkAccountIndex(account);
    if (a < 0 || loadSecret(smartLinkTokenKey(account)).isEmpty()) {
        setLastError(tr("Sign in to that SmartLink account first."));
        return;
    }
    setLastError({});
    m_favouriteSerial = serial;
    m_favouriteTimer.start(kFavouriteWaitMs);
    qCInfo(lcMobile) << "SmartLink: favourite" << index + 1 << (account == m_smartLinkActiveId ? "on the active account"
                                                                                             : "on another account");
    if (account != m_smartLinkActiveId || m_smartLinkState == QLatin1String("signedOut")) {
        useSmartLinkAccount(account);
        return;
    }
    favouriteRadioListed();
}

// The account's list has come: the favourite waiting for it connects.
void MobileEngine::favouriteRadioListed()
{
    if (m_favouriteSerial.isEmpty() || m_connectionState != QLatin1String("idle")) {
        return;
    }
    for (qsizetype i = 0; i < m_wanRadios.size(); ++i) {
        if (m_wanRadios.at(i).serial == m_favouriteSerial) {
            m_favouriteSerial.clear();
            m_favouriteTimer.stop();
            connectSmartLinkRadio(int(i));
            return;
        }
    }
}

} // namespace AetherSDR::mobile
