#include "SecureStore.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

namespace AetherSDR::mobile {

// Desktop development build: no platform keychain here, so the secrets the
// app keeps (QRZ.com password, logbook API key, SmartLink sign-in) go to a
// file only this user can read, so test runs keep them between runs. The
// iOS and Android builds use the platform's secure store instead.
namespace {

QString secretsPath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/secrets.json");
}

QJsonObject readAll()
{
    QFile file(secretsPath());
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

bool writeAll(const QJsonObject& secrets)
{
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

} // namespace

bool saveSecret(const QString& key, const QString& value)
{
    QJsonObject secrets = readAll();
    secrets.insert(key, value);
    return writeAll(secrets);
}

QString loadSecret(const QString& key)
{
    return readAll().value(key).toString();
}

void removeSecret(const QString& key)
{
    QJsonObject secrets = readAll();
    if (secrets.contains(key)) {
        secrets.remove(key);
        writeAll(secrets);
    }
}

bool secureStoreAvailable()
{
    return true;
}

} // namespace AetherSDR::mobile
