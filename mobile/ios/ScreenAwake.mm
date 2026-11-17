#include "ScreenAwake.h"

#import <UIKit/UIKit.h>

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcBackground, "aether.mobile.background")

namespace AetherSDR::mobile {

void setKeepScreenAwake(bool awake)
{
    dispatch_async(dispatch_get_main_queue(), ^{
        [UIApplication sharedApplication].idleTimerDisabled = awake ? YES : NO;
    });
}

namespace {
// The background task held for a reconnect (main thread only).
UIBackgroundTaskIdentifier g_reconnectTask = UIBackgroundTaskInvalid;
}  // namespace

void setBackgroundTimeHeld(bool held)
{
    dispatch_async(dispatch_get_main_queue(), ^{
        UIApplication* app = [UIApplication sharedApplication];
        if (held) {
            if (g_reconnectTask != UIBackgroundTaskInvalid) {
                return;
            }
            g_reconnectTask = [app beginBackgroundTaskWithName:@"OksaSDR reconnect"
                                             expirationHandler:^{
                                                 qCInfo(lcBackground) << "background time for the reconnect ran out";
                                                 [[UIApplication sharedApplication] endBackgroundTask:g_reconnectTask];
                                                 g_reconnectTask = UIBackgroundTaskInvalid;
                                             }];
            if (g_reconnectTask == UIBackgroundTaskInvalid) {
                qCInfo(lcBackground) << "background time for the reconnect not given";
            } else if (app.applicationState == UIApplicationStateBackground) {
                // DBL_MAX while iOS keeps the app running anyway (its audio
                // session): no limit, said so, not as 2147483647 s.
                const NSTimeInterval left = app.backgroundTimeRemaining;
                if (left > 24 * 3600.0) {
                    qCInfo(lcBackground) << "background time held for the reconnect: no limit from iOS now";
                } else {
                    qCInfo(lcBackground) << "background time held for the reconnect:" << qRound(left) << "s";
                }
            } else {
                qCInfo(lcBackground) << "background time held for the reconnect (app in front)";
            }
        } else if (g_reconnectTask != UIBackgroundTaskInvalid) {
            [app endBackgroundTask:g_reconnectTask];
            g_reconnectTask = UIBackgroundTaskInvalid;
            qCInfo(lcBackground) << "background time for the reconnect released";
        }
    });
}

} // namespace AetherSDR::mobile
