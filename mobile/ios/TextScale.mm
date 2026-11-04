#include "TextScale.h"

#import <UIKit/UIKit.h>

namespace AetherSDR::mobile {

// UIKit's content size categories against the default (Large), as Apple's
// body text grows: 17 pt at Large, 19 at XL, 21 XXL, 23 XXXL, then the
// accessibility sizes. The smaller ones shrink it (14 pt at XS).
double systemTextScale()
{
    __block NSString* category = nil;
    if ([NSThread isMainThread]) {
        category = UIApplication.sharedApplication.preferredContentSizeCategory;
    } else {
        dispatch_sync(dispatch_get_main_queue(), ^{
            category = UIApplication.sharedApplication.preferredContentSizeCategory;
        });
    }
    if (!category) return 1.0;
    struct Size { NSString* name; double factor; };
    const Size sizes[] = {
        {UIContentSizeCategoryExtraSmall, 14.0 / 17.0},
        {UIContentSizeCategorySmall, 15.0 / 17.0},
        {UIContentSizeCategoryMedium, 16.0 / 17.0},
        {UIContentSizeCategoryLarge, 1.0},
        {UIContentSizeCategoryExtraLarge, 19.0 / 17.0},
        {UIContentSizeCategoryExtraExtraLarge, 21.0 / 17.0},
        {UIContentSizeCategoryExtraExtraExtraLarge, 23.0 / 17.0},
        {UIContentSizeCategoryAccessibilityMedium, 28.0 / 17.0},
        {UIContentSizeCategoryAccessibilityLarge, 33.0 / 17.0},
        {UIContentSizeCategoryAccessibilityExtraLarge, 40.0 / 17.0},
        {UIContentSizeCategoryAccessibilityExtraExtraLarge, 47.0 / 17.0},
        {UIContentSizeCategoryAccessibilityExtraExtraExtraLarge, 53.0 / 17.0},
    };
    for (const Size& s : sizes) {
        if ([category isEqualToString:s.name]) return s.factor;
    }
    return 1.0;
}

} // namespace AetherSDR::mobile
