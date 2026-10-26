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
                                   << QString::fromNSString(error.localizedDescription);
        } else {
            qCWarning(lcAudioSession) << "setActive failed:"
                                      << QString::fromNSString(error.localizedDescription);
        }
    } else {
        g_active = true;
        qCInfo(lcAudioSession) << "play-and-record session active, route"
                               << QString::fromNSString(session.currentRoute.outputs.firstObject.portType)
                               << "rate" << session.sampleRate << "Hz, input channels"
                               << static_cast<int>(session.inputNumberOfChannels) << ", IO buffer"
                               << qRound(session.IOBufferDuration * 1000.0) << "ms";
    }
}

} // namespace

void setAudioOutputMode(const QString& mode)
{
    if (mode == g_outputMode) {
        return;
    }
    g_outputMode = mode;
    // A session already running takes the new options now (the route is
    // rebuilt once, on the operator's choice).
    if (g_active) {
        activate();
    }
    qCInfo(lcAudioSession).noquote() << "audio output" << mode << "route"
        << QString::fromNSString([AVAudioSession sharedInstance].currentRoute.outputs.firstObject.portType);
}

void setAudioSessionReactivatedHandler(std::function<void()> handler)
{
    g_reactivated = std::move(handler);
}

void configureAudioSession()
{
    // Already active as play-and-record: leave it. Re-activating on every PTT
    // press took ~0.6 s before the microphone opened and could be heard as a
    // whoosh while the route was rebuilt.
    AVAudioSession* session = [AVAudioSession sharedInstance];
    if (!g_active || ![session.category isEqualToString:AVAudioSessionCategoryPlayAndRecord]) {
        activate();
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
                        activate();
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
                    if (!g_active) {
                        qCInfo(lcAudioSession) << "app in front: activating the session";
                        activate();
                        if (g_active && g_reactivated) {
                            g_reactivated();
                        }
                    }
                }];
}

} // namespace AetherSDR::mobile
