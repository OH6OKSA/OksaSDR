#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QStringList>

class QFile;
class QQmlApplicationEngine;
class QTimer;

namespace AetherSDR::mobile {

class MobileEngine;

// Development (--shot-plan): many screenshots in one run, so the manual's
// pictures need one connection and one filled waterfall per window size
// instead of one run of about 45 s per picture. A plan is a text file of
// commands, one a line (# starts a comment):
//
//   wait MS                 wait (the first one lets the display fill)
//   shot NAME               save the window as DIR/NAME.png
//   popup NAME[:SECTION]    open the chooser or dialog with this objectName
//                           (its preview() when it has one)
//   close                   close every open popup and the controls drawer
//   drawer TAB              open the controls drawer on tab TAB
//   scroll PX               scroll the open controls (or Radio Setup) to PX
//                           (-PX: that far above the end)
//   pin I[,J...]            pin these sections in the tablet's panel (the
//                           operator's choice is put back at the end)
//   controls visible|hidden show or hide the tablet's controls
//   frequency-dialog        open frequency entry
//   no-slice on|off         the No slice notice's preview
//   preview-tx on|off       the meters as while transmitting
//   meter-style L,R         analog, bar or two, each side (remembered)
//   tap FX,FY               tap at this fraction of the window
//   tapmhz MHZ              tap the spectrum at that frequency
//   holdmhz MHZ,MS          press the spectrum there for MS (a new notch)
//   hold FX,FY,MS           press there for MS (auto-repeat), logging the frequency
//   freq MHZ                tune the active slice there (to put it back)
//   span KHZ                the pan this wide, centred on the slice
//   mode NAME               the active slice's mode (USB shows the squelch row;
//                           put the mode back after)
//   rfpower W               the TX power (to put it back after a cut-short run)
//   status OBJECT | K=V ... a status line as if the radio had sent it (hardware not
//                           at hand, e.g. "usb_cable FT1 | type=cat plugged_in=1")
//   ptt on|off              key / unkey the radio as PTT held (the TX slice: make it a
//                           DIGU slice with no audio for a carrier-free test)
//   txslice LETTER          that slice active and TX (to put TX back after a test)
//   addslice                a slice of this app's own (+ Slice)
//   closeslice LETTER       close that slice (after addslice)
//   slices                  log every slice (frequency, mode, TX, active, its pan),
//                           the number of panadapters and the power
//   amp HOST[:PORT]|off     the amplifier's address (Radio Setup › Amp); off forgets it,
//                           pause / resume its Read the amplifier switch
//   kpa500 HOST:PORT|off    an Elecraft KPA500's bridge (Radio Setup › Amp); off forgets
//                           it; "control on|off" its Control; "state" logs Engine.kpa500
//   kat500 HOST:PORT|off    the KAT500's, the same
//   appstate STATE          the app's state as the system sets it: active, inactive,
//                           hidden, suspended (background and back)
//   netchange               as the system reporting another network (SmartLink's
//                           server connection is made again)
//   smartlink I             connect to the Connect page's SmartLink radio I
//   slicerecovery           this app's slice and pan removed and made again, as after
//                           a reconnect that gave no slice (the slice put back where it was)
//   dxconnect HOST:PORT     the DX cluster there for this run (settings untouched);
//                           dxstate logs its state and message, dxoff disconnects it
//   kpa HOST[:PORT]|off     the KPA1500's address (Radio Setup › Amp › Elecraft); off
//                           forgets it, pause / resume its Read the amplifier
//   ulanzi KEY VALUE|on|off a Ulanzi Dial key (a Linux code, 0 release / 1 press /
//                           2 repeat; -1 the dial gone), or its switch (roadmap 83)
//   ampstate                log the Amp tab's link state and readings
//   amptune start|stop      the Amp tab's Tune as confirmed (a tune carrier for the
//                           amplifier's tuner) / its Stop
//   tunepower W             the TX tab's Tune power (put it back after)
//   click NAME              clicked() on the visible buttons with this objectName
//   tapname NAME[@TEXT] [~X,Y] a tap through the window on that control's
//                           middle (@TEXT: the one so named that shows TEXT;
//                           ~X,Y: the one nearest to X,Y; holdname too)
//   taptext TEXT [>X] [~X,Y] a tap on the control showing TEXT (one with no
//                           name; >X: only one right of X; ~X,Y: the one
//                           nearest to X,Y)
//   ready                   wait until connected with a slice (up to 30 s),
//                           then 4 s for the display
//   setup KEY=VALUE         a Radio Setup value, as the page sets it (e.g.
//                           showTxInWaterfall=0; put it back after)
//   size WxH                resize the window (a phone turning: WxH swapped)
//   theme dark|light        the page's theme (Controls › View › Theme; remembered:
//                           put dark back at the end)
//   tnfs                    log the notches the radio has and the frequency
//   txchain                 log this device's microphone processing (ESSB)
//   rtty [spots]            log the RTTY decoder: mode, mark, shift, SNR, text
//                           (spots: and the RTTY stations the spots know)
//   rttyfeed TEXT           TEXT as RTTY audio into the running decoder (a test)
//   textmode MODE           the Digi tab's mode: RTTY, PSK31 or PSK63
//   textclear               the Digi tab's text cleared, as its Clear
//   qsodry [off]            QSOs printed to the log, not written or sent
//   loggerudp HOST[:PORT]|off  the logging program address (WSJT-X UDP), as Save
//   loggerudpstate          log its status line
//   pskfeed [+-HZ] TEXT     TEXT as a PSK signal at the decoder's carrier (HZ off it)
//   logqso K=V | K=V ...    log a QSO with those fields (call, rstSent, rstRcvd, timeOn, comment)
//   qsoedit CALL | k=v ...  the newest contact with CALL edited as the Log tab's dialog does
//                           (comment=, rstRcvd=, name=, time=HH:MM; _ for a space)
//   qsodelete CALL          the newest contact with CALL deleted (the dialog's Delete)
//   psk [KEY=VALUE]         setPsk (carrier=HZ, afc=true|false), or log the PSK state
//   focus                   log where the keyboard is (keyboardOnPage, active item)
//   keys TEXT               type TEXT through the window ("\\b": Backspace as a
//                           Mac's UIKit sends it), then log the focused field's text;
//                           <F1>..<F12>, <Esc>, <Enter>, <Cmd-0>..<Cmd-9>, <Shift-A>..<Shift-Z>
//                           alone: that key
//                           <Mac-F1>..<Mac-F12>: an F-key as a Mac hands it to an iPad app
//   scheme light|dark|none  the device's appearance as Qt reports it (Theme Auto)
//   txblock on|off          every transmission refused (as in a link plan), logged
//   sidekey space|spaceheld down|up  a Space as GameController reports it on an iPad
//                           (spaceheld: with Control-Option or Command), to the engine
//   spacemods               log which keys held with Space keep it from PTT
//   keyhold <Fn> MS [go]    that F-key (or <Space>, <Ctrl-Alt-Space>, <Meta-Space>,
//                           <Shift-Space>: Space with those keys) held MS ms: the press, its auto-repeat every
//                           50 ms, the release (the PTT key, F-key repeats); go: the
//                           next steps run while it is held
//   focussweep              give the keyboard to every control in view that takes it
//                           on a press (no press): each must hand it back to the page
//   focusaudit FX,FY        tap every text field in view, then FX,FY: each must
//                           take the keyboard and let it go to the page
//   a11y on|                on: accessibility on; bare: log the screen reader's
//                           elements in view (role, name) and the unnamed buttons
//   a11ypress NAME          the named element's press action (a screen
//                           reader's double tap)
//   a11yinc NAME            its increase action (a swipe up on a slider)
//   a11ydec NAME            its decrease action (a swipe down)
//   layout                  log the page's layout decisions and the info
//                           panels' geometry (layout-diff.py compares runs)
//   record FPS|off          save the window FPS times a second as
//                           DIR/frames/MS.jpg (MS since the start), the RX
//                           audio as DIR/audio.f32 and RADE's speech as
//                           DIR/speech.f32 (24 kHz stereo), and log
//                           each later line with its time, for a video
//                           (mobile/tools/make-video.py); off stops it
//   where TEXT|@NAME|...    log where each visible control showing TEXT (or
//                           named NAME) is, for a plan's coordinates
//   caption TEXT            nothing; with record on its time is logged, so
//                           the video's captions come from the plan
//
// The run quits after the last command (exit status 2 if a shot failed).
class ShotPlan : public QObject {
    Q_OBJECT
public:
    ShotPlan(QQmlApplicationEngine& qml, MobileEngine& engine, const QString& planFile, const QString& outDir,
             QObject* parent = nullptr);
    // A plan from the developer link (DevLink "plan"): these lines, run in
    // the app as it is (no quit at the end: finished() instead), and only
    // the commands in `allowed` (the rest refused, logged).
    ShotPlan(QQmlApplicationEngine& qml, MobileEngine& engine, const QStringList& lines,
             const QStringList& allowed, QObject* parent = nullptr);
    void start();
    // Stops at the next line (the developer link's "planstop").
    void stop() { m_next = static_cast<int>(m_lines.size()); }

signals:
    void finished(bool failed);
    // A plan's "shot" over the developer link: the picture, grabbed as the
    // link's own screenshot is (grabWindow does not return on iOS / Metal),
    // for the link to send home; nothing is written on the device.
    void shotTaken(const QString& name, const QImage& image);

private:
    void step();
    void run(const QString& command, const QStringList& args, int* delayMs);
    void closePopups();
    void writeAudio(const QByteArray& pcm, int rate);
    void writeSpeech(const QByteArray& pcm);
    void logLayout();

    QQmlApplicationEngine& m_qml;
    MobileEngine& m_engine;
    QStringList m_lines;
    QString m_outDir;
    int m_next{0};
    int m_readyTries{0};
    bool m_failed{false};
    // Run from a file by --shot-plan: the app quits at the end.
    bool m_quitAtEnd{true};
    QStringList m_allowed;
    // Conditional lines ("? cmd" runs when the last wait succeeded, "! cmd"
    // when it did not) and the waits that poll (waittx, waitcall, waitover).
    bool m_condition{true};
    bool m_waiting{false};
    QElapsedTimer m_waitClock;
    QElapsedTimer m_growthClock;
    qsizetype m_lastTextLen{0};
    QStringList m_savedPinned;
    bool m_pinned{false};
    // record: the frame timer and the clock the frames and lines are timed by.
    QTimer* m_recordTimer{nullptr};
    QElapsedTimer m_recordClock;
    QFile* m_audioFile{nullptr};
    qint64 m_audioFrames{0}; // written, at 24 kHz
    QFile* m_speechFile{nullptr};
    qint64 m_speechFrames{0};
};

} // namespace AetherSDR::mobile
