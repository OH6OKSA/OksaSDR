#pragma once

#include <QString>

namespace AetherSDR::mobile {

// Puts text on the system pasteboard (iOS: UIPasteboard, which Qt's
// clipboard did not reliably reach for testers).
void copyText(const QString& text);

// iOS and Android: opens the share sheet (Mail, Messages, …) with the text.
// Elsewhere it copies instead. Returns false if no sheet could be shown.
bool shareText(const QString& subject, const QString& text);

// iOS and Android: opens the share sheet with a file (Mail attachment,
// Files, AirDrop, Drive, …). On Android the file must be in the app's files
// folder (the FileProvider's path). Elsewhere it copies the path instead.
bool shareFile(const QString& subject, const QString& path, const QString& mimeType);

// A mail to the developer with a file attached (Dennis Daniel KM6DF: Share
// left the address to fill in). iOS: Mail's compose sheet with the address,
// subject, body and the file; with no Mail account the share sheet with the
// file, the address copied for pasting. Android: the mail apps (ACTION_SEND
// to the address, the file through the FileProvider), any share target when
// no mail app takes it. Elsewhere the address is copied.
enum class DeveloperMail { NotShown, Mail, ShareSheet };
DeveloperMail sendToDeveloper(const QString& address, const QString& subject, const QString& body,
                              const QString& path);

// The device's model for a report ("iPhone16,1", "OnePlus CPH2551").
QString deviceModel();

} // namespace AetherSDR::mobile
