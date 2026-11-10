#include "MidiPairing.h"

namespace AetherSDR::mobile {

// No MIDI on Android yet (RtMidi's AMIDI backend needs API 29; roadmap 54's
// second step): nothing to pair.
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

} // namespace AetherSDR::mobile
