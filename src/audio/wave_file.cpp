#include "blokkily/audio/wave_file.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>

namespace blokkily {
namespace {

constexpr std::uint16_t channels = 2;

std::uint16_t bits_per_sample(WaveFormat format) {
    switch (format) {
    case WaveFormat::pcm16: return 16;
    case WaveFormat::pcm24: return 24;
    case WaveFormat::float32: return 32;
    }
    return 32;
}

std::uint16_t format_tag(WaveFormat format) {
    return format == WaveFormat::float32 ? 3 /* IEEE float */ : 1 /* PCM */;
}

void put32(std::ostream& out, std::uint32_t value) {
    const std::array<char, 4> bytes{static_cast<char>(value & 0xFF),
                                    static_cast<char>((value >> 8) & 0xFF),
                                    static_cast<char>((value >> 16) & 0xFF),
                                    static_cast<char>((value >> 24) & 0xFF)};
    out.write(bytes.data(), bytes.size());
}

void put16(std::ostream& out, std::uint16_t value) {
    const std::array<char, 2> bytes{static_cast<char>(value & 0xFF),
                                    static_cast<char>((value >> 8) & 0xFF)};
    out.write(bytes.data(), bytes.size());
}

std::uint32_t read32(const std::vector<char>& data, std::size_t offset) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(data[offset])) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(data[offset + 1])) << 8) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(data[offset + 2])) << 16) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(data[offset + 3])) << 24);
}

std::uint16_t read16(const std::vector<char>& data, std::size_t offset) {
    return static_cast<std::uint16_t>(
        static_cast<unsigned char>(data[offset]) |
        (static_cast<unsigned char>(data[offset + 1]) << 8));
}

bool fail(std::string* error, const char* message) {
    if (error != nullptr) *error = message;
    return false;
}

// Rounds to nearest and clamps to the format's range, so a hot mix distorts at
// full scale instead of wrapping to the opposite polarity.
std::int32_t quantize(float sample, std::int32_t peak) {
    const double scaled = std::lround(static_cast<double>(sample) * (peak + 1));
    return static_cast<std::int32_t>(std::clamp<double>(scaled, -peak - 1, peak));
}

} // namespace

struct WaveWriter::Impl {
    std::ofstream out;
    WaveFormat format = WaveFormat::float32;
    std::uint32_t sample_rate = 48000;
};

WaveWriter::WaveWriter() : impl_(std::make_unique<Impl>()) {}
WaveWriter::~WaveWriter() { (void)close(); }

bool WaveWriter::open(const std::filesystem::path& file, std::uint32_t sample_rate,
                      WaveFormat format, std::string* error) {
    if (sample_rate == 0) return fail(error, "sample rate must be positive");
    std::error_code failure;
    const auto parent = file.parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, failure);
    impl_->out.open(file, std::ios::binary | std::ios::trunc);
    if (!impl_->out) return fail(error, "cannot open wave file for writing");
    impl_->format = format;
    impl_->sample_rate = sample_rate;
    frames_ = 0;

    const std::uint16_t bits = bits_per_sample(format);
    const std::uint16_t block_align = static_cast<std::uint16_t>(channels * bits / 8);
    impl_->out.write("RIFF", 4);
    put32(impl_->out, 0); // patched on close
    impl_->out.write("WAVEfmt ", 8);
    put32(impl_->out, 16);
    put16(impl_->out, format_tag(format));
    put16(impl_->out, channels);
    put32(impl_->out, sample_rate);
    put32(impl_->out, sample_rate * block_align);
    put16(impl_->out, block_align);
    put16(impl_->out, bits);
    impl_->out.write("data", 4);
    put32(impl_->out, 0); // patched on close
    return static_cast<bool>(impl_->out);
}

bool WaveWriter::write(std::span<const float> left, std::span<const float> right,
                       std::string* error) {
    if (!impl_->out.is_open()) return fail(error, "wave file is not open");
    if (left.size() != right.size()) return fail(error, "channel lengths differ");
    for (std::size_t frame = 0; frame < left.size(); ++frame) {
        for (const float sample : {left[frame], right[frame]}) {
            switch (impl_->format) {
            case WaveFormat::float32: {
                std::array<char, 4> bytes{};
                std::memcpy(bytes.data(), &sample, sizeof(sample));
                impl_->out.write(bytes.data(), bytes.size());
                break;
            }
            case WaveFormat::pcm16:
                put16(impl_->out, static_cast<std::uint16_t>(quantize(sample, 32767)));
                break;
            case WaveFormat::pcm24: {
                const auto value = static_cast<std::uint32_t>(quantize(sample, 8388607));
                const std::array<char, 3> bytes{static_cast<char>(value & 0xFF),
                                                static_cast<char>((value >> 8) & 0xFF),
                                                static_cast<char>((value >> 16) & 0xFF)};
                impl_->out.write(bytes.data(), bytes.size());
                break;
            }
            }
        }
    }
    frames_ += left.size();
    if (!impl_->out) return fail(error, "failed while writing wave data");
    return true;
}

bool WaveWriter::close(std::string* error) {
    if (!impl_ || !impl_->out.is_open()) return true;
    const std::uint16_t bits = bits_per_sample(impl_->format);
    const auto data_bytes = static_cast<std::uint32_t>(frames_ * channels * (bits / 8));
    impl_->out.seekp(4);
    put32(impl_->out, 36 + data_bytes);
    impl_->out.seekp(40);
    put32(impl_->out, data_bytes);
    const bool valid = static_cast<bool>(impl_->out);
    impl_->out.close();
    if (!valid) return fail(error, "failed while finishing wave file");
    return true;
}

std::optional<WaveData> read_wave(const std::filesystem::path& file, std::string* error) {
    std::ifstream in(file, std::ios::binary);
    if (!in) { (void)fail(error, "cannot open wave file for reading"); return std::nullopt; }
    const std::vector<char> bytes((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());
    if (bytes.size() < 44 || std::string_view(bytes.data(), 4) != "RIFF" ||
        std::string_view(bytes.data() + 8, 4) != "WAVE") {
        (void)fail(error, "not a wave file");
        return std::nullopt;
    }
    WaveData data;
    bool have_format = false;
    std::size_t cursor = 12;
    while (cursor + 8 <= bytes.size()) {
        const std::string_view id(bytes.data() + cursor, 4);
        const auto size = read32(bytes, cursor + 4);
        const std::size_t body = cursor + 8;
        if (body + size > bytes.size()) { (void)fail(error, "truncated chunk"); return std::nullopt; }
        if (id == "fmt " && size >= 16) {
            const auto tag = read16(bytes, body);
            data.channels = read16(bytes, body + 2);
            data.sample_rate = read32(bytes, body + 4);
            const auto bits = read16(bytes, body + 14);
            if (tag == 3 && bits == 32) data.format = WaveFormat::float32;
            else if (tag == 1 && bits == 16) data.format = WaveFormat::pcm16;
            else if (tag == 1 && bits == 24) data.format = WaveFormat::pcm24;
            else { (void)fail(error, "unsupported wave encoding"); return std::nullopt; }
            have_format = true;
        } else if (id == "data") {
            if (!have_format) { (void)fail(error, "data before format"); return std::nullopt; }
            const std::size_t stride = data.format == WaveFormat::pcm16 ? 2
                                     : data.format == WaveFormat::pcm24 ? 3 : 4;
            const std::size_t samples = size / stride;
            data.interleaved.reserve(samples);
            for (std::size_t index = 0; index < samples; ++index) {
                const std::size_t at = body + index * stride;
                switch (data.format) {
                case WaveFormat::float32: {
                    float value = 0.0F;
                    std::memcpy(&value, bytes.data() + at, sizeof(value));
                    data.interleaved.push_back(value);
                    break;
                }
                case WaveFormat::pcm16:
                    data.interleaved.push_back(
                        static_cast<float>(static_cast<std::int16_t>(read16(bytes, at))) / 32768.0F);
                    break;
                case WaveFormat::pcm24: {
                    std::int32_t value =
                        static_cast<unsigned char>(bytes[at]) |
                        (static_cast<unsigned char>(bytes[at + 1]) << 8) |
                        (static_cast<unsigned char>(bytes[at + 2]) << 16);
                    if ((value & 0x800000) != 0) value |= ~0xFFFFFF; // sign extend
                    data.interleaved.push_back(static_cast<float>(value) / 8388608.0F);
                    break;
                }
                }
            }
            data.frames = data.channels == 0 ? 0 : samples / data.channels;
        }
        cursor = body + size + (size % 2); // chunks are word aligned
    }
    if (!have_format) { (void)fail(error, "wave file has no format chunk"); return std::nullopt; }
    return data;
}

} // namespace blokkily
