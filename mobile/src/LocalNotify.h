#pragma once

#include <QString>

namespace AetherSDR::mobile {

// A local notification from the app itself, for a spot alert while the app
// is away from the front (roadmap 132): iOS's and the Mac's
// UNUserNotificationCenter (ios/LocalNotify.mm), Android's
// NotificationManager (Platform.postNotification); nothing elsewhere.
// requestLocalNotifications asks the system once for permission (iOS; on
// Android 13+ the app already asks for notifications for its background
// receive).
void requestLocalNotifications();
void postLocalNotification(const QString& title, const QString& body);

} // namespace AetherSDR::mobile
