#include "engine_automation.hpp"

namespace blokkily::engine {

StripGain chunk_strip_gain(StripAutomation&, const ArrangementAutomation&, StripGain fixed,
                           std::uint64_t, std::size_t, bool) noexcept {
    return fixed;
}

} // namespace blokkily::engine
