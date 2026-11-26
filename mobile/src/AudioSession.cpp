#include "AudioSession.h"

namespace AetherSDR::mobile {

void configureAudioSession() {}
void setAudioSessionReactivatedHandler(std::function<void()>) {}
bool audioSessionPlaybackOnly() { return false; }

} // namespace AetherSDR::mobile
