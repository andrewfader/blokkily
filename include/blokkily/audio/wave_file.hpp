#pragma once

#include <cstdint>
#include <memory>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace blokkily {

enum class WaveFormat { pcm16, pcm24, float32 };

// A streaming RIFF/WAVE writer. Sizes are patched on close, so a bounce of any
// length can be written without knowing the frame count up front.
class WaveWriter {
public:
    WaveWriter();
    ~WaveWriter();
    WaveWriter(const WaveWriter&) = delete;
    WaveWriter& operator=(const WaveWriter&) = delete;

    [[nodiscard]] bool open(const std::filesystem::path& file, std::uint32_t sample_rate,
                            WaveFormat format, std::string* error = nullptr);
    // Interleaves and converts one stereo block. Integer formats are clamped,
    // not wrapped, so an over-hot mix distorts rather than tearing.
    [[nodiscard]] bool write(std::span<const float> left, std::span<const float> right,
                             std::string* error = nullptr);
    [[nodiscard]] bool close(std::string* error = nullptr);
    [[nodiscard]] std::uint64_t frames() const noexcept { return frames_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::uint64_t frames_ = 0;
};

struct WaveData {
    std::uint32_t sample_rate = 0;
    std::uint16_t channels = 0;
    WaveFormat format = WaveFormat::float32;
    std::uint64_t frames = 0;
    std::vector<float> interleaved;
};

// Reads a WAVE file back into normalized floats. Deliberately small: it exists
// so a bounce can be verified as audio rather than as a file that exists.
[[nodiscard]] std::optional<WaveData> read_wave(const std::filesystem::path& file,
                                                std::string* error = nullptr);

} // namespace blokkily
