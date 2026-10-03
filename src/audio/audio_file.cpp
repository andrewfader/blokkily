#include "blokkily/audio/audio_file.hpp"

#include "blokkily/audio/sndfile_path.hpp"

#include <samplerate.h>
#include <sndfile.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace blokkily {
namespace {

bool fail(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
    return false;
}

struct SndFile {
    SNDFILE* handle = nullptr;
    SF_INFO info{};
    SndFile() = default;
    SndFile(const SndFile&) = delete;
    SndFile& operator=(const SndFile&) = delete;
    ~SndFile() {
        if (handle != nullptr) sf_close(handle);
    }
};

// libsndfile quietly shortens a WAV or AIFF whose sample data ends before its
// header says it does, and then reports only the frames present. A cut-off
// file is refused instead, so compare the declared data size with them.
bool data_is_cut_short(SNDFILE* handle, const SF_INFO& info) {
    std::size_t sample_bytes = 0;
    switch (info.format & SF_FORMAT_SUBMASK) {
    case SF_FORMAT_PCM_S8:
    case SF_FORMAT_PCM_U8: sample_bytes = 1; break;
    case SF_FORMAT_PCM_16: sample_bytes = 2; break;
    case SF_FORMAT_PCM_24: sample_bytes = 3; break;
    case SF_FORMAT_PCM_32:
    case SF_FORMAT_FLOAT: sample_bytes = 4; break;
    case SF_FORMAT_DOUBLE: sample_bytes = 8; break;
    default: return false;
    }
    const char* chunk = nullptr;
    std::uint64_t header_bytes = 0;   // SSND starts with an offset and a block size
    switch (info.format & SF_FORMAT_TYPEMASK) {
    case SF_FORMAT_WAV:
    case SF_FORMAT_WAVEX: chunk = "data"; break;
    case SF_FORMAT_AIFF: chunk = "SSND"; header_bytes = 8; break;
    default: return false;
    }
    SF_CHUNK_INFO wanted{};
    std::memcpy(wanted.id, chunk, 4);
    wanted.id_size = 4;
    SF_CHUNK_ITERATOR* found = sf_get_chunk_iterator(handle, &wanted);
    if (found == nullptr) return false;
    SF_CHUNK_INFO size{};
    if (sf_get_chunk_size(found, &size) != SF_ERR_NO_ERROR) return false;
    const std::uint64_t present = static_cast<std::uint64_t>(info.frames) *
                                  static_cast<std::uint64_t>(info.channels) * sample_bytes;
    return size.datalen > present + header_bytes;
}

// Opens the file and checks the header describes audio this program can hold.
bool open_for_reading(const std::filesystem::path& file, SndFile& sound, std::string* error) {
    std::error_code failure;
    if (!std::filesystem::is_regular_file(file, failure)) {
        return fail(error, "not an audio file: " + file.string() + " does not exist");
    }
    sound.handle = sf_open(sndfile_path(file).c_str(), SFM_READ, &sound.info);
    if (sound.handle == nullptr) {
        return fail(error, "cannot read " + file.string() + ": " + sf_strerror(nullptr));
    }
    if (sound.info.channels <= 0 || sound.info.channels > std::numeric_limits<std::uint16_t>::max() ||
        sound.info.samplerate <= 0 || sound.info.frames < 0) {
        return fail(error, "cannot read " + file.string() + ": the header is not valid audio");
    }
    if (data_is_cut_short(sound.handle, sound.info)) {
        return fail(error, "cannot read " + file.string() +
                               ": the file ends before the audio its header declares");
    }
    return true;
}

AudioFileInfo info_of(const SF_INFO& info) {
    return AudioFileInfo{static_cast<std::uint64_t>(info.frames),
                         static_cast<std::uint32_t>(info.samplerate),
                         static_cast<std::uint16_t>(info.channels)};
}

} // namespace

std::optional<AudioFileInfo> probe_audio_file(const std::filesystem::path& file,
                                              std::string* error) {
    SndFile sound;
    if (!open_for_reading(file, sound, error)) return std::nullopt;
    return info_of(sound.info);
}

std::optional<DecodedAudio> decode_audio_file(const std::filesystem::path& file,
                                              std::string* error) {
    SndFile sound;
    if (!open_for_reading(file, sound, error)) return std::nullopt;

    DecodedAudio decoded;
    decoded.info = info_of(sound.info);
    const std::size_t channels = decoded.info.channels;
    const std::size_t frames = decoded.info.frames;
    decoded.left.resize(frames);
    if (channels >= 2) decoded.right.resize(frames);

    // Integer formats are read as floats divided by full scale (0x8000 for
    // 16-bit), which is exact, so a PCM file decodes bit-for-bit.
    sf_command(sound.handle, SFC_SET_NORM_FLOAT, nullptr, SF_TRUE);
    constexpr std::size_t block = 4096;
    std::vector<float> interleaved(block * channels);
    std::size_t done = 0;
    while (done < frames) {
        const auto wanted = static_cast<sf_count_t>(std::min(block, frames - done));
        const sf_count_t got = sf_readf_float(sound.handle, interleaved.data(), wanted);
        if (got <= 0) break;
        for (std::size_t frame = 0; frame < static_cast<std::size_t>(got); ++frame) {
            decoded.left[done + frame] = interleaved[frame * channels];
            if (channels >= 2) decoded.right[done + frame] = interleaved[frame * channels + 1];
        }
        done += static_cast<std::size_t>(got);
    }
    if (done != frames || sf_error(sound.handle) != SF_ERR_NO_ERROR) {
        fail(error, "cannot read " + file.string() + ": the file ends after " +
                        std::to_string(done) + " of its " + std::to_string(frames) + " frames");
        return std::nullopt;
    }

    SF_INSTRUMENT instrument{};
    if (sf_command(sound.handle, SFC_GET_INSTRUMENT, &instrument, sizeof instrument) == SF_TRUE) {
        if (instrument.basenote >= 0) decoded.root_key = static_cast<int>(instrument.basenote);
        if (instrument.loop_count > 0 && instrument.loops[0].mode != SF_LOOP_NONE) {
            // libsndfile reports the end one past the last looped frame.
            const LoopPoints loop{instrument.loops[0].start, instrument.loops[0].end};
            if (loop.start < loop.end && loop.end <= frames) decoded.loop = loop;
        }
    }
    return decoded;
}

std::vector<float> resample(std::span<const float> input, double ratio) {
    if (input.empty() || !(ratio > 0.0) || !std::isfinite(ratio)) return {};
    const auto expected =
        static_cast<std::size_t>(std::llround(static_cast<double>(input.size()) * ratio));
    if (ratio == 1.0) return {input.begin(), input.end()};

    int status = 0;
    SRC_STATE* state = src_new(SRC_SINC_BEST_QUALITY, 1, &status);
    if (state == nullptr) return std::vector<float>(expected, 0.0F);

    // Room for everything the converter can flush past the exact length; the
    // result is then cut or padded to exactly `expected` frames.
    std::vector<float> output(expected + 64);
    std::size_t consumed = 0;
    std::size_t produced = 0;
    for (;;) {
        if (produced == output.size()) output.resize(output.size() * 2);
        SRC_DATA data{};
        data.data_in = input.data() + consumed;
        data.input_frames = static_cast<long>(input.size() - consumed);
        data.data_out = output.data() + produced;
        data.output_frames = static_cast<long>(output.size() - produced);
        data.end_of_input = 1;
        data.src_ratio = ratio;
        if (src_process(state, &data) != 0) break;
        consumed += static_cast<std::size_t>(data.input_frames_used);
        produced += static_cast<std::size_t>(data.output_frames_gen);
        if (data.output_frames_gen == 0 && (consumed == input.size() || data.input_frames_used == 0)) {
            break;
        }
    }
    src_delete(state);
    output.resize(produced);
    output.resize(expected, 0.0F);
    return output;
}

} // namespace blokkily
