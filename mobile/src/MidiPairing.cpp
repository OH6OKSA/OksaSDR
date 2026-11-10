#include "MidiPairing.h"

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

} // namespace AetherSDR::mobile
