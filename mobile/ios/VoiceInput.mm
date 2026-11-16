#include "VoiceInput.h"

#include "VoskSpeech.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>

#import <AVFAudio/AVFAudio.h>
#import <Speech/Speech.h>

#include <cstring>
#include <memory>

namespace AetherSDR::mobile {

namespace {

// Not ARC: the recogniser, the request and the task of the recognition
// under way are kept here until the next one (the task needs both alive).
SFSpeechRecognizer* g_recognizer = nil;
SFSpeechAudioBufferRecognitionRequest* g_request = nil;
SFSpeechRecognitionTask* g_task = nil;

void keep(SFSpeechRecognizer* recognizer, SFSpeechAudioBufferRecognitionRequest* request)
{
    if (g_task) {
        [g_task cancel];
        [g_task release];
        g_task = nil;
    }
    [g_request release];
    g_request = [request retain];
    if (recognizer != g_recognizer) {
        [g_recognizer release];
        g_recognizer = [recognizer retain];
    }
}

// Callsigns are spelled in English, whatever the device's language.
SFSpeechRecognizer* englishRecognizer()
{
    if (!g_recognizer) {
        g_recognizer = [[SFSpeechRecognizer alloc] initWithLocale:[NSLocale localeWithLocaleIdentifier:@"en-US"]];
    }
    return g_recognizer;
}

QString tr(const char* text)
{
    return QCoreApplication::translate("VoiceInput", text);
}

// Vosk held to the alphabet when this build has it (roadmap 81: Apple's
// recogniser cut calls at a word it misheard, build 174); Apple's otherwise.
bool voskHere()
{
    static const bool here = VoskSpeech::unavailableReason().isEmpty();
    return here;
}

} // namespace

bool voiceInputAvailable()
{
    if (voskHere()) {
        return true;
    }
    SFSpeechRecognizer* recognizer = englishRecognizer();
    return recognizer && recognizer.isAvailable;
}

QString voiceInputUnavailableReason()
{
    return voiceInputAvailable() ? QString() : tr("Speech recognition is not available on this device now.");
}

void recognizeSpeech(const QByteArray& floatMono, int rate, const QStringList& vocabulary,
                     std::function<void(const QStringList&, const QString&)> done)
{
    if (voskHere()) {
        VoskSpeech::recognizeClip(floatMono, rate, std::move(done));
        return;
    }
    // The answer on the GUI thread, once.
    auto finish = std::make_shared<std::function<void(const QStringList&, const QString&)>>(std::move(done));
    auto answer = [finish](const QStringList& alternatives, const QString& error) {
        QMetaObject::invokeMethod(qApp, [finish, alternatives, error] {
            if (*finish) {
                auto f = std::move(*finish);
                *finish = nullptr;
                f(alternatives, error);
            }
        }, Qt::QueuedConnection);
    };
    const int frames = floatMono.size() / int(sizeof(float));
    if (frames <= 0 || rate <= 0) {
        answer({}, tr("Nothing was recorded."));
        return;
    }
    QByteArray samples = floatMono;
    NSMutableArray<NSString*>* words = [NSMutableArray arrayWithCapacity:vocabulary.size()];
    for (const QString& w : vocabulary) {
        [words addObject:w.toNSString()];
    }
    [words retain];
    [SFSpeechRecognizer requestAuthorization:^(SFSpeechRecognizerAuthorizationStatus status) {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (status != SFSpeechRecognizerAuthorizationStatusAuthorized) {
                [words release];
                answer({}, tr("Speech recognition is not allowed: turn it on in Settings › OksaSDR."));
                return;
            }
            SFSpeechRecognizer* recognizer = englishRecognizer();
            if (!recognizer || !recognizer.isAvailable) {
                [words release];
                answer({}, tr("Speech recognition is not available on this device now."));
                return;
            }
            SFSpeechAudioBufferRecognitionRequest* request = [[[SFSpeechAudioBufferRecognitionRequest alloc] init] autorelease];
            // On the device where it can be: nothing leaves it.
            if (recognizer.supportsOnDeviceRecognition) {
                request.requiresOnDeviceRecognition = YES;
            }
            request.shouldReportPartialResults = NO;
            request.contextualStrings = words;
            [words release];
            AVAudioFormat* format = [[[AVAudioFormat alloc] initWithCommonFormat:AVAudioPCMFormatFloat32
                                                                       sampleRate:rate
                                                                         channels:1
                                                                      interleaved:NO] autorelease];
            AVAudioPCMBuffer* buffer = [[[AVAudioPCMBuffer alloc] initWithPCMFormat:format
                                                                       frameCapacity:AVAudioFrameCount(frames)] autorelease];
            buffer.frameLength = AVAudioFrameCount(frames);
            std::memcpy(buffer.floatChannelData[0], samples.constData(), size_t(frames) * sizeof(float));
            [request appendAudioPCMBuffer:buffer];
            [request endAudio];
            keep(recognizer, request);
            g_task = [[recognizer recognitionTaskWithRequest:request
                                               resultHandler:^(SFSpeechRecognitionResult* result, NSError* error) {
                if (result && result.isFinal) {
                    QStringList alternatives;
                    for (SFTranscription* t in result.transcriptions) {
                        alternatives << QString::fromNSString(t.formattedString);
                    }
                    answer(alternatives, QString());
                } else if (error) {
                    answer({}, QString::fromNSString(error.localizedDescription));
                }
            }] retain];
        });
    }];
}

// The Speech framework reads the app's own recording (recognizeSpeech).
bool voiceInputListensItself()
{
    return false;
}

void startListening(const QStringList&, std::function<void()>,
                    std::function<void(const QStringList&, const QString&)> done)
{
    done({}, tr("Speech recognition is not available on this device now."));
}

void stopListening()
{
}

} // namespace AetherSDR::mobile
