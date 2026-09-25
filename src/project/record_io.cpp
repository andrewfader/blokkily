#include "record_io.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace blokkily::project_io {

namespace {

constexpr char empty_marker = '~';
constexpr std::string_view base64_alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

bool is_plain(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
           c == '.' || c == '_' || c == '-' || c == '/';
}

} // namespace

std::string escape(std::string_view text) {
    if (text.empty()) return std::string(1, empty_marker);
    std::string out;
    out.reserve(text.size());
    for (const unsigned char c : text) {
        if (is_plain(static_cast<char>(c))) {
            out.push_back(static_cast<char>(c));
        } else {
            std::array<char, 4> buffer{};
            std::snprintf(buffer.data(), buffer.size(), "%%%02X", c);
            out.append(buffer.data(), 3);
        }
    }
    return out;
}

std::optional<std::string> unescape(std::string_view token) {
    if (token.size() == 1 && token.front() == empty_marker) return std::string{};
    std::string out;
    out.reserve(token.size());
    for (std::size_t index = 0; index < token.size(); ++index) {
        if (token[index] != '%') { out.push_back(token[index]); continue; }
        if (index + 2 >= token.size()) return std::nullopt;
        const auto digit = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        const int high = digit(token[index + 1]);
        const int low = digit(token[index + 2]);
        if (high < 0 || low < 0) return std::nullopt;
        out.push_back(static_cast<char>(high * 16 + low));
        index += 2;
    }
    return out;
}

std::string encode_base64(std::span<const std::byte> data) {
    if (data.empty()) return std::string(1, empty_marker);
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    for (std::size_t index = 0; index < data.size(); index += 3) {
        const std::size_t remaining = data.size() - index;
        const auto byte = [&](std::size_t offset) {
            return offset < remaining ? static_cast<unsigned>(data[index + offset]) : 0U;
        };
        const unsigned triple = (byte(0) << 16) | (byte(1) << 8) | byte(2);
        out.push_back(base64_alphabet[(triple >> 18) & 0x3F]);
        out.push_back(base64_alphabet[(triple >> 12) & 0x3F]);
        out.push_back(remaining > 1 ? base64_alphabet[(triple >> 6) & 0x3F] : '=');
        out.push_back(remaining > 2 ? base64_alphabet[triple & 0x3F] : '=');
    }
    return out;
}

std::optional<std::vector<std::byte>> decode_base64(std::string_view text) {
    if (text.size() == 1 && text.front() == empty_marker) return std::vector<std::byte>{};
    if (text.empty() || text.size() % 4 != 0) return std::nullopt;

    std::size_t padding = 0;
    while (padding < 2 && text[text.size() - 1 - padding] == '=') ++padding;
    const std::string_view body = text.substr(0, text.size() - padding);
    if (body.find('=') != std::string_view::npos) return std::nullopt;

    std::vector<std::byte> out;
    out.reserve(text.size() / 4 * 3);
    for (std::size_t index = 0; index < text.size(); index += 4) {
        unsigned triple = 0;
        const std::size_t available = std::min<std::size_t>(4, body.size() - index);
        for (std::size_t offset = 0; offset < available; ++offset) {
            const auto position = base64_alphabet.find(body[index + offset]);
            if (position == std::string_view::npos) return std::nullopt;
            triple |= static_cast<unsigned>(position) << (18 - 6 * offset);
        }
        // A group of n encoded characters carries n - 1 bytes.
        for (std::size_t byte = 0; byte + 1 < available; ++byte)
            out.push_back(static_cast<std::byte>((triple >> (16 - 8 * byte)) & 0xFF));
    }
    return out;
}

std::string number(double value) {
    std::array<char, 32> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%.17g", value);
    return buffer.data();
}

std::string number(float value) {
    std::array<char, 32> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%.9g", static_cast<double>(value));
    return buffer.data();
}

std::optional<long long> Fields::integer(std::size_t index) const {
    if (index >= tokens.size()) return std::nullopt;
    char* end = nullptr;
    const long long value = std::strtoll(tokens[index].c_str(), &end, 10);
    if (end == tokens[index].c_str() || *end != '\0') return std::nullopt;
    return value;
}

std::optional<double> Fields::real(std::size_t index) const {
    if (index >= tokens.size()) return std::nullopt;
    char* end = nullptr;
    const double value = std::strtod(tokens[index].c_str(), &end);
    if (end == tokens[index].c_str() || *end != '\0') return std::nullopt;
    return value;
}

std::optional<std::string> Fields::text(std::size_t index) const {
    if (index >= tokens.size()) return std::nullopt;
    return unescape(tokens[index]);
}

Fields split(const std::string& line) {
    Fields fields;
    std::istringstream stream(line);
    std::string token;
    while (stream >> token) fields.tokens.push_back(token);
    return fields;
}

bool fail(std::string* error, std::string message) {
    if (error) *error = std::move(message);
    return false;
}

} // namespace blokkily::project_io
