#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace blokkily {

// What a file's header says about the audio inside it. `channels` is the
// file's own channel count, even when decoding keeps only the first two.
struct AudioFileInfo {
    std::uint64_t frames = 0;
    std::uint32_t sample_rate = 0;
    std::uint16_t channels = 0;

    friend bool operator==(const AudioFileInfo&, const AudioFileInfo&) = default;
};

// A sustain loop in frames: `start` is the first frame inside the loop and
// `end` is one past the last, so `end - start` is the loop length.
struct LoopPoints {
    std::uint64_t start = 0;
    std::uint64_t end = 0;

    friend bool operator==(const LoopPoints&, const LoopPoints&) = default;
};

// A whole file decoded into normalised floats at its own sample rate. Mono
// files leave `right` empty; files with more than two channels keep channels 0
// and 1. The loop and root key come from the file's sampler metadata (the WAV
// `smpl` chunk, the AIFF `INST` chunk) when it has any.
struct DecodedAudio {
    AudioFileInfo info;
    std::vector<float> left;
    std::vector<float> right;
    std::optional<LoopPoints> loop;
    std::optional<int> root_key;
};

// Reads only the header. Control thread; performs file I/O.
[[nodiscard]] std::optional<AudioFileInfo> probe_audio_file(const std::filesystem::path& file,
                                                            std::string* error = nullptr);

// Decodes WAV (PCM, float, EXTENSIBLE), FLAC, AIFF, and whatever else the
// installed libsndfile reads (OGG, MP3). A file that cannot be opened, is not
// audio, or ends before the frames its header promises is an error, never a
// short buffer. Control thread; performs file I/O and allocates.
[[nodiscard]] std::optional<DecodedAudio> decode_audio_file(const std::filesystem::path& file,
                                                            std::string* error = nullptr);

// Converts one channel by `ratio` (output rate / input rate) with the best
// sinc converter. The result always has round(input.size() * ratio) frames
// and is aligned with the input: frame n of the output sits at input time
// n / ratio. Deterministic. Control thread only: it allocates.
[[nodiscard]] std::vector<float> resample(std::span<const float> input, double ratio);

} // namespace blokkily
