#pragma once

// A number read from text the same way everywhere: the whole text and nothing
// else, no locale, no leading space or '+', no hex. That is std::from_chars's
// rule, and std::from_chars does the reading where the C++ library has it for
// floating point. The C++ library of older Xcode releases (16.x, which Intel
// macOS runners build with) declares it deleted for floating point, so there
// strtod_l in the C locale reads the number under the same rule.

#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>

#if defined(__APPLE__)
#include <xlocale.h>
#endif

namespace blokkily {

template <typename Number>
[[nodiscard]] bool read_number_text(std::string_view text, Number& value) {
    if constexpr (std::is_floating_point_v<Number>) {
#if defined(__APPLE__)
        if (text.empty() || text.front() == '+' || text.front() == ' ' || text.front() == '\t' ||
            text.find_first_of("xX \t\n\r") != std::string_view::npos)
            return false;
        const std::string copy(text);
        char* end = nullptr;
        errno = 0;
        Number parsed{};
        if constexpr (std::is_same_v<Number, float>)
            parsed = strtof_l(copy.c_str(), &end, LC_C_LOCALE);
        else
            parsed = static_cast<Number>(strtod_l(copy.c_str(), &end, LC_C_LOCALE));
        if (end != copy.c_str() + copy.size() || errno == ERANGE) return false;
        value = parsed;
        return true;
#else
        const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
        return result.ec == std::errc() && result.ptr == text.data() + text.size();
#endif
    } else {
        const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
        return result.ec == std::errc() && result.ptr == text.data() + text.size();
    }
}

} // namespace blokkily
