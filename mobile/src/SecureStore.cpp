#include "SecureStore.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QStandardPaths>

#ifdef OKSASDR_KEYCHAIN
#include <QEventLoop>
#include <QTimer>
#include <qt6keychain/keychain.h>
#endif

Q_DECLARE_LOGGING_CATEGORY(lcMobile)

namespace AetherSDR::mobile {

// The desktop: the secrets the app keeps (SmartLink sign-ins, the QRZ.com
// password, logbook API keys) go to the desktop's Secret Service (GNOME
// Keyring, KWallet, KeePassXC) through qtkeychain, under the service name
// "OksaSDR" (roadmap 91). A build without qtkeychain (the development
// build), or a desktop with no Secret Service, keeps them in a file only
// this user can read, so test runs keep them between runs. A secret found
// in that file moves to the Secret Service the first time it is read. The
// iOS and Android builds use the platform's secure store instead.
namespace {

QString secretsPath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/secrets.json");
}

QJsonObject fileReadAll()
{
    QFile file(secretsPath());
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

bool fileWriteAll(const QJsonObject& secrets)
{
    if (secrets.isEmpty()) {
        return !QFile::exists(secretsPath()) || QFile::remove(secretsPath());
    }
    QSaveFile file(secretsPath());
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.write(QJsonDocument(secrets).toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        return false;
    }
    return QFile::setPermissions(secretsPath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

bool fileSave(const QString& key, const QString& value)
{
    QJsonObject secrets = fileReadAll();
    secrets.insert(key, value);
    return fileWriteAll(secrets);
}

QString fileLoad(const QString& key)
{
    return fileReadAll().value(key).toString();
}

void fileRemove(const QString& key)
{
    QJsonObject secrets = fileReadAll();
    if (secrets.contains(key)) {
        secrets.remove(key);
        fileWriteAll(secrets);
    }
}

#ifdef OKSASDR_KEYCHAIN
const QString kService = QStringLiteral("OksaSDR");

enum class Outcome { Done, NotFound, Denied, NoService };

// No Secret Service this session (none running, or no backend): the file
// from then on, said once in the log.
bool g_noService = false;
// What was read or written this session: the Secret Service is asked once.
QHash<QString, QString> g_cache;

// One keychain job run to its end. The Secret Service may first ask to
// unlock the keyring; meanwhile the window draws but takes no input.
Outcome run(QKeychain::Job* job, QString* text = nullptr)
{
    job->setAutoDelete(false);
    bool finished = false;
    QEventLoop loop;
    QObject::connect(job, &QKeychain::Job::finished, &loop, [&finished, &loop] {
        finished = true;
        loop.quit();
    });
    QTimer::singleShot(120000, &loop, &QEventLoop::quit);
    job->start();
    if (!finished) {
        loop.exec(QEventLoop::ExcludeUserInputEvents);
    }
    Outcome outcome = Outcome::Done;
    if (!finished) {
        qCWarning(lcMobile) << "secure store: no answer from the Secret Service in 2 min";
        outcome = Outcome::Denied;
    } else {
        switch (job->error()) {
        case QKeychain::NoError:
            if (text) {
                *text = static_cast<QKeychain::ReadPasswordJob*>(job)->textData();
            }
            break;
        case QKeychain::EntryNotFound:
            outcome = Outcome::NotFound;
            break;
        case QKeychain::AccessDeniedByUser:
        case QKeychain::AccessDenied:
            qCWarning(lcMobile).noquote() << "secure store: the keyring was not opened:" << job->errorString();
            outcome = Outcome::Denied;
            break;
        default:
            qCWarning(lcMobile).noquote() << "secure store: no Secret Service (" << job->errorString()
                                          << "), the secrets stay in a file only this user can read";
            g_noService = true;
            outcome = Outcome::NoService;
            break;
        }
    }
    job->deleteLater();
    return outcome;
}

Outcome keychainWrite(const QString& key, const QString& value)
{
    auto* job = new QKeychain::WritePasswordJob(kService);
    job->setKey(key);
    job->setTextData(value);
    return run(job);
}

Outcome keychainRead(const QString& key, QString* value)
{
    auto* job = new QKeychain::ReadPasswordJob(kService);
    job->setKey(key);
    return run(job, value);
}

Outcome keychainRemove(const QString& key)
{
    auto* job = new QKeychain::DeletePasswordJob(kService);
    job->setKey(key);
    return run(job);
}
#endif

} // namespace

bool saveSecret(const QString& key, const QString& value)
{
#ifdef OKSASDR_KEYCHAIN
    if (!g_noService) {
        const Outcome outcome = keychainWrite(key, value);
        if (outcome == Outcome::Done) {
            g_cache.insert(key, value);
            fileRemove(key);
            return true;
        }
        if (outcome != Outcome::NoService) {
            return false;
        }
    }
#endif
    return fileSave(key, value);
}

QString loadSecret(const QString& key)
{
#ifdef OKSASDR_KEYCHAIN
    if (const auto it = g_cache.constFind(key); it != g_cache.cend()) {
        return it.value();
    }
    if (!g_noService) {
        QString value;
        const Outcome outcome = keychainRead(key, &value);
        if (outcome == Outcome::Done) {
            g_cache.insert(key, value);
            return value;
        }
        if (outcome == Outcome::Denied) {
            return {};
        }
        if (outcome == Outcome::NotFound) {
            // Kept in the file by an earlier build: into the Secret Service.
            value = fileLoad(key);
            if (!value.isEmpty() && keychainWrite(key, value) == Outcome::Done) {
                qCInfo(lcMobile).noquote() << "secure store:" << key << "moved from the file to the Secret Service";
                g_cache.insert(key, value);
                fileRemove(key);
            }
            return value;
        }
    }
#endif
    return fileLoad(key);
}

void removeSecret(const QString& key)
{
#ifdef OKSASDR_KEYCHAIN
    g_cache.remove(key);
    if (!g_noService) {
        keychainRemove(key);
    }
#endif
    fileRemove(key);
}

bool secureStoreAvailable()
{
    return true;
}

} // namespace AetherSDR::mobile
