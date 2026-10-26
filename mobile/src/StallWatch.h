#pragma once

#include <QString>

class QQuickWindow;

namespace AetherSDR::mobile {

// Logs when the GUI thread stops answering for a second or more, and when
// no frame is drawn for two seconds while it runs (a tester's tablet showed
// Android's "not responding" while PTT was held, and the system's trace
// could not say what the app was busy with). A watcher thread posts a beat
// to the GUI thread every 200 ms; frames come from the window's
// frameSwapped. With each stall the log gets what was going on: transmit,
// the window's last event and the last frame. The same line goes to a file,
// so a run Android closed during the stall reports it at the next start.
class StallWatch {
public:
    // Starts the watcher; logs and clears what the previous run left.
    static void start();
    static void stop();
    // The window whose events and frames are watched (GUI thread).
    static void watch(QQuickWindow* window);
    // Frames are expected (connected, display on screen): a pause in them
    // is logged.
    static void setExpectFrames(bool on);
    static void setTransmitting(bool on);
};

} // namespace AetherSDR::mobile
