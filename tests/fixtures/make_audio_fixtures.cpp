// Writes the audio files the tests decode. The WAV files are assembled byte by
// byte here, not through libsndfile, so the decoder under test never reads
// back what its own library wrote; FLAC and AIFF go through libsndfile because
// writing a FLAC encoder by hand would prove nothing more. The content of every
// file is defined in audio_fixture_content.hpp.
//
// Usage: blokkily_make_audio_fixtures <output-directory>

#include "audio_fixture_content.hpp"

#include <sndfile.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace fx = blokkily::audio_fixtures;

namespace {

using Bytes = std::vector<std::uint8_t>;

void put16(Bytes& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
}

void put24(Bytes& out, std::int32_t value) {
    const auto bits = static_cast<std::uint32_t>(value);
    out.push_back(static_cast<std::uint8_t>(bits & 0xFF));
    out.push_back(static_cast<std::uint8_t>((bits >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((bits >> 16) & 0xFF));
}

void put32(Bytes& out, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFF));
    }
}

void put_float(Bytes& out, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    put32(out, bits);
}

void put_tag(Bytes& out, const char* tag) { out.insert(out.end(), tag, tag + 4); }

void put_chunk(Bytes& out, const char* tag, const Bytes& body) {
    put_tag(out, tag);
    put32(out, static_cast<std::uint32_t>(body.size()));
    out.insert(out.end(), body.begin(), body.end());
    if (body.size() % 2 != 0) out.push_back(0);
}

enum class Tag : std::uint16_t { pcm = 1, ieee_float = 3, extensible = 0xFFFE };

struct WaveSpec {
    Tag tag = Tag::pcm;
    std::uint16_t channels = 1;
    std::uint32_t rate = 44100;
    std::uint16_t bits = 16;
    Bytes data;
    std::vector<std::pair<const char*, Bytes>> extra_chunks;   // written before "data"
    std::uint32_t declared_data_size = 0;                      // 0 = the real size
};

Bytes wave(const WaveSpec& spec) {
    const std::uint16_t block_align = static_cast<std::uint16_t>(spec.channels * spec.bits / 8);
    Bytes format;
    put16(format, static_cast<std::uint16_t>(spec.tag));
    put16(format, spec.channels);
    put32(format, spec.rate);
    put32(format, spec.rate * block_align);
    put16(format, block_align);
    put16(format, spec.bits);
    if (spec.tag == Tag::extensible) {
        put16(format, 22);               // cbSize
        put16(format, spec.bits);        // valid bits per sample
        put32(format, spec.channels == 2 ? 0x3U : 0x4U);   // front L|R, or centre
        // KSDATAFORMAT_SUBTYPE_IEEE_FLOAT 00000003-0000-0010-8000-00aa00389b71
        const std::array<std::uint8_t, 16> guid{0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
                                                0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
        format.insert(format.end(), guid.begin(), guid.end());
    }

    Bytes body;
    put_tag(body, "WAVE");
    put_chunk(body, "fmt ", format);
    if (spec.tag != Tag::pcm) {
        Bytes fact;
        put32(fact, static_cast<std::uint32_t>(spec.data.size() / block_align));
        put_chunk(body, "fact", fact);
    }
    for (const auto& [tag, chunk] : spec.extra_chunks) put_chunk(body, tag, chunk);
    put_tag(body, "data");
    put32(body, spec.declared_data_size != 0 ? spec.declared_data_size
                                             : static_cast<std::uint32_t>(spec.data.size()));
    body.insert(body.end(), spec.data.begin(), spec.data.end());

    Bytes file;
    put_tag(file, "RIFF");
    const std::uint32_t riff_size =
        spec.declared_data_size != 0
            ? static_cast<std::uint32_t>(body.size() - spec.data.size()) + spec.declared_data_size
            : static_cast<std::uint32_t>(body.size());
    put32(file, riff_size);
    file.insert(file.end(), body.begin(), body.end());
    return file;
}

bool save(const std::filesystem::path& file, const Bytes& bytes) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        std::cerr << "cannot write " << file << '\n';
        return false;
    }
    return true;
}

bool save_with_sndfile(const std::filesystem::path& file, int format, int channels,
                       std::uint32_t rate, const std::vector<short>& interleaved) {
    SF_INFO info{};
    info.samplerate = static_cast<int>(rate);
    info.channels = channels;
    info.format = format;
    SNDFILE* sound = sf_open(file.c_str(), SFM_WRITE, &info);
    if (sound == nullptr) {
        std::cerr << "cannot write " << file << ": " << sf_strerror(nullptr) << '\n';
        return false;
    }
    const auto frames = static_cast<sf_count_t>(interleaved.size() / static_cast<std::size_t>(channels));
    const bool written = sf_writef_short(sound, interleaved.data(), frames) == frames;
    sf_close(sound);
    if (!written) std::cerr << "short write to " << file << '\n';
    return written;
}

Bytes sine1k_pcm16(std::uint64_t frames) {
    Bytes data;
    for (std::uint64_t frame = 0; frame < frames; ++frame) {
        put16(data, static_cast<std::uint16_t>(fx::sine1k(frame)));
    }
    return data;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: blokkily_make_audio_fixtures <output-directory>\n";
        return 2;
    }
    const std::filesystem::path dir = argv[1];
    std::error_code failure;
    std::filesystem::create_directories(dir, failure);
    if (failure) {
        std::cerr << "cannot create " << dir << ": " << failure.message() << '\n';
        return 1;
    }
    bool ok = true;

    const Bytes sine_wave = wave({Tag::pcm, 1, fx::sine1k_rate, 16, sine1k_pcm16(fx::sine1k_frames), {}, 0});
    ok &= save(dir / "sine1k_44k1_pcm16.wav", sine_wave);

    {
        WaveSpec spec{Tag::ieee_float, 1, fx::step_rate, 32, {}, {}, 0};
        for (std::uint64_t frame = 0; frame < fx::step_frames; ++frame) put_float(spec.data, fx::step(frame));
        ok &= save(dir / "step_44k1_float.wav", wave(spec));
    }
    {
        WaveSpec spec{Tag::pcm, 2, fx::stereo24_rate, 24, {}, {}, 0};
        for (std::uint64_t frame = 0; frame < fx::stereo24_frames; ++frame) {
            put24(spec.data, fx::stereo24(frame, 0));
            put24(spec.data, fx::stereo24(frame, 1));
        }
        ok &= save(dir / "stereo_48k_pcm24.wav", wave(spec));
    }
    {
        WaveSpec spec{Tag::extensible, 2, fx::extensible_rate, 32, {}, {}, 0};
        for (std::uint64_t frame = 0; frame < fx::extensible_frames; ++frame) {
            put_float(spec.data, fx::extensible(frame, 0));
            put_float(spec.data, fx::extensible(frame, 1));
        }
        ok &= save(dir / "extensible_float_48k_stereo.wav", wave(spec));
    }
    {
        WaveSpec spec{Tag::pcm, 1, fx::smpl_rate, 16, {}, {}, 0};
        for (std::uint64_t frame = 0; frame < fx::smpl_frames; ++frame) {
            put16(spec.data, static_cast<std::uint16_t>(fx::smpl_sample(frame)));
        }
        Bytes smpl;
        put32(smpl, 0);                                   // manufacturer
        put32(smpl, 0);                                   // product
        put32(smpl, 1000000000U / fx::smpl_rate);         // sample period, ns
        put32(smpl, static_cast<std::uint32_t>(fx::smpl_root_key));
        put32(smpl, 0);                                   // pitch fraction
        put32(smpl, 0);                                   // SMPTE format
        put32(smpl, 0);                                   // SMPTE offset
        put32(smpl, 1);                                   // loops
        put32(smpl, 0);                                   // sampler data
        put32(smpl, 0);                                   // cue point id
        put32(smpl, 0);                                   // type: forward
        put32(smpl, fx::smpl_loop_first);
        put32(smpl, fx::smpl_loop_last);                  // inclusive
        put32(smpl, 0);                                   // fraction
        put32(smpl, 0);                                   // play count: forever
        spec.extra_chunks.emplace_back("smpl", smpl);
        ok &= save(dir / "smpl_loop_44k1_pcm16.wav", wave(spec));
    }
    {
        WaveSpec spec{Tag::pcm, 1, fx::hits8_rate, 16, {}, {}, 0};
        for (std::uint64_t frame = 0; frame < fx::hits8_frames; ++frame) {
            put16(spec.data, static_cast<std::uint16_t>(fx::hits8(frame)));
        }
        ok &= save(dir / "hits8_48k_pcm16.wav", wave(spec));
    }
    {
        WaveSpec spec{Tag::pcm, 1, fx::tones8_rate, 16, {}, {}, 0};
        for (std::uint64_t frame = 0; frame < fx::tones8_frames; ++frame) {
            put16(spec.data, static_cast<std::uint16_t>(fx::tones8(frame)));
        }
        ok &= save(dir / "tones8_48k_pcm16.wav", wave(spec));
    }
    // A forward sustain loop over loop480's frames, as a `smpl` chunk.
    const auto with_loop480_smpl = [](WaveSpec& spec) {
        Bytes smpl;
        put32(smpl, 0);                                   // manufacturer
        put32(smpl, 0);                                   // product
        put32(smpl, 1000000000U / fx::loop480_rate);      // sample period, ns
        put32(smpl, static_cast<std::uint32_t>(fx::loop480_root_key));
        for (int field = 0; field < 3; ++field) put32(smpl, 0);   // fraction, SMPTE
        put32(smpl, 1);                                   // loops
        put32(smpl, 0);                                   // sampler data
        put32(smpl, 0);                                   // cue point id
        put32(smpl, 0);                                   // type: forward
        put32(smpl, fx::loop480_loop_first);
        put32(smpl, fx::loop480_loop_last);               // inclusive
        put32(smpl, 0);                                   // fraction
        put32(smpl, 0);                                   // play count: forever
        spec.extra_chunks.emplace_back("smpl", smpl);
    };
    {
        WaveSpec spec{Tag::pcm, 1, fx::loop480_rate, 16, {}, {}, 0};
        for (std::uint64_t frame = 0; frame < fx::loop480_frames; ++frame) {
            put16(spec.data, static_cast<std::uint16_t>(fx::loop480(frame)));
        }
        with_loop480_smpl(spec);
        ok &= save(dir / "loop480_48k_pcm16.wav", wave(spec));
    }
    {
        WaveSpec spec{Tag::pcm, 1, fx::loop480_rate, 16, {}, {}, 0};
        for (std::uint64_t frame = 0; frame < fx::loop480_frames; ++frame) {
            put16(spec.data, static_cast<std::uint16_t>(fx::loopseam(frame)));
        }
        with_loop480_smpl(spec);
        ok &= save(dir / "loopseam_48k_pcm16.wav", wave(spec));
    }

    // Broken files: each must be refused with a reason, never half-played.
    {
        WaveSpec spec{Tag::pcm, 1, fx::sine1k_rate, 16, sine1k_pcm16(fx::truncated_frames_present), {},
                      static_cast<std::uint32_t>(fx::sine1k_frames * 2)};
        ok &= save(dir / "truncated_data.wav", wave(spec));
        ok &= save(dir / "truncated_header.wav", Bytes(sine_wave.begin(), sine_wave.begin() + 30));
        ok &= save(dir / "empty.wav", Bytes{});
        Bytes garbage;
        std::uint32_t state = 0x1234567U;
        for (int i = 0; i < 4096; ++i) {
            state = state * 1664525U + 1013904223U;
            garbage.push_back(static_cast<std::uint8_t>(state >> 24));
        }
        ok &= save(dir / "garbage.wav", garbage);
    }

    {
        std::vector<short> mono;
        std::vector<short> stereo;
        for (std::uint64_t frame = 0; frame < fx::sine1k_frames; ++frame) {
            const short sample = fx::sine1k(frame);
            mono.push_back(sample);
            stereo.push_back(sample);
            stereo.push_back(static_cast<short>(-sample));
        }
        ok &= save_with_sndfile(dir / "sine1k_44k1.flac", SF_FORMAT_FLAC | SF_FORMAT_PCM_16, 1,
                                fx::sine1k_rate, mono);
        ok &= save_with_sndfile(dir / "sine1k_44k1_stereo.aiff", SF_FORMAT_AIFF | SF_FORMAT_PCM_16, 2,
                                fx::sine1k_rate, stereo);
        // The same AIFF cut off halfway through its sample data.
        std::ifstream in(dir / "sine1k_44k1_stereo.aiff", std::ios::binary);
        const Bytes whole{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        ok &= !whole.empty() && save(dir / "truncated_data.aiff", Bytes(whole.begin(), whole.begin() + static_cast<std::ptrdiff_t>(whole.size() / 2)));
    }
    return ok ? 0 : 1;
}
