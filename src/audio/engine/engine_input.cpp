#include "engine_input.hpp"

namespace blokkily::engine {

std::size_t gather_input(InputQueue* port, PerformQueue& performed,
                         std::span<RoutedEvent> into) noexcept {
    std::size_t count = 0;
    if (port != nullptr)
        while (count < into.size() && port->pop(into[count])) ++count;
    while (count < into.size() && performed.pop(into[count])) ++count;
    return count;
}

void add_input_monitoring(InputPlayback&, StereoBlock, std::uint64_t, bool) noexcept {}

} // namespace blokkily::engine
