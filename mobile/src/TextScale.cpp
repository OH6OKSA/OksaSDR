#include "TextScale.h"

#include <QString>
#include <QtGlobal>

namespace AetherSDR::mobile {

// No system text size on the desktop: the render tool sets one to check the
// layouts at the larger sizes.
double systemTextScale()
{
    bool ok = false;
    const double v = qEnvironmentVariable("OKSASDR_TEXT_SCALE").toDouble(&ok);
    return ok && v > 0.0 ? v : 1.0;
}

double narrowScreenScale()
{
    return 1.0;
}

} // namespace AetherSDR::mobile
