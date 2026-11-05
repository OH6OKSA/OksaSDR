#include "TextScale.h"

#include "AndroidJni.h"

namespace AetherSDR::mobile {

// Configuration.fontScale: 1.0 at the default, 0.85 small, 1.15 large,
// 1.3 largest, up to 2.0 on Android 14's non-linear font scaling.
double systemTextScale()
{
    const QJniObject resources = androidContext().callMethod<jobject>("getResources", "()Landroid/content/res/Resources;");
    if (!resources.isValid()) return 1.0;
    const QJniObject config = resources.callMethod<jobject>("getConfiguration", "()Landroid/content/res/Configuration;");
    if (!config.isValid()) return 1.0;
    const jfloat scale = config.getField<jfloat>("fontScale");
    return scale > 0.0f ? double(scale) : 1.0;
}

// Not yet on Android: Display size at its largest could narrow a phone below
// 375 dp too, to be checked on a device first.
double narrowScreenScale()
{
    return 1.0;
}

} // namespace AetherSDR::mobile
