#include "Haptics.h"

#include "AndroidJni.h"

namespace AetherSDR::mobile {

// The window's own view gives the tap (View.performHapticFeedback): no
// vibrate permission, and Android's touch feedback setting decides whether
// it is felt.
void haptic(Haptic kind)
{
    // HapticFeedbackConstants: CLOCK_TICK, VIRTUAL_KEY, VIRTUAL_KEY_RELEASE
    // (Android 8.1; earlier ones ignore it).
    const jint constant = kind == Haptic::Step ? 4 : kind == Haptic::Press ? 1 : 8;
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([constant] {
        const QJniObject window = androidContext().callMethod<jobject>("getWindow", "()Landroid/view/Window;");
        if (!window.isValid()) {
            return;
        }
        const QJniObject view = window.callMethod<jobject>("getDecorView", "()Landroid/view/View;");
        if (view.isValid()) {
            view.callMethod<jboolean>("performHapticFeedback", "(I)Z", constant);
        }
    });
}

} // namespace AetherSDR::mobile
