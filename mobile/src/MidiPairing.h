#pragma once

#include <QString>

namespace AetherSDR::mobile {

// Bluetooth MIDI controllers (roadmap 54). iOS pairs them itself: its own
// pairing sheet (CoreAudioKit's CABTMIDICentralViewController) over the app,
// after which the controller is a CoreMIDI port like a USB one. A Mac pairs
// in Audio MIDI Setup, so an iPad app on a Mac does not offer it. Android
// has no pairing screen for Bluetooth LE MIDI: the app searches for them
// (15 s) and the ones found join the controller list. Not on the desktop
// build.
bool bluetoothMidiPairingAvailable();
// An iPad / iPhone app running on a Mac (Apple silicon).
bool runningOnMac();
// Shows the pairing sheet, or starts the search; false when it cannot here
// (bluetoothMidiPairingProblem says why where it can).
bool showBluetoothMidiPairing();
QString bluetoothMidiPairingProblem();
// Android's search under way.
bool bluetoothMidiSearching();
// Android: the controllers through Android's MidiManager in RtMidi's place
// (MidiPlatformPorts; RtMidi has no backend there). Elsewhere nothing.
void setupPlatformMidi();

} // namespace AetherSDR::mobile
