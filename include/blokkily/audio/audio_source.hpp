#pragma once

#include "blokkily/plugins/plugin.hpp"

namespace blokkily {

// Anything the hardware output can pull audio from. Pattern preview and song
// playback are both sources, so the device adapter does not need to know which
// one is running.
class AudioSource {
public:
    virtual ~AudioSource() = default;
    // Called on the audio thread. Implementations must not allocate, lock,
    // touch the filesystem, log, or call GUI APIs.
    virtual void process(StereoBlock output) noexcept = 0;
};

} // namespace blokkily
