#include "MidiPairing.h"

#include "MidiFramer.h"

#include "core/MidiControlManager.h"

#include <QByteArray>
#include <QStringList>
#include <QTimer>

#include <thread>

namespace AetherSDR::mobile {

// Desktop development build: ALSA lists every MIDI port itself, nothing to
// pair here.
bool bluetoothMidiPairingAvailable()
{
    return false;
}

bool runningOnMac()
{
    return false;
}

bool showBluetoothMidiPairing()
{
    return false;
}

QString bluetoothMidiPairingProblem()
{
    return {};
}

bool bluetoothMidiSearching()
{
    return false;
}

// Development: OKSASDR_MIDI_FAKE="b0 64 41 41 ..." stands in for Android's
// MidiManager: one port, "Fake MIDI", whose bytes (hex) go through the
// framer and MidiPlatformPorts from a thread of their own 2 s after it is
// opened, as Android's receiver hands them on.
void setupPlatformMidi()
{
    const QByteArray hex = qgetenv("OKSASDR_MIDI_FAKE");
    if (hex.isEmpty()) {
        return;
    }
    static bool open = false;
    MidiPlatformPorts ports;
    ports.list = [] { return QStringList{QStringLiteral("Fake MIDI")}; };
    ports.open = [hex](const QString&, MidiControlManager* target) {
        open = true;
        QTimer::singleShot(2000, [hex, target] {
            std::thread([hex, target] {
                MidiFramer framer;
                for (const QByteArray& b : hex.split(' ')) {
                    if (!b.isEmpty()) {
                        framer.feed(static_cast<unsigned char>(b.toUInt(nullptr, 16)),
                                    [target](const unsigned char* m, int n) {
                                        MidiControlManager::deliverPlatformMessage(target, m, n);
                                    });
                        std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    }
                }
            }).detach();
        });
        return true;
    };
    ports.close = [] { open = false; };
    ports.isOpen = [] { return open; };
    MidiControlManager::setPlatformPorts(std::move(ports));
}

} // namespace AetherSDR::mobile
