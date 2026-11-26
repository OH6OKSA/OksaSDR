#include "AudioSession.h"

#import <AVFAudio/AVFAudio.h>
#import <UIKit/UIKit.h>

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcAudioSession, "aether.mobile.audiosession")

namespace AetherSDR::mobile {

namespace {

// Whether the last activation took. A launch iOS makes in the background
// (TestFlight update, prewarm) cannot activate a session; it is retried
// when the app comes to the front.
bool g_active = false;
// A playing-only session since an interruption ended in the background
// (roadmap 215). iOS does not let a play-and-record session start there, so
// after a call, an alarm or another app's sound receive stayed silent until
// the app was opened, and with nothing playing iOS suspended the app and the
// radio dropped the connection. Playing alone may start in the background.
// The play-and-record session is taken back when the app is in front, before
// any microphone opens.
bool g_playbackOnly = false;
std::function<void()> g_reactivated;
QString g_outputMode = QStringLiteral("Auto");

// Auto: Bluetooth hands-free (with the headset's microphone) or media,
// else the speaker. Speaker: no Bluetooth route at all. Bluetooth: the media
// link only, so the device's microphone with better receive sound.
AVAudioSessionCategoryOptions categoryOptions()
{
    AVAudioSessionCategoryOptions options = AVAudioSessionCategoryOptionDefaultToSpeaker;
    if (g_outputMode == QLatin1String("Auto")) {
        options |= AVAudioSessionCategoryOptionAllowBluetooth | AVAudioSessionCategoryOptionAllowBluetoothA2DP;
    } else if (g_outputMode == QLatin1String("Bluetooth")) {
        options |= AVAudioSessionCategoryOptionAllowBluetoothA2DP;
    }
    return options;
}

void activate()
{
    AVAudioSession* session = [AVAudioSession sharedInstance];
    NSError* error = nil;
    if (![session setCategory:AVAudioSessionCategoryPlayAndRecord
                         mode:AVAudioSessionModeDefault
                      options:categoryOptions()
                        error:&error]) {
        qCWarning(lcAudioSession) << "setCategory failed:"
                                  << QString::fromNSString(error.localizedDescription);
    }
    // Ask for 48 kHz, the rate the TX voice chain works at: in play-and-record
    // with the built-in mic some iPhones otherwise run the input at a lower
    // rate than the format the app is handed (a tester's iPhone 17 sent 71 %
    // of real time, so speech came out broken).
    error = nil;
    [session setPreferredSampleRate:48000.0 error:&error];
    error = nil;
    if (![session setActive:YES error:&error]) {
        g_active = false;
        const bool background = UIApplication.sharedApplication.applicationState != UIApplicationStateActive;
        if (background) {
            qCInfo(lcAudioSession) << "setActive deferred (app not in front yet):"
                                   << QString::fromNSString(error.localizedDescription)
                                   << "code" << static_cast<qlonglong>(error.code);
        } else {
            qCWarning(lcAudioSession) << "setActive failed:"
                                      << QString::fromNSString(error.localizedDescription)
                                      << "code" << static_cast<qlonglong>(error.code);
        }
    } else {
        g_active = true;
        g_playbackOnly = false;
        qCInfo(lcAudioSession) << "play-and-record session active, route"
                               << QString::fromNSString(session.currentRoute.outputs.firstObject.portType)
                               << "rate" << session.sampleRate << "Hz, input channels"
                               << static_cast<int>(session.inputNumberOfChannels) << ", IO buffer"
                               << qRound(session.IOBufferDuration * 1000.0) << "ms";
    }
}

// A playing-only session for the background (roadmap 215): no microphone, so
// iOS lets it start there. It plays where iOS routes media (a Bluetooth
// device's media link, else the speaker): the options of the play-and-record
// session do not exist for it. True if it took.
bool activatePlaybackOnly()
{
    AVAudioSession* session = [AVAudioSession sharedInstance];
    NSError* error = nil;
    if (![session setCategory:AVAudioSessionCategoryPlayback
                         mode:AVAudioSessionModeDefault
                      options:static_cast<AVAudioSessionCategoryOptions>(0)
                        error:&error]) {
        qCWarning(lcAudioSession) << "playing-only session: setCategory failed:"
                                  << QString::fromNSString(error.localizedDescription)
                                  << "code" << static_cast<qlonglong>(error.code);
        return false;
    }
    error = nil;
    if (![session setActive:YES error:&error]) {
        qCInfo(lcAudioSession) << "playing-only session not taken either:"
                               << QString::fromNSString(error.localizedDescription)
                               << "code" << static_cast<qlonglong>(error.code);
        return false;
    }
    g_active = true;
    g_playbackOnly = true;
    qCInfo(lcAudioSession) << "playing-only session active in the background (play-and-record again when the app is in front), route"
                           << QString::fromNSString(session.currentRoute.outputs.firstObject.portType);
    return true;
}

} // namespace

void setAudioOutputMode(const QString& mode)
{
    if (mode == g_outputMode) {
        return;
    }
    g_outputMode = mode;
    // A session already running takes the new options now (the route is
    // rebuilt once, on the operator's choice); a playing-only one in the
    // background takes them when the app is in front again.
    if (g_active && !g_playbackOnly) {
        activate();
    }
    qCInfo(lcAudioSession).noquote() << "audio output" << mode << "route"
        << QString::fromNSString([AVAudioSession sharedInstance].currentRoute.outputs.firstObject.portType);
}

void setAudioSessionReactivatedHandler(std::function<void()> handler)
{
    g_reactivated = std::move(handler);
}

bool audioSessionPlaybackOnly()
{
    return g_playbackOnly;
}

void configureAudioSession()
{
    // Already active as play-and-record: leave it. Re-activating on every PTT
    // press took ~0.6 s before the microphone opened and could be heard as a
    // whoosh while the route was rebuilt.
    AVAudioSession* session = [AVAudioSession sharedInstance];
    // A playing-only session in the background (roadmap 215) is left as it
    // is: asked for there, play-and-record would be refused and receive lost
    // again. In front it gives way, and receive is opened again on the new
    // session.
    const bool wasPlaybackOnly = g_playbackOnly;
    const bool keepPlaybackOnly = g_playbackOnly && g_active
        && UIApplication.sharedApplication.applicationState == UIApplicationStateBackground;
    if (!keepPlaybackOnly
        && (!g_active || ![session.category isEqualToString:AVAudioSessionCategoryPlayAndRecord])) {
        activate();
        if (wasPlaybackOnly && g_active && !g_playbackOnly && g_reactivated) {
            g_reactivated();
        }
    }
    static bool observing = false;
    if (observing) {
        return;
    }
    observing = true;
    // After a call, alarm or Siri the system ends the interruption; take the
    // session back so receive audio resumes.
    [[NSNotificationCenter defaultCenter]
        addObserverForName:AVAudioSessionInterruptionNotification
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(NSNotification* note) {
                    const auto type = static_cast<AVAudioSessionInterruptionType>(
                        [note.userInfo[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue]);
                    qCInfo(lcAudioSession) << "interruption"
                                           << (type == AVAudioSessionInterruptionTypeBegan ? "began" : "ended");
                    if (type == AVAudioSessionInterruptionTypeBegan) {
                        g_active = false;
                    } else if (type == AVAudioSessionInterruptionTypeEnded) {
                        const NSUInteger options =
                            [note.userInfo[AVAudioSessionInterruptionOptionKey] unsignedIntegerValue];
                        const bool resume = (options & AVAudioSessionInterruptionOptionShouldResume) != 0;
                        activate();
                        // In the background that is refused (roadmap 215):
                        // playing alone is taken instead, when iOS says to
                        // resume or nothing else is playing, so that receive
                        // is heard again and the app stays running. Only
                        // here, where iOS has said the other sound is over:
                        // a retry on a timer would take another app's sound
                        // away.
                        if (!g_active
                            && UIApplication.sharedApplication.applicationState == UIApplicationStateBackground) {
                            const bool other = [[AVAudioSession sharedInstance] isOtherAudioPlaying] == YES;
                            qCInfo(lcAudioSession) << "interruption ended in the background: iOS says resume"
                                                   << resume << ", other audio playing" << other;
                            if (resume || !other) {
                                activatePlaybackOnly();
                            }
                        }
                        if (g_active && g_reactivated) {
                            g_reactivated();
                        }
                    }
                }];
    // A session that could not start at launch: take it once the app is
    // in front.
    [[NSNotificationCenter defaultCenter]
        addObserverForName:UIApplicationDidBecomeActiveNotification
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(NSNotification*) {
                    // Also a playing-only session of the background (roadmap
                    // 215): play-and-record back before a microphone opens.
                    if (!g_active || g_playbackOnly) {
                        qCInfo(lcAudioSession) << (g_playbackOnly
                                                       ? "app in front: play-and-record in place of the playing-only session"
                                                       : "app in front: activating the session");
                        activate();
                        if (g_active && g_reactivated) {
                            g_reactivated();
                        }
                    }
                }];
}

} // namespace AetherSDR::mobile
