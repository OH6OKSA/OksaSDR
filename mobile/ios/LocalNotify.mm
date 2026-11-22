#include "LocalNotify.h"

#import <Foundation/Foundation.h>
#import <UserNotifications/UserNotifications.h>

// Local notifications for spot alerts (roadmap 132). This file is not built
// with ARC: what is allocated is released here; the blocks copied by
// dispatch_async retain the strings they capture.

namespace AetherSDR::mobile {

void requestLocalNotifications()
{
    dispatch_async(dispatch_get_main_queue(), ^{
        [[UNUserNotificationCenter currentNotificationCenter]
            requestAuthorizationWithOptions:(UNAuthorizationOptionAlert | UNAuthorizationOptionSound)
                          completionHandler:^(BOOL, NSError*) {}];
    });
}

void postLocalNotification(const QString& title, const QString& body)
{
    NSString* t = title.toNSString();
    NSString* b = body.toNSString();
    dispatch_async(dispatch_get_main_queue(), ^{
        UNMutableNotificationContent* content = [[UNMutableNotificationContent alloc] init];
        content.title = t;
        content.body = b;
        content.sound = [UNNotificationSound defaultSound];
        UNNotificationRequest* request = [UNNotificationRequest requestWithIdentifier:[[NSUUID UUID] UUIDString]
                                                                              content:content
                                                                              trigger:nil];
        [[UNUserNotificationCenter currentNotificationCenter] addNotificationRequest:request withCompletionHandler:nil];
        [content release];
    });
}

} // namespace AetherSDR::mobile
