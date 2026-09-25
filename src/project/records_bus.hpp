#pragma once

// How the effects and automation records name a bus: `track`, `return` or
// `master`, followed by its index. Internal to src/project.

#include "blokkily/model/processor_address.hpp"

#include <optional>
#include <string_view>

namespace blokkily::project_io {

[[nodiscard]] inline std::string_view bus_kind_token(BusKind kind) {
    switch (kind) {
    case BusKind::track: return "track";
    case BusKind::ret: return "return";
    case BusKind::master: return "master";
    }
    return "track";
}

[[nodiscard]] inline std::optional<BusKind> parse_bus_kind(std::string_view token) {
    if (token == "track") return BusKind::track;
    if (token == "return") return BusKind::ret;
    if (token == "master") return BusKind::master;
    return std::nullopt;
}

} // namespace blokkily::project_io
