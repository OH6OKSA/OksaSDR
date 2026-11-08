#include "ShareText.h"

#import <MessageUI/MessageUI.h>
#import <UIKit/UIKit.h>

#include <sys/utsname.h>

// Dismisses Mail's compose sheet when it is sent, saved or cancelled.
@interface OksaMailDelegate : NSObject <MFMailComposeViewControllerDelegate>
@end

@implementation OksaMailDelegate
- (void)mailComposeController:(MFMailComposeViewController*)controller
          didFinishWithResult:(MFMailComposeResult)result
                        error:(NSError*)error
{
    (void)result;
    (void)error;
    [controller dismissViewControllerAnimated:YES completion:nil];
}
@end

namespace AetherSDR::mobile {

namespace {

UIViewController* topViewController()
{
    UIWindow* window = nil;
    for (UIScene* scene in UIApplication.sharedApplication.connectedScenes) {
        if (![scene isKindOfClass:[UIWindowScene class]]) {
            continue;
        }
        for (UIWindow* w in static_cast<UIWindowScene*>(scene).windows) {
            if (w.isKeyWindow) {
                window = w;
                break;
            }
        }
        if (window) {
            break;
        }
    }
    UIViewController* top = window.rootViewController;
    while (top.presentedViewController) {
        top = top.presentedViewController;
    }
    return top;
}

} // namespace

void copyText(const QString& text)
{
    UIPasteboard.generalPasteboard.string = text.toNSString();
}

bool shareText(const QString& subject, const QString& text)
{
    UIViewController* top = topViewController();
    if (!top) {
        copyText(text);
        return false;
    }
    UIActivityViewController* sheet =
        [[UIActivityViewController alloc] initWithActivityItems:@[text.toNSString()]
                                          applicationActivities:nil];
    [sheet setValue:subject.toNSString() forKey:@"subject"];
    // iPad shows the sheet as a popover, which needs an anchor.
    if (sheet.popoverPresentationController) {
        sheet.popoverPresentationController.sourceView = top.view;
        sheet.popoverPresentationController.sourceRect =
            CGRectMake(CGRectGetMidX(top.view.bounds), CGRectGetMidY(top.view.bounds), 1, 1);
        sheet.popoverPresentationController.permittedArrowDirections = 0;
    }
    [top presentViewController:sheet animated:YES completion:nil];
    return true;
}

DeveloperMail sendToDeveloper(const QString& address, const QString& subject, const QString& body,
                              const QString& path)
{
    UIViewController* top = topViewController();
    if (!top) {
        return DeveloperMail::NotShown;
    }
    if (![MFMailComposeViewController canSendMail]) {
        // No account in Mail (Gmail or Outlook instead): the share sheet,
        // with the address on the pasteboard for the recipient.
        copyText(address);
        return shareFile(subject, path, QStringLiteral("text/plain")) ? DeveloperMail::ShareSheet
                                                                      : DeveloperMail::NotShown;
    }
    NSData* data = [NSData dataWithContentsOfFile:path.toNSString()];
    if (!data) {
        return DeveloperMail::NotShown;
    }
    // Not ARC: the delegate is kept for the app's life (the sheet holds it
    // weakly); the sheet is autoreleased, the presenting controller keeps it.
    static OksaMailDelegate* delegate = [[OksaMailDelegate alloc] init];
    MFMailComposeViewController* mail = [[[MFMailComposeViewController alloc] init] autorelease];
    mail.mailComposeDelegate = delegate;
    [mail setToRecipients:@[address.toNSString()]];
    [mail setSubject:subject.toNSString()];
    [mail setMessageBody:body.toNSString() isHTML:NO];
    [mail addAttachmentData:data mimeType:@"text/plain" fileName:[path.toNSString() lastPathComponent]];
    [top presentViewController:mail animated:YES completion:nil];
    return DeveloperMail::Mail;
}

QString deviceModel()
{
    struct utsname name;
    if (uname(&name) != 0) {
        return QStringLiteral("unknown");
    }
    return QString::fromLatin1(name.machine);  // "iPhone16,1"; "arm64" for an iOS app on a Mac
}

bool shareFile(const QString& subject, const QString& path, const QString&)
{
    UIViewController* top = topViewController();
    if (!top) {
        return false;
    }
    NSURL* url = [NSURL fileURLWithPath:path.toNSString()];
    UIActivityViewController* sheet = [[UIActivityViewController alloc] initWithActivityItems:@[url]
                                                                        applicationActivities:nil];
    [sheet setValue:subject.toNSString() forKey:@"subject"];
    if (sheet.popoverPresentationController) {
        sheet.popoverPresentationController.sourceView = top.view;
        sheet.popoverPresentationController.sourceRect =
            CGRectMake(CGRectGetMidX(top.view.bounds), CGRectGetMidY(top.view.bounds), 1, 1);
        sheet.popoverPresentationController.permittedArrowDirections = 0;
    }
    [top presentViewController:sheet animated:YES completion:nil];
    return true;
}

} // namespace AetherSDR::mobile
