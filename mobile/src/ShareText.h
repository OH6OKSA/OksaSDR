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

} // namespace AetherSDR::mobile
