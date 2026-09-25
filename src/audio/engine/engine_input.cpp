#include "engine_input.hpp"

#include <algorithm>

namespace blokkily::engine {

namespace {
constexpr std::uint32_t monitor_bit = 1U << 24;
constexpr std::uint32_t capture_bit = 1U << 25;
} // namespace

std::uint32_t InputPlayback::pack(const AudioInputRoute& route) noexcept {
    const std::uint32_t channels = std::min<std::uint32_t>(route.channels, 2);
    return route.first_channel | (channels << 16) | (route.monitor ? monitor_bit : 0U) |
           (route.capture ? capture_bit : 0U);
}

AudioInputRoute InputPlayback::unpack(std::uint32_t word) noexcept {
    AudioInputRoute route;
    route.first_channel = static_cast<std::uint16_t>(word & 0xFFFFU);
    route.channels = static_cast<std::uint8_t>((word >> 16) & 0x3U);
    route.monitor = (word & monitor_bit) != 0;
    route.capture = (word & capture_bit) != 0;
    return route;
}

std::size_t gather_input(InputQueue* port, PerformQueue& performed,
                         std::span<RoutedEvent> into) noexcept {
    std::size_t count = 0;
    if (port != nullptr)
        while (count < into.size() && port->pop(into[count])) ++count;
    while (count < into.size() && performed.pop(into[count])) ++count;
    return count;
}

void add_input_monitoring(InputPlayback& input, StereoBlock track, const InputBlock& device,
                          SampleRing* capture, std::uint32_t track_index,
                          std::uint64_t song_position) noexcept {
    const auto route = InputPlayback::unpack(input.route.load(std::memory_order_acquire));
    if (route.channels == 0 || device.empty()) return;
    const auto frames = track.left.size();
    if (device.frames != frames ||
        static_cast<std::uint32_t>(route.first_channel) + route.channels > device.channels)
        return;
    const auto first = device.channel(route.first_channel);
    const auto second = route.channels == 2 ? device.channel(route.first_channel + 1U) : first;
    // The raw input, as it arrived: what a take records is never what the
    // track's inserts or strip made of it.
    if (capture != nullptr && route.capture)
        (void)capture->push(track_index, song_position, first,
                            route.channels == 2 ? second : std::span<const float>{});
    if (!route.monitor) return;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        track.left[frame] += first[frame];
        track.right[frame] += second[frame];
    }
}

} // namespace blokkily::engine
