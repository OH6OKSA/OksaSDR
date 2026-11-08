// Named app setups (Larry Averbeck W9FLA, roadmap 17): the app's own
// settings saved under a name, switched on the Prof tab, exported as a file
// and imported later or on another device. The radio's own settings stay in
// its profiles (the sections above on the Prof tab).
//
// A setup is every app setting (display, meters, panels, DSP, audio and the
// TX chain, macros and CW memories, logbooks, devices, the radio list) with
// the passwords and keys the app keeps in the secure store. Left out: the
// voice keyer's recordings (sound files kept on the device), and what would
// break if copied: the device's GUI client ID (two devices with one ID take
// each other's place on the radio), the logbooks' upload queues (QSOs sent
// twice), what was read from them, version and migration marks, and the
// SmartLink sign-in (a session token of this device).
//
// On the device a setup is a JSON file in the app's data folder; its
// passwords and keys stay in the secure store under the setup's own names.
// An exported file carries them in the file, so the Prof tab says so first.

#include "MobileEngine.h"

#include "SecureStore.h"
#include "ShareText.h"

#include "core/AppSettings.h"
#include "core/AudioEngine.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

Q_DECLARE_LOGGING_CATEGORY(lcMobile)

namespace AetherSDR::mobile {

namespace {

const QString kFormat = QStringLiteral("OksaSDR app setup");
constexpr int kFormatVersion = 1;
const QString kActiveKey = QStringLiteral("MobileAppSetup");
constexpr int kMaxNameLength = 40;
constexpr qint64 kMaxImportBytes = 4 * 1024 * 1024;

// The secure store's entries a setup carries (not the SmartLink sign-in).
const QStringList& secretKeys()
{
    static const QStringList keys{
        QStringLiteral("qrz_password"), QStringLiteral("onlinelog_key"), QStringLiteral("qrzlog_key"),
        QStringLiteral("AgAuthCode"), QStringLiteral("PgxlAuthCode"), QStringLiteral("TgxlAuthCode"),
    };
    return keys;
}

// Settings that are this device's or this moment's, never a setup's.
bool deviceBound(const QString& key)
{
    static const QStringList exact{
        QStringLiteral("GUIClientID"), QStringLiteral("GuiClientIdentity"),
        QStringLiteral("ApplicationVersion"), QStringLiteral("LastSeenVersion"),
        QStringLiteral("Mobile3DRunning"), kActiveKey,
        QStringLiteral("MobileLogger32Pending"), QStringLiteral("MobileOnlineLogPending"),
        QStringLiteral("MobileQrzLogPending"), QStringLiteral("MobileSharedAdifPending"),
        QStringLiteral("MobileOnlineLogReadFrom"), QStringLiteral("MobileOnlineLogReadIds"),
        QStringLiteral("MobileQrzLogReadId"),
    };
    return exact.contains(key) || key.contains(QLatin1String("Migrated"))
        // A recording's sample rate belongs to the sound file on the device.
        || key.startsWith(QLatin1String("MobileVoiceRate"));
}

// Settings whose subsystem reads them when the app starts: applied, they
// take effect at the next start (named so on the Prof tab).
QString nextStartPart(const QString& key)
{
    if (key.startsWith(QLatin1String("MobileAmp"))) {
        return QObject::tr("the amplifier");
    }
    if (key.startsWith(QLatin1String("MobilePgxl")) || key.startsWith(QLatin1String("MobileTgxl"))
        || key.startsWith(QLatin1String("MobileAg")) || key.startsWith(QLatin1String("AG_"))) {
        return QObject::tr("the 4O3A devices");
    }
    if (key.startsWith(QLatin1String("DxCluster"))) {
        return QObject::tr("the DX cluster");
    }
    if (key.startsWith(QLatin1String("MobileDevLink"))) {
        return QObject::tr("the developer link");
    }
    if (key == QLatin1String("AudioCompression") || key == QLatin1String("NetworkMtu")) {
        return QObject::tr("the radio's audio stream (next connect)");
    }
    return {};
}

QString setupsDir()
{
    const QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                            .filePath(QStringLiteral("setups"));
    QDir().mkpath(dir);
    return dir;
}

QString setupPath(const QString& name)
{
    return QDir(setupsDir()).filePath(QString::fromLatin1(QUrl::toPercentEncoding(name)) + QStringLiteral(".json"));
}

// A setup's own secure-store names, so another setup's keys never mix in.
QString secretName(const QString& setup, const QString& key)
{
    const QByteArray id = QCryptographicHash::hash(setup.toUtf8(), QCryptographicHash::Sha1).toHex().left(16);
    return QStringLiteral("appsetup.%1.%2").arg(QString::fromLatin1(id), key);
}

QString cleanName(const QString& name)
{
    QString clean = name.simplified();
    clean.remove(QLatin1Char('/'));
    clean.remove(QLatin1Char('\\'));
    return clean.left(kMaxNameLength);
}

QJsonObject readSetup(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QObject::tr("The setup could not be read.");
        return {};
    }
    if (file.size() > kMaxImportBytes) {
        *error = QObject::tr("The file is too large for an OksaSDR setup.");
        return {};
    }
    QJsonParseError parse;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parse);
    const QJsonObject root = doc.object();
    if (parse.error != QJsonParseError::NoError || root.value(QStringLiteral("format")).toString() != kFormat
        || !root.value(QStringLiteral("settings")).isObject()) {
        *error = QObject::tr("This is not an OksaSDR setup file.");
        return {};
    }
    if (root.value(QStringLiteral("version")).toInt() > kFormatVersion) {
        *error = QObject::tr("The setup is from a newer OksaSDR: update the app first.");
        return {};
    }
    return root;
}

bool writeSetup(const QString& path, const QJsonObject& root)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return file.commit();
}

QVariantMap failed(const QString& error)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
}

} // namespace

QStringList MobileEngine::appSetups() const
{
    QStringList names;
    const QFileInfoList files = QDir(setupsDir()).entryInfoList({QStringLiteral("*.json")}, QDir::Files);
    for (const QFileInfo& info : files) {
        names << QUrl::fromPercentEncoding(info.completeBaseName().toLatin1());
    }
    names.sort(Qt::CaseInsensitive);
    return names;
}

QString MobileEngine::activeAppSetup() const
{
    const QString name = AppSettings::instance().value(kActiveKey).toString();
    return QFile::exists(setupPath(name)) ? name : QString();
}

QVariantMap MobileEngine::saveAppSetup(const QString& requested)
{
    const QString name = cleanName(requested);
    if (name.isEmpty()) {
        return failed(tr("Give the setup a name."));
    }
    AppSettings& settings = AppSettings::instance();
    // Features own their keys; a setup is the whole store, so it reads the
    // keys the way the settings dump does.
    QJsonObject values;
    for (const QString& key : settings.allKeysForDiagnostics()) {
        if (!deviceBound(key)) {
            values.insert(key, settings.value(key).toString());
        }
    }
    const QJsonObject root{
        {QStringLiteral("format"), kFormat},
        {QStringLiteral("version"), kFormatVersion},
        {QStringLiteral("name"), name},
        {QStringLiteral("saved"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("build"), QStringLiteral(AETHER_MOBILE_BUILD_NUMBER)},
        {QStringLiteral("settings"), values},
    };
    if (!writeSetup(setupPath(name), root)) {
        return failed(tr("The setup could not be saved."));
    }
    for (const QString& key : secretKeys()) {
        const QString value = loadSecret(key);
        if (value.isEmpty()) {
            removeSecret(secretName(name, key));
        } else {
            saveSecret(secretName(name, key), value);
        }
    }
    settings.setValue(kActiveKey, name);
    settings.save();
    qCInfo(lcMobile).noquote() << QStringLiteral("app setup \"%1\" saved: %2 settings").arg(name).arg(values.size());
    emit appSetupsChanged();
    return {{QStringLiteral("ok"), true}};
}

QVariantMap MobileEngine::applyAppSetup(const QString& name)
{
    QString error;
    const QJsonObject root = readSetup(setupPath(name), &error);
    if (root.isEmpty()) {
        return failed(error);
    }
    // Not in the middle of a transmission or an FT8 / FT4 run: the audio
    // chain and the digital modes' settings change under them.
    if (m_pttHeld || m_session.radioModel().transmitModel().isTransmitting() || m_digiTransmitting
        || m_textTransmitting) {
        return failed(tr("Not while transmitting: try again after the transmission."));
    }
    if (digiRunning()) {
        return failed(tr("Stop FT8 / FT4 first."));
    }
    AppSettings& settings = AppSettings::instance();
    // A TX chain change waiting to be written would put the old values back.
    m_txChainSaveTimer.stop();
    const QJsonObject values = root.value(QStringLiteral("settings")).toObject();
    QStringList later;
    const auto noteLater = [&later](const QString& key) {
        const QString part = nextStartPart(key);
        if (!part.isEmpty() && !later.contains(part)) {
            later << part;
        }
    };
    int changed = 0;
    // What the setup does not hold goes back to its default, as when the
    // setup was saved.
    for (const QString& key : settings.allKeysForDiagnostics()) {
        if (!deviceBound(key) && !values.contains(key)) {
            noteLater(key);
            settings.remove(key);
            ++changed;
        }
    }
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        if (deviceBound(it.key())) {
            continue;
        }
        const QString value = it.value().toString();
        if (!settings.contains(it.key()) || settings.value(it.key()).toString() != value) {
            noteLater(it.key());
            settings.setValue(it.key(), value);
            ++changed;
        }
    }
    settings.setValue(kActiveKey, name);
    settings.save();
    for (const QString& key : secretKeys()) {
        const QString value = loadSecret(secretName(name, key));
        if (value.isEmpty()) {
            removeSecret(key);
        } else {
            saveSecret(key, value);
        }
    }

    // What the app holds in memory from the settings, read again.
    loadDigiSettings();
    loadFreeDvSettings();
    m_onlineLogKey = loadSecret(QStringLiteral("onlinelog_key"));
    m_qrzLogKey = loadSecret(QStringLiteral("qrzlog_key"));
    const QString sharedUrl = settings.value(QStringLiteral("MobileSharedAdifUrl")).toString();
    if (sharedUrl != m_sharedAdifUrl) {
        m_sharedAdifUrl = sharedUrl;
        m_sharedWritable = settings.value(QStringLiteral("MobileSharedAdifWritable"), QStringLiteral("True")).toString()
                           == QLatin1String("True");
        m_fileKnown.clear();
        m_wkDirty = true;
        if (!m_sharedAdifUrl.isEmpty()) {
            sharedAdifRead();
        }
    }
    if (m_audio) {
        m_audio->loadClientEqSettings();
        m_audio->loadClientGateSettings();
        m_audio->loadClientCompSettings();
        m_audio->loadClientDeEssSettings();
        m_audio->loadClientTubeSettings();
        m_audio->loadClientPuduSettings();
        m_audio->loadClientReverbSettings();
        m_audio->loadClientFinalLimiterSettings();
    }
    applyBeatCancel();
    m_outputDeviceApplied = false;
    applyAudioOutputDevice();
    if (!onlineLogBase().isEmpty() && !m_onlineLogKey.isEmpty()) {
        onlineLogCheck();
    }
    if (!m_qrzLogKey.isEmpty()) {
        qrzLogCheck();
    }
    announceAllProperties();
    qCInfo(lcMobile).noquote() << QStringLiteral("app setup \"%1\" applied: %2 settings changed%3")
                                      .arg(name).arg(changed)
                                      .arg(later.isEmpty() ? QString() : QStringLiteral(", at the next start: ") + later.join(QStringLiteral(", ")));
    return {{QStringLiteral("ok"), true}, {QStringLiteral("later"), later.join(QStringLiteral(", "))}};
}

QVariantMap MobileEngine::deleteAppSetup(const QString& name)
{
    if (!QFile::remove(setupPath(name))) {
        return failed(tr("The setup could not be deleted."));
    }
    for (const QString& key : secretKeys()) {
        removeSecret(secretName(name, key));
    }
    if (AppSettings::instance().value(kActiveKey).toString() == name) {
        AppSettings::instance().remove(kActiveKey);
        AppSettings::instance().save();
    }
    qCInfo(lcMobile).noquote() << QStringLiteral("app setup \"%1\" deleted").arg(name);
    emit appSetupsChanged();
    return {{QStringLiteral("ok"), true}};
}

QVariantMap MobileEngine::exportAppSetup(const QString& name)
{
    QString error;
    QJsonObject root = readSetup(setupPath(name), &error);
    if (root.isEmpty()) {
        return failed(error);
    }
    QJsonObject secrets;
    for (const QString& key : secretKeys()) {
        const QString value = loadSecret(secretName(name, key));
        if (!value.isEmpty()) {
            secrets.insert(key, value);
        }
    }
    root.insert(QStringLiteral("secrets"), secrets);
    // In the app's files folder (Android shares only from there); the next
    // export replaces it.
    const QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("share"));
    QDir().mkpath(dir);
    QString base = name;
    for (QChar& c : base) {
        if (!c.isLetterOrNumber() && c != QLatin1Char('-')) {
            c = QLatin1Char('-');
        }
    }
    const QString path = QDir(dir).filePath(QStringLiteral("OksaSDR-setup-%1.json").arg(base));
    if (!writeSetup(path, root)) {
        return failed(tr("The setup could not be prepared for sharing."));
    }
    qCInfo(lcMobile).noquote() << QStringLiteral("app setup \"%1\" exported, %2 secret(s) in the file").arg(name).arg(secrets.size());
    if (!shareFile(tr("OksaSDR setup %1").arg(name), path, QStringLiteral("application/json"))) {
        return failed(tr("The share sheet could not be opened."));
    }
    return {{QStringLiteral("ok"), true}};
}

QVariantMap MobileEngine::importAppSetup(const QUrl& url)
{
    // Android hands a content:// URL, which QFile reads as it is.
    QString error;
    const QJsonObject root = readSetup(url.isLocalFile() ? url.toLocalFile() : url.toString(), &error);
    if (root.isEmpty()) {
        qCWarning(lcMobile) << "app setup import:" << url << error;
        return failed(error);
    }
    QString name = cleanName(root.value(QStringLiteral("name")).toString());
    if (name.isEmpty()) {
        name = tr("Imported");
    }
    // Never over a setup of the same name: "Contest (2)".
    const QString base = name;
    for (int n = 2; QFile::exists(setupPath(name)); ++n) {
        name = QStringLiteral("%1 (%2)").arg(base.left(kMaxNameLength - 5)).arg(n);
    }
    QJsonObject stored = root;
    const QJsonObject secrets = stored.take(QStringLiteral("secrets")).toObject();
    stored.insert(QStringLiteral("name"), name);
    if (!writeSetup(setupPath(name), stored)) {
        return failed(tr("The setup could not be saved."));
    }
    for (const QString& key : secretKeys()) {
        const QString value = secrets.value(key).toString();
        if (!value.isEmpty()) {
            saveSecret(secretName(name, key), value);
        }
    }
    qCInfo(lcMobile).noquote() << QStringLiteral("app setup \"%1\" imported: %2 settings, %3 secret(s)")
                                      .arg(name).arg(stored.value(QStringLiteral("settings")).toObject().size())
                                      .arg(secrets.size());
    emit appSetupsChanged();
    return {{QStringLiteral("ok"), true}, {QStringLiteral("name"), name}};
}

// Every property's change signal without arguments: the pages read what
// the applied setup holds (most properties read the settings when asked).
void MobileEngine::announceAllProperties()
{
    const QMetaObject* meta = metaObject();
    QList<int> sent;
    for (int i = meta->propertyOffset(); i < meta->propertyCount(); ++i) {
        const QMetaProperty property = meta->property(i);
        if (!property.hasNotifySignal()) {
            continue;
        }
        const QMetaMethod signal = property.notifySignal();
        if (signal.parameterCount() != 0 || sent.contains(signal.methodIndex())) {
            continue;
        }
        sent << signal.methodIndex();
        signal.invoke(this, Qt::DirectConnection);
    }
}

} // namespace AetherSDR::mobile
