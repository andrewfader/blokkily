#pragma once

// A fixed-capacity sample delay: the building block of the built-in delay
// and, later, of plugin delay compensation (plan 2.4). The buffer is sized on
// the control thread by resize(); everything else is real-time safe (no
// allocation, no locking) and runs on the audio thread.

#include <cstddef>
#include <vector>

namespace blokkily {

class DelayLine {
public:
    // Control thread. Makes room for delays of up to `max_delay` samples and
    // clears the line. Allocates.
    void resize(std::size_t max_delay) {
        std::size_t capacity = 1;
        while (capacity < max_delay + 1) capacity <<= 1U;
        buffer_.assign(capacity, 0.0F);
        mask_ = capacity - 1;
        max_delay_ = max_delay;
        write_ = 0;
    }

    // Forgets everything written, keeping the capacity.
    void clear() noexcept {
        for (float& sample : buffer_) sample = 0.0F;
        write_ = 0;
    }

    [[nodiscard]] std::size_t max_delay() const noexcept { return max_delay_; }

    // The sample written `delay` writes ago, 1 <= delay <= max_delay() (the
    // delay is clamped into that range). Silence before anything was written.
    [[nodiscard]] float read(std::size_t delay) const noexcept {
        if (buffer_.empty()) return 0.0F;
        if (delay < 1) delay = 1;
        if (delay > max_delay_) delay = max_delay_;
        return buffer_[(write_ - delay) & mask_];
    }

    void write(float sample) noexcept {
        if (buffer_.empty()) return;
        buffer_[write_ & mask_] = sample;
        ++write_;
    }

    // A fixed delay: returns what went in `delay` samples ago and stores
    // `input`. A delay of 0 returns `input` itself.
    float process(float input, std::size_t delay) noexcept {
        const float output = delay == 0 ? input : read(delay);
        write(input);
        return output;
    }

private:
    std::vector<float> buffer_;
    std::size_t mask_ = 0;
    std::size_t max_delay_ = 0;
    std::size_t write_ = 0;
};

} // namespace blokkily
