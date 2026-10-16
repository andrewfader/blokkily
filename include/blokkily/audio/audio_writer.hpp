#pragma once

#include "blokkily/audio/wave_file.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace blokkily {

// The kind of file an export is written as. WAV keeps its bit depth
// (WaveFormat); MP3 and Ogg Vorbis are encoded by libsndfile at a high VBR
// quality, so the depth means nothing to them.
enum class ExportContainer { wav, mp3, ogg };

// The container a path's extension names (.mp3, .ogg/.oga), WAV otherwise.
[[nodiscard]] ExportContainer container_for_path(const std::filesystem::path& file);
// The extension, with its dot, a container's files are written with.
[[nodiscard]] const char* container_extension(ExportContainer container);
// "WAV", "MP3" or "OGG", and back. Unknown names are WAV.
[[nodiscard]] const char* container_name(ExportContainer container);
[[nodiscard]] ExportContainer container_from_name(std::string_view name);

// A streaming stereo writer for any export container. WAV goes through
// WaveWriter, byte for byte as before; the lossy containers through
// libsndfile, which clips rather than wraps a mix over full scale.
class AudioFileWriter {
public:
    AudioFileWriter();
    ~AudioFileWriter();
    AudioFileWriter(const AudioFileWriter&) = delete;
    AudioFileWriter& operator=(const AudioFileWriter&) = delete;

    [[nodiscard]] bool open(const std::filesystem::path& file, std::uint32_t sample_rate,
                            ExportContainer container, WaveFormat depth,
                            std::string* error = nullptr);
    [[nodiscard]] bool write(std::span<const float> left, std::span<const float> right,
                             std::string* error = nullptr);
    [[nodiscard]] bool close(std::string* error = nullptr);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace blokkily
