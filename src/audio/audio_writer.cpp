#include "blokkily/audio/audio_writer.hpp"
#include "blokkily/audio/sndfile_path.hpp"

#include <sndfile.h>

#include <algorithm>
#include <cctype>
#include <vector>

namespace blokkily {
namespace {

bool fail(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
    return false;
}

std::string lower(std::string text) {
    std::ranges::transform(text, text.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

} // namespace

ExportContainer container_for_path(const std::filesystem::path& file) {
    const auto extension = lower(file.extension().string());
    if (extension == ".mp3") return ExportContainer::mp3;
    if (extension == ".ogg" || extension == ".oga") return ExportContainer::ogg;
    return ExportContainer::wav;
}

const char* container_extension(ExportContainer container) {
    switch (container) {
    case ExportContainer::mp3: return ".mp3";
    case ExportContainer::ogg: return ".ogg";
    case ExportContainer::wav: break;
    }
    return ".wav";
}

const char* container_name(ExportContainer container) {
    switch (container) {
    case ExportContainer::mp3: return "MP3";
    case ExportContainer::ogg: return "OGG";
    case ExportContainer::wav: break;
    }
    return "WAV";
}

ExportContainer container_from_name(std::string_view name) {
    const auto text = lower(std::string(name));
    if (text == "mp3") return ExportContainer::mp3;
    if (text == "ogg") return ExportContainer::ogg;
    return ExportContainer::wav;
}

struct AudioFileWriter::Impl {
    WaveWriter wave;
    SNDFILE* encoded = nullptr;
    bool wave_open = false;
    std::vector<float> interleaved;
};

AudioFileWriter::AudioFileWriter() : impl_(std::make_unique<Impl>()) {}

void AudioFileWriter::set_dither(bool on) noexcept { impl_->wave.set_dither(on); }
AudioFileWriter::~AudioFileWriter() { (void)close(); }

bool AudioFileWriter::open(const std::filesystem::path& file, std::uint32_t sample_rate,
                           ExportContainer container, WaveFormat depth, std::string* error) {
    if (container == ExportContainer::wav) {
        impl_->wave_open = impl_->wave.open(file, sample_rate, depth, error);
        return impl_->wave_open;
    }
    if (sample_rate == 0) return fail(error, "sample rate must be positive");
    std::error_code failure;
    if (const auto parent = file.parent_path(); !parent.empty())
        std::filesystem::create_directories(parent, failure);
    SF_INFO info{};
    info.samplerate = static_cast<int>(sample_rate);
    info.channels = 2;
    info.format = container == ExportContainer::mp3
                      ? SF_FORMAT_MPEG | SF_FORMAT_MPEG_LAYER_III
                      : SF_FORMAT_OGG | SF_FORMAT_VORBIS;
    if (sf_format_check(&info) == 0)
        return fail(error, std::string("this libsndfile cannot write ") +
                               container_name(container));
    impl_->encoded = sf_open(sndfile_path(file).c_str(), SFM_WRITE, &info);
    if (impl_->encoded == nullptr)
        return fail(error, std::string("cannot open ") + container_name(container) +
                               " file for writing: " + sf_strerror(nullptr));
    // Quality, not size: VBR near the top of each encoder's scale (V1-ish
    // LAME, Vorbis q8). 0.0 is the best quality libsndfile offers.
    double level = 0.15;
    (void)sf_command(impl_->encoded, SFC_SET_COMPRESSION_LEVEL, &level, sizeof(level));
    if (container == ExportContainer::mp3) {
        int mode = SF_BITRATE_MODE_VARIABLE;
        (void)sf_command(impl_->encoded, SFC_SET_BITRATE_MODE, &mode, sizeof(mode));
    }
    (void)sf_command(impl_->encoded, SFC_SET_CLIPPING, nullptr, SF_TRUE);
    return true;
}

bool AudioFileWriter::write(std::span<const float> left, std::span<const float> right,
                            std::string* error) {
    if (impl_->wave_open) return impl_->wave.write(left, right, error);
    if (impl_->encoded == nullptr) return fail(error, "audio file is not open");
    if (left.size() != right.size()) return fail(error, "channel lengths differ");
    impl_->interleaved.resize(left.size() * 2);
    for (std::size_t frame = 0; frame < left.size(); ++frame) {
        // The encoders take floats as they are; clamp so a hot mix
        // distorts at full scale, as a PCM export does.
        impl_->interleaved[frame * 2] = std::clamp(left[frame], -1.0F, 1.0F);
        impl_->interleaved[frame * 2 + 1] = std::clamp(right[frame], -1.0F, 1.0F);
    }
    const auto frames = static_cast<sf_count_t>(left.size());
    if (sf_writef_float(impl_->encoded, impl_->interleaved.data(), frames) != frames)
        return fail(error, std::string("failed while encoding: ") + sf_strerror(impl_->encoded));
    return true;
}

bool AudioFileWriter::close(std::string* error) {
    if (!impl_) return true;
    if (impl_->wave_open) {
        impl_->wave_open = false;
        return impl_->wave.close(error);
    }
    if (impl_->encoded == nullptr) return true;
    const int result = sf_close(impl_->encoded);
    impl_->encoded = nullptr;
    if (result != 0) return fail(error, std::string("failed while finishing the file: ") +
                                            sf_error_number(result));
    return true;
}

} // namespace blokkily
