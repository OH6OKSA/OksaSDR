#include "DeviceId.h"

#import <UIKit/UIKit.h>

namespace AetherSDR::mobile {

// Not ARC: identifierForVendor and its string are autoreleased, read here
// at once.
QString deviceId()
{
    NSUUID* id = UIDevice.currentDevice.identifierForVendor;
    return id ? QString::fromNSString(id.UUIDString) : QString();
}

} // namespace AetherSDR::mobile
