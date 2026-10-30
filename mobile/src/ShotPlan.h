#pragma once

#include <QObject>
#include <QStringList>

class QQmlApplicationEngine;

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
//   scroll PX               scroll the open controls to PX
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
//   click NAME              clicked() on the visible buttons with this objectName
//   tapname NAME            a tap through the window on that control's middle
//   ready                   wait until connected with a slice (up to 30 s),
//                           then 4 s for the display
//   setup KEY=VALUE         a Radio Setup value, as the page sets it (e.g.
//                           showTxInWaterfall=0; put it back after)
//   size WxH                resize the window (a phone turning: WxH swapped)
//   tnfs                    log the notches the radio has and the frequency
//   txchain                 log this device's microphone processing (ESSB)
//   a11y on|                on: accessibility on; bare: log the screen reader's
//                           elements in view (role, name) and the unnamed buttons
//   a11ypress NAME          the named element's press action (a screen
//                           reader's double tap)
//   a11yinc NAME            its increase action (a swipe up on a slider)
//   a11ydec NAME            its decrease action (a swipe down)
//   layout                  log the page's layout decisions and the info
//                           panels' geometry (layout-diff.py compares runs)
//
// The run quits after the last command (exit status 2 if a shot failed).
class ShotPlan : public QObject {
    Q_OBJECT
public:
    ShotPlan(QQmlApplicationEngine& qml, MobileEngine& engine, const QString& planFile, const QString& outDir,
             QObject* parent = nullptr);
    void start();

private:
    void step();
    void run(const QString& command, const QStringList& args, int* delayMs);
    void closePopups();
    void logLayout();

    QQmlApplicationEngine& m_qml;
    MobileEngine& m_engine;
    QStringList m_lines;
    QString m_outDir;
    int m_next{0};
    int m_readyTries{0};
    bool m_failed{false};
    QStringList m_savedPinned;
    bool m_pinned{false};
};

} // namespace AetherSDR::mobile
