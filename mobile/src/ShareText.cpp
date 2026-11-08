#include "ShareText.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QSysInfo>

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

DeveloperMail sendToDeveloper(const QString& address, const QString&, const QString&, const QString&)
{
    copyText(address);
    return DeveloperMail::NotShown;
}

QString deviceModel()
{
    return QSysInfo::currentCpuArchitecture();
}

} // namespace AetherSDR::mobile
