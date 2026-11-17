#pragma once

#include <QString>

namespace AetherSDR::mobile {

// Small secrets (SmartLink sign-ins, the QRZ.com password, logbook API
// keys) in the platform's secure store: the iOS Keychain, on Android a key
// in the Android Keystore that encrypts them, on the Linux desktop the
// Secret Service through qtkeychain. Only the desktop falls back to a file
// this user alone can read: in a build without qtkeychain (the development
// build) or on a desktop with no Secret Service (SecureStore.cpp).
bool saveSecret(const QString& key, const QString& value);
QString loadSecret(const QString& key);
void removeSecret(const QString& key);
bool secureStoreAvailable();

} // namespace AetherSDR::mobile
