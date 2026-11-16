#pragma once

namespace AetherSDR::mobile {

// Whole MIDI messages out of a byte stream as a platform hands it on
// (several messages at once or one in parts, running status): channel
// messages go on, system ones (SysEx, clock, sensing) are left out as
// RtMidi's ignoreTypes leaves them (Android's MidiManager, roadmap 54).
struct MidiFramer {
    unsigned char message[3]{};
    int have = 0;
    int need = 0;
    unsigned char running = 0;
    bool sysex = false;

    template <typename Deliver>
    void feed(unsigned char b, Deliver deliver)
    {
        if (b >= 0xF8) {
            return;   // real time, between any bytes
        }
        if (b & 0x80) {
            sysex = b == 0xF0;
            if (b >= 0xF0) {
                running = 0;
                have = 0;
                return;
            }
            running = b;
            message[0] = b;
            have = 1;
            need = (b & 0xF0) == 0xC0 || (b & 0xF0) == 0xD0 ? 2 : 3;
            return;
        }
        if (sysex || running == 0) {
            return;
        }
        if (have == 0) {
            message[0] = running;
            have = 1;
        }
        message[have++] = b;
        if (have == need) {
            deliver(message, need);
            have = 0;
        }
    }
};

} // namespace AetherSDR::mobile
