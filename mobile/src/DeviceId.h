#pragma once

#include <QString>

namespace AetherSDR::mobile {

// The platform's own identifier for this device where Qt has none: iOS's
// identifierForVendor (a restored backup on another device gets a new one).
// Empty elsewhere: Android's ANDROID_ID is read by AppSettings itself, the
// desktop has Qt's machine id.
QString deviceId();

} // namespace AetherSDR::mobile
