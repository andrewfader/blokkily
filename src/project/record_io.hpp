#pragma once

// Token-level helpers shared by every project record module. Internal to
// src/project: nothing outside the project file format includes this.

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace blokkily::project_io {

// Percent-escapes to a single whitespace-free token so that every record stays
// one parseable line regardless of what a plugin path or parameter id contains.
// The empty string is written as a lone '~'.
[[nodiscard]] std::string escape(std::string_view text);
[[nodiscard]] std::optional<std::string> unescape(std::string_view token);

[[nodiscard]] std::string encode_base64(std::span<const std::byte> data);
[[nodiscard]] std::optional<std::vector<std::byte>> decode_base64(std::string_view text);

// %.17g and strtod round-trip an IEEE double exactly, so tempo and parameter
// lock values survive a save/load without drifting.
[[nodiscard]] std::string number(double value);
[[nodiscard]] std::string number(float value);

// One record line split on whitespace, with typed accessors that refuse
// anything that is not wholly a number.
struct Fields {
    std::vector<std::string> tokens;

    [[nodiscard]] bool count(std::size_t expected) const { return tokens.size() == expected; }
    [[nodiscard]] bool at_least(std::size_t expected) const { return tokens.size() >= expected; }

    [[nodiscard]] std::optional<long long> integer(std::size_t index) const;
    [[nodiscard]] std::optional<double> real(std::size_t index) const;
    [[nodiscard]] std::optional<std::string> text(std::size_t index) const;
};

[[nodiscard]] Fields split(const std::string& line);

// Stores the message (when there is somewhere to put it) and returns false, so
// a handler can write `return fail(error, "...")`.
bool fail(std::string* error, std::string message);

} // namespace blokkily::project_io
