#include "ShareText.h"

#include <QClipboard>
#include <QGuiApplication>

namespace AetherSDR::mobile {

void copyText(const QString& text)
{
    QGuiApplication::clipboard()->setText(text);
}

bool shareText(const QString&, const QString& text)
{
    copyText(text);
    return false;
}

bool shareFile(const QString&, const QString& path, const QString&)
{
    copyText(path);
    return false;
}

} // namespace AetherSDR::mobile
