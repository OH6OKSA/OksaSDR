#pragma once

namespace AetherSDR::mobile {

// Bluetooth MIDI controllers (roadmap 54) are paired by the system: iOS
// shows its own pairing sheet (CoreAudioKit's CABTMIDICentralViewController)
// over the app, after which the controller is a CoreMIDI port like a USB one.
// A Mac pairs in Audio MIDI Setup, so an iPad app on a Mac does not offer it;
// neither do Android (no MIDI there yet) and the desktop build.
bool bluetoothMidiPairingAvailable();
// An iPad / iPhone app running on a Mac (Apple silicon).
bool runningOnMac();
// Shows the pairing sheet; false when it cannot be shown here.
bool showBluetoothMidiPairing();

} // namespace AetherSDR::mobile
