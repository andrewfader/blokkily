#pragma once

#include "blokkily/plugins/plugin.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace blokkily {

// What the audio device's inputs delivered for one block (item 3.2): every
// input channel, non-interleaved, `frames` long, channel c starting `stride`
// floats after channel c - 1. An empty block (no channels) is a device opened
// for output only, or a render that takes no input at all: a bounce.
struct InputBlock {
    const float* data = nullptr;
    std::uint32_t channels = 0;
    std::size_t frames = 0;
    std::size_t stride = 0;

    [[nodiscard]] bool empty() const noexcept { return data == nullptr || channels == 0; }
    // One channel's frames. Only for c < channels.
    [[nodiscard]] std::span<const float> channel(std::uint32_t c) const noexcept {
        return {data + static_cast<std::size_t>(c) * stride, frames};
    }
    // `count` frames from `offset`, every channel: what one chunk of a block
    // hears. Allocates nothing.
    [[nodiscard]] InputBlock slice(std::size_t offset, std::size_t count) const noexcept {
        if (empty()) return {};
        return {data + offset, channels, count, stride};
    }
};

// Anything the hardware output can pull audio from. Pattern preview and song
// playback are both sources, so the device adapter does not need to know which
// one is running.
class AudioSource {
public:
    virtual ~AudioSource() = default;
    // Called on the audio thread. Implementations must not allocate, lock,
    // touch the filesystem, log, or call GUI APIs.
    virtual void process(StereoBlock output) noexcept = 0;
    // The same, with what the device's inputs delivered for this block. A
    // source that takes no input ignores it; the device calls this one.
    virtual void process(StereoBlock output, InputBlock /*input*/) noexcept { process(output); }
};

} // namespace blokkily
