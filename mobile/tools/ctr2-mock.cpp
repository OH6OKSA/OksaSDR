// A stand-in for Lynovation's CTR2-MIDI on this PC (roadmap 54): an ALSA
// virtual MIDI port "CTR2-MIDI" that sends what the CTR2 sends in its
// MIDI mode (its manual): the knob as CC 100 "WheelA", 64 plus the speed
// for clockwise and 64 minus it for counter-clockwise; the buttons as notes
// 1-6 (Note On 127 when pressed, Note Off when let go), long presses as
// notes 11-16. Each argument is one step:
//   knob:+3      three clockwise detents (CC 100 = 65, three times)
//   knob:-2      two counter-clockwise detents (CC 100 = 63)
//   knob:+5x3    five detents at speed 3 (CC 100 = 67)
//   tap:4        button 4 pressed and let go
//   down:4 / up:4  button 4 pressed / let go (held between)
//   long:4       button 4 held long (note 14)
//   sleep:500    wait 500 ms
// With "-" the steps come from stdin, one a line, so a test can send each
// when the app is ready for it.
// Build: g++ -O2 -D__LINUX_ALSA__ -Ithird_party/rtmidi mobile/tools/ctr2-mock.cpp
//        third_party/rtmidi/RtMidi.cpp -lasound -lpthread -o build-mobile/ctr2-mock
#include "RtMidi.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

void send(RtMidiOut& out, unsigned char a, unsigned char b, unsigned char c)
{
    std::vector<unsigned char> m{a, b, c};
    out.sendMessage(&m);
    std::printf("sent %02X %d %d\n", a, b, c);
    std::fflush(stdout);
}

void pause(int ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

} // namespace

int main(int argc, char** argv)
{
    RtMidiOut out(RtMidi::LINUX_ALSA, "CTR2-MIDI");
    out.openVirtualPort("CTR2-MIDI");
    std::printf("port open\n");
    std::fflush(stdout);
    std::vector<std::string> steps(argv + 1, argv + argc);
    const bool fromStdin = steps.size() == 1 && steps.front() == "-";
    std::string line;
    for (std::size_t i = 0; fromStdin || i < steps.size(); ++i) {
        if (fromStdin && !std::getline(std::cin, line)) {
            break;
        }
        const std::string arg = fromStdin ? line : steps[i];
        if (arg.empty()) {
            continue;
        }
        const auto colon = arg.find(':');
        const std::string verb = arg.substr(0, colon);
        const std::string rest = colon == std::string::npos ? std::string() : arg.substr(colon + 1);
        if (verb == "knob") {
            const auto x = rest.find('x');
            const int count = std::stoi(rest.substr(0, x));
            const int speed = x == std::string::npos ? 1 : std::stoi(rest.substr(x + 1));
            for (int n = 0; n < std::abs(count); ++n) {
                send(out, 0xB0, 100, static_cast<unsigned char>(count > 0 ? 64 + speed : 64 - speed));
                pause(30);
            }
        } else if (verb == "tap" || verb == "down" || verb == "up" || verb == "long") {
            const int button = std::stoi(rest);
            const unsigned char note = static_cast<unsigned char>(verb == "long" ? 10 + button : button);
            if (verb != "up") {
                send(out, 0x90, note, 127);
            }
            if (verb == "tap" || verb == "long") {
                pause(verb == "long" ? 800 : 120);
            }
            if (verb != "down") {
                send(out, 0x80, note, 0);
            }
        } else if (verb == "sleep") {
            pause(std::stoi(rest));
        } else {
            std::fprintf(stderr, "unknown step %s\n", arg.c_str());
            return 1;
        }
    }
    std::printf("done\n");
    std::fflush(stdout);
    return 0;
}
