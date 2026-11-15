#include "VoiceInput.h"

#include <QCoreApplication>

namespace AetherSDR::mobile {

// The desktop build has no recogniser: the render tool's "voicetext"
// command stands in for one in tests.
bool voiceInputAvailable()
{
    return false;
}

QString voiceInputUnavailableReason()
{
    return QCoreApplication::translate("VoiceInput", "No speech recognition in this build.");
}

void recognizeSpeech(const QByteArray&, int, const QStringList&,
                     std::function<void(const QStringList&, const QString&)> done)
{
    done({}, voiceInputUnavailableReason());
}

} // namespace AetherSDR::mobile
