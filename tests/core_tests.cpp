#include "blokkily/model/keyboard.hpp"
#include "blokkily/model/pattern.hpp"
#include "blokkily/model/scale.hpp"
#include "blokkily/model/tuning.hpp"
#include "blokkily/plugins/clap_catalog.hpp"
#include "blokkily/plugins/clap_instance.hpp"
#include "blokkily/instruments/soundfont_synth.hpp"
#include "blokkily/instruments/soundfont_catalog.hpp"
#include "blokkily/audio/playback.hpp"
#include "blokkily/plugins/plugin_scan.hpp"
#include "blokkily/plugins/vst3_instance.hpp"
#include "blokkily/sequencer/scheduler.hpp"
#include "blokkily/project/project.hpp"
#include "blokkily/model/song.hpp"
#include "blokkily/audio/mixer.hpp"
#include "blokkily/audio/song_engine.hpp"
#include "blokkily/audio/bounce.hpp"
#include "blokkily/audio/wave_file.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

using namespace blokkily;

namespace {
// The pitch a buffer actually sounds, read back by normalised autocorrelation.
// Retuning is only proved by what came out of the instrument, so every tuning
// test measures the audio rather than the event that asked for it.
double estimate_frequency(const std::vector<float>& samples, double sample_rate,
                          double expected_hertz) {
    if (samples.size() < 4096 || expected_hertz <= 0.0) return 0.0;
    // Measure the steady part of the note, past any attack.
    const std::size_t start = samples.size() / 2;
    const std::size_t window = samples.size() - start;
    const double expected_lag = sample_rate / expected_hertz;
    // A band of five semitones either way: wide enough that a retune of a
    // quarter tone is found rather than assumed, narrow enough that a harmonic
    // or a subharmonic cannot be mistaken for the note.
    const auto shortest = static_cast<std::size_t>(std::max(2.0, expected_lag * 0.75));
    const auto longest = std::min(static_cast<std::size_t>(expected_lag * 1.34), window / 2);
    if (longest <= shortest) return 0.0;
    std::vector<double> correlation(longest + 2, 0.0);
    for (std::size_t lag = shortest; lag <= longest; ++lag) {
        double product = 0.0;
        double left_energy = 0.0;
        double right_energy = 0.0;
        for (std::size_t index = 0; index + lag < window; ++index) {
            const double a = samples[start + index];
            const double b = samples[start + index + lag];
            product += a * b;
            left_energy += a * a;
            right_energy += b * b;
        }
        const double norm = std::sqrt(left_energy * right_energy);
        correlation[lag] = norm > 0.0 ? product / norm : 0.0;
    }
    std::size_t best = shortest;
    for (std::size_t lag = shortest; lag <= longest; ++lag)
        if (correlation[lag] > correlation[best]) best = lag;
    if (correlation[best] <= 0.0) return 0.0;
    // Interpolate between neighbours, so the answer is not limited to whole
    // samples of period.
    double refined = static_cast<double>(best);
    if (best > shortest && best < longest) {
        const double before = correlation[best - 1];
        const double centre = correlation[best];
        const double after = correlation[best + 1];
        const double divisor = 2.0 * (2.0 * centre - before - after);
        if (std::abs(divisor) > 1e-12) refined += (after - before) / divisor;
    }
    return refined > 0.0 ? sample_rate / refined : 0.0;
}

// Renders one note through an instrument and reports the pitch it sounded.
double sounded_frequency(PluginInstance& instrument, std::int16_t key, double cents,
                         double sample_rate = 48000.0) {
    std::vector<float> left(32768, 0.0F);
    std::vector<float> right(32768, 0.0F);
    const std::array<PluginEvent, 2> start{
        PluginEvent{PluginEvent::Type::parameter_value, 0, 1, 1.0},   // ask for a tone
        PluginEvent{PluginEvent::Type::note_on, 0, key, 1.0, cents}};
    std::size_t rendered = 0;
    bool first = true;
    while (rendered < left.size()) {
        const std::size_t frames = std::min<std::size_t>(512, left.size() - rendered);
        const std::span<float> block_left{left.data() + rendered, frames};
        const std::span<float> block_right{right.data() + rendered, frames};
        instrument.process({block_left, block_right},
                           first ? std::span<const PluginEvent>{start}
                                 : std::span<const PluginEvent>{});
        first = false;
        rendered += frames;
    }
    // Release the note: a polyphonic instrument would otherwise still be
    // holding it when the next measurement is taken.
    const PluginEvent release{PluginEvent::Type::note_off, 0, key, 0.0, cents};
    std::vector<float> tail(4096, 0.0F);
    std::vector<float> tail_right(4096, 0.0F);
    instrument.process({tail, tail_right}, std::span{&release, 1});
    return estimate_frequency(left, sample_rate,
                              440.0 * std::pow(2.0, (key - 69 + cents / 100.0) / 12.0));
}
} // namespace

int main() {
    const auto catalog_dir = std::filesystem::temp_directory_path() /
                             "blokkily-soundfont-catalog-test";
    std::filesystem::create_directories(catalog_dir / "nested");
    const auto sf2 = catalog_dir / "Piano.SF2";
    const auto sf3 = catalog_dir / "nested" / "Strings.sf3";
    const auto ignored = catalog_dir / "readme.txt";
    std::ofstream(sf2).put('\0');
    std::ofstream(sf3).put('\0');
    std::ofstream(ignored).put('\0');
    const auto soundfonts = SoundFontCatalog::scan_paths({catalog_dir, sf2});
    assert(soundfonts.size() == 2);
    assert(std::find(soundfonts.begin(), soundfonts.end(), sf2) != soundfonts.end());
    assert(std::find(soundfonts.begin(), soundfonts.end(), sf3) != soundfonts.end());
    std::filesystem::remove(sf2);
    std::filesystem::remove(sf3);
    std::filesystem::remove(ignored);
    std::filesystem::remove(catalog_dir / "nested");
    std::filesystem::remove(catalog_dir);

    Pattern pattern(1920, 480);
    Trigger chord;
    chord.start = 240;
    chord.duration = 240;
    chord.musical_data = Chord{60, {0, 4, 7}, 0, 10};
    const auto chord_id = pattern.add(chord);

    Trigger ratchet;
    ratchet.start = 0;
    ratchet.duration = 120;
    ratchet.ratchets = 3;
    ratchet.musical_data = Note{36, 1.0F, 0.0F};
    const auto ratchet_id = pattern.add(ratchet);

    assert(pattern.events().front().id == ratchet_id);
    assert(pattern.find(chord_id) != nullptr);

    const auto notes = Scheduler{}.render_loop(pattern, 1, 42);
    assert(notes.size() == 6);
    assert(notes[0].start == 0 && notes[1].start == 40 && notes[2].start == 80);
    assert(notes[3].key == 60 && notes[4].key == 64 && notes[5].key == 67);
    assert(notes[3].start == 240 && notes[4].start == 250 && notes[5].start == 260);

    Trigger invalid;
    invalid.probability = 2.0F;
    bool threw = false;
    try { (void)pattern.add(invalid); } catch (const std::invalid_argument&) { threw = true; }
    assert(threw);

    assert(pattern.remove(chord_id));
    assert(!pattern.remove(chord_id));

    const auto scan = ClapCatalog{}.scan_file(BLOKKILY_TEST_CLAP_PATH);
    assert(scan.failures.empty());
    assert(scan.plugins.size() == 1);
    assert(scan.plugins.front().id == "dev.blokkily.test");
    assert(scan.plugins.front().features.size() == 2);

    std::string clap_error;
    auto clap = ClapPluginInstance::create(
        BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test", &clap_error);
    assert(clap != nullptr && clap_error.empty());
    assert(clap->activate(48000.0, 1, 256));
    std::vector<float> clap_left(128, -1.0F), clap_right(128, -1.0F);
    const PluginEvent clap_note{PluginEvent::Type::note_on, 64, 60, 1.0};
    clap->process({clap_left, clap_right}, std::span{&clap_note, 1});
    assert(clap_left[63] == 0.0F && clap_left[64] == 0.25F);
    const PluginEvent clap_changes[]{
        {PluginEvent::Type::parameter_value, 0, 0, 0.4},
        {PluginEvent::Type::parameter_modulation, 0, 0, 0.1}};
    clap->process({clap_left, clap_right}, clap_changes);
    assert(clap_left.front() > 0.49F && clap_left.front() < 0.51F);
    const auto clap_state = clap->save_state();
    assert(!clap_state.empty() && clap->load_state(clap_state));

    auto transport_clap = ClapPluginInstance::create(
        BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test", &clap_error);
    RealtimePlayback playback(std::move(transport_clap));
    Pattern playback_pattern(1920, 480);
    Trigger playback_note;
    playback_note.start = 120;
    playback_note.duration = 120;
    playback_note.musical_data = Note{60, 1.0F, 0.0F};
    (void)playback_pattern.add(playback_note);
    assert(playback.prepare(playback_pattern, 120.0, 48000.0, 16384));
    const auto transport_state = playback.save_instrument_state();
    assert(!transport_state.empty());
    assert(playback.load_instrument_state(transport_state));
    std::vector<float> transport_left(16384), transport_right(16384);
    playback.set_playing(true);
    playback.process({transport_left, transport_right});
    assert(transport_left[5999] == 0.0F);
    assert(transport_left[6000] != 0.0F);
    assert(transport_left[11999] != 0.0F);
    assert(transport_left[12000] == 0.0F);
    playback.set_playing(false);
    playback.process({transport_left, transport_right});
    assert(transport_left[6000] == 0.0F);

    // The bundle path arrives unnormalised, with `.` and `..` segments, exactly
    // as a host resolving a binary's enclosing bundle would produce it.
    const std::filesystem::path vst_bundle =
        std::filesystem::path(BLOKKILY_TEST_VST3_PATH) / "." / "Contents" / "..";
    const auto vst_descriptors = Vst3PluginInstance::scan(vst_bundle);
    assert(vst_descriptors.size() == 1);
    assert(Vst3PluginInstance::scan_paths({BLOKKILY_TEST_VST3_PATH}).size() == 1);
    assert(vst_descriptors.front().name == "Blokkily Test VST3");
    assert(vst_descriptors.front().manufacturer == "Blokkily");
    assert(!vst_descriptors.front().identifier.empty());
    assert(!vst_descriptors.front().bundle.empty());
    assert(vst_descriptors.front().index == 0);
    assert(Vst3PluginInstance::scan(vst_bundle / "Contents" / "Resources").empty());
    std::string vst_error;
    auto vst = Vst3PluginInstance::create(vst_bundle, 0, &vst_error);
    assert(vst != nullptr && vst_error.empty());
    assert(vst->activate(48000.0, 1, 256));
    std::vector<float> vst_left(128), vst_right(128);
    const PluginEvent vst_note{PluginEvent::Type::note_on, 32, 60, 1.0};
    vst->process({vst_left, vst_right}, std::span{&vst_note, 1});
    assert(vst_left[31] == 0.0F && vst_left[32] != 0.0F);
    const auto vst_state = vst->save_state();
    assert(!vst_state.empty() && vst->load_state(vst_state));

    // --- Parameter locks reach the plugins --------------------------------
    // A lock on a step must change what that step sounds like: the scheduler
    // must emit it, the transport must place it before the note, and both
    // plugin adapters must apply it.
    Pattern locked_pattern(1920, 480);
    Trigger locked_step;
    locked_step.start = 120;
    locked_step.duration = 120;
    locked_step.musical_data = Note{60, 1.0F, 0.0F};
    locked_step.locks = {{"level", 0, 0.75, ParameterLock::Kind::automation}};
    const auto locked_id = locked_pattern.add(locked_step);

    const auto compiled = Scheduler{}.render(locked_pattern, 1, 0);
    assert(compiled.notes.size() == 1);
    assert(compiled.parameters.size() == 1);
    assert(compiled.parameters.front().source_id == locked_id);
    assert(compiled.parameters.front().start == 120);
    assert(compiled.parameters.front().index == 0);
    assert(compiled.parameters.front().value == 0.75);
    assert(compiled.parameters.front().kind == ParameterLock::Kind::automation);

    // A step silenced by its loop condition takes its locks with it, so a
    // suppressed step never leaves a stray parameter change behind.
    Pattern conditional_pattern(1920, 480);
    Trigger conditional_step = locked_step;
    conditional_step.play_on_loop = 2;
    (void)conditional_pattern.add(conditional_step);
    assert(Scheduler{}.render(conditional_pattern, 2, 0).parameters.empty());
    assert(Scheduler{}.render(conditional_pattern, 1, 0).parameters.size() == 1);

    // Through the production transport into a real CLAP instrument: the note
    // at tick 120 lands on sample 6000 and must sound at the locked level.
    auto locked_clap = ClapPluginInstance::create(
        BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test", &clap_error);
    RealtimePlayback locked_playback(std::move(locked_clap));
    assert(locked_playback.prepare(locked_pattern, 120.0, 48000.0, 16384));
    std::vector<float> locked_left(16384), locked_right(16384);
    locked_playback.set_playing(true);
    locked_playback.process({locked_left, locked_right});
    assert(locked_left[5999] == 0.0F);
    assert(locked_left[6000] == 0.75F);
    assert(locked_left[11999] == 0.75F);

    // The same lock expressed as modulation stays a distinct event type and
    // rides on top of the automated value rather than replacing it.
    Pattern modulated_pattern(1920, 480);
    Trigger modulated_step = locked_step;
    modulated_step.locks = {{"level", 0, 0.5, ParameterLock::Kind::automation},
                            {"level", 0, 0.25, ParameterLock::Kind::modulation}};
    (void)modulated_pattern.add(modulated_step);
    const auto modulated = Scheduler{}.render(modulated_pattern, 1, 0);
    assert(modulated.parameters.size() == 2);
    assert(modulated.parameters[0].kind == ParameterLock::Kind::automation);
    assert(modulated.parameters[1].kind == ParameterLock::Kind::modulation);
    auto modulated_clap = ClapPluginInstance::create(
        BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test", &clap_error);
    RealtimePlayback modulated_playback(std::move(modulated_clap));
    assert(modulated_playback.prepare(modulated_pattern, 120.0, 48000.0, 16384));
    std::vector<float> modulated_left(16384), modulated_right(16384);
    modulated_playback.set_playing(true);
    modulated_playback.process({modulated_left, modulated_right});
    assert(modulated_left[6000] > 0.749F && modulated_left[6000] < 0.751F);

    // The VST3 boundary applies automation sample-accurately too: the level
    // changes exactly at sample 32, not at the start of the block.
    auto vst_parameters = Vst3PluginInstance::create(vst_bundle, 0, &vst_error);
    assert(vst_parameters != nullptr && vst_parameters->activate(48000.0, 1, 256));
    std::vector<float> parameter_left(128, -1.0F), parameter_right(128, -1.0F);
    const PluginEvent vst_events[]{
        {PluginEvent::Type::note_on, 0, 60, 1.0},
        {PluginEvent::Type::parameter_value, 32, 0, 0.75}};
    vst_parameters->process({parameter_left, parameter_right}, vst_events);
    assert(parameter_left[0] == 0.25F);
    assert(parameter_left[31] == 0.25F);
    assert(parameter_left[32] == 0.75F);
    assert(parameter_left[127] == 0.75F);

    // Modulation is additive on the VST3 side as well, and a later automation
    // event must not discard the modulation that is still in effect. VST3
    // quantises parameter values at the format boundary, so these compare with
    // a tolerance far tighter than the 0.15 the modulation contributes.
    const auto near = [](float actual, float expected) {
        const float difference = actual > expected ? actual - expected : expected - actual;
        return difference < 0.006F;
    };
    std::vector<float> vst_modulated(128, -1.0F), vst_modulated_right(128, -1.0F);
    const PluginEvent vst_modulation[]{
        {PluginEvent::Type::parameter_modulation, 0, 0, 0.15},
        {PluginEvent::Type::parameter_value, 64, 0, 0.5}};
    vst_parameters->process({vst_modulated, vst_modulated_right}, vst_modulation);
    assert(near(vst_modulated[0], 0.90F));   // automation 0.75 + modulation 0.15
    assert(near(vst_modulated[63], 0.90F));
    assert(near(vst_modulated[64], 0.65F));  // automation 0.50, modulation still 0.15
    assert(near(vst_modulated[127], 0.65F));

    // --- Project persistence ---------------------------------------------
    // A project carries the full richness of the canonical model plus the
    // opaque state of every instrument, and must come back byte-identical.
    Project project;
    project.name = "Blokkily Demo \xE2\x88\x86 1";  // non-ASCII and spaces exercise escaping
    project.tempo = 137.5;
    project.song.patterns = {PatternSlot{"Verse", Pattern(1920, 480)},
                             PatternSlot{"Chorus", Pattern(960, 480)}};

    Trigger rich;
    rich.start = 480;
    rich.duration = 240;
    rich.micro_offset = -7;
    rich.probability = 0.375F;
    rich.ratchets = 4;
    rich.play_on_loop = 3;
    rich.musical_data = Chord{62, {0, 3, 7, 10}, 2, 15};
    rich.locks = {{"filter cutoff", 0, 0.125, ParameterLock::Kind::automation},
                  {"res/onance", 3, 0.9, ParameterLock::Kind::modulation}};
    const auto rich_id = project.pattern().add(rich);

    Trigger plain;
    plain.start = 0;
    plain.duration = 120;
    plain.musical_data = Note{48, 0.55F, 0.25F};
    const auto plain_id = project.pattern().add(plain);

    // Real CLAP state: drive the fixture to a distinctive level, then persist
    // exactly what the plugin itself wrote to its state stream.
    auto clap_source = ClapPluginInstance::create(
        BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test", &clap_error);
    assert(clap_source != nullptr && clap_source->activate(48000.0, 1, 256));
    const PluginEvent clap_level{PluginEvent::Type::parameter_value, 0, 0, 0.375};
    std::vector<float> shaping_left(16), shaping_right(16);
    clap_source->process({shaping_left, shaping_right}, std::span{&clap_level, 1});
    // Instruments belong to tracks now: a session is a mixer, not one synth.
    Track clap_track;
    clap_track.name = "CLAP lead";
    clap_track.mix = {-4.5, -0.75, false, true};
    clap_track.instrument = {"CLAP", BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test",
                             clap_source->save_state()};
    project.song.tracks = {clap_track};
    assert(!project.song.tracks.front().instrument.state.empty());

    // Real VST3 state: whatever the JUCE host adapter reports for the loaded
    // bundle, persisted verbatim through the project file.
    const auto vst_saved_state = vst->save_state();
    Track vst_track;
    vst_track.name = "VST3 pad";
    vst_track.mix = {2.0, 0.5, true, false};
    vst_track.instrument = {"VST3", vst_bundle.string(),
                            vst_descriptors.front().identifier, vst_saved_state};
    project.song.tracks.push_back(vst_track);
    project.song.master_gain_db = -1.5;
    // Two tracks, two patterns, and an arrangement that places them in time.
    project.song.clips = {{0, 0, 0, 2}, {1, 1, 3840, 1}};
    assert(!vst_saved_state.empty());

    const auto project_file =
        std::filesystem::temp_directory_path() / "blokkily-project-roundtrip.blok";
    std::string project_error;
    assert(ProjectFile::save(project, project_file, &project_error) && project_error.empty());
    auto reloaded = ProjectFile::load(project_file, &project_error);
    assert(reloaded.has_value() && project_error.empty());

    // Serialising the reloaded project reproduces the original file exactly, so
    // nothing was lost, reordered, or rounded on the way through.
    assert(ProjectFile::serialize(*reloaded) == ProjectFile::serialize(project));
    assert(reloaded->name == project.name);
    assert(reloaded->tempo == 137.5);
    assert(reloaded->pattern().length() == 1920 && reloaded->pattern().ticks_per_beat() == 480);
    assert(reloaded->pattern().events().size() == 2);
    // The whole arrangement survives, not just the pattern being edited.
    assert(reloaded->song.patterns.size() == 2);
    assert(reloaded->song.patterns[0].name == "Verse");
    assert(reloaded->song.patterns[1].name == "Chorus");
    assert(reloaded->song.patterns[1].pattern.length() == 960);
    assert(reloaded->song.master_gain_db == -1.5);
    assert(reloaded->song.tracks.size() == 2);
    assert(reloaded->song.tracks[0].name == "CLAP lead");
    assert(reloaded->song.tracks[0].mix.gain_db == -4.5);
    assert(reloaded->song.tracks[0].mix.pan == -0.75);
    assert(!reloaded->song.tracks[0].mix.mute && reloaded->song.tracks[0].mix.solo);
    assert(reloaded->song.tracks[1].name == "VST3 pad");
    assert(reloaded->song.tracks[1].mix.mute && !reloaded->song.tracks[1].mix.solo);
    assert(reloaded->song.clips.size() == 2);
    assert(reloaded->song.clips[0].pattern == 0 && reloaded->song.clips[0].repeats == 2);
    assert(reloaded->song.clips[1].track == 1 && reloaded->song.clips[1].start == 3840);
    // Two repeats of a 1920-tick pattern, then a 960-tick clip from tick 3840.
    assert(reloaded->song.length() == 4800);

    const auto* reloaded_rich = reloaded->pattern().find(rich_id);
    assert(reloaded_rich != nullptr && reloaded->pattern().find(plain_id) != nullptr);
    assert(reloaded_rich->micro_offset == -7 && reloaded_rich->ratchets == 4);
    assert(reloaded_rich->play_on_loop == 3 && reloaded_rich->probability == 0.375F);
    const auto& reloaded_chord = std::get<Chord>(reloaded_rich->musical_data);
    assert(reloaded_chord.root == 62 && reloaded_chord.inversion == 2);
    assert(reloaded_chord.strum == 15);
    assert((reloaded_chord.intervals == std::vector<std::int16_t>{0, 3, 7, 10}));
    assert(reloaded_rich->locks.size() == 2);
    assert(reloaded_rich->locks[0].parameter_id == "filter cutoff");
    assert(reloaded_rich->locks[0].value == 0.125);
    assert(reloaded_rich->locks[0].parameter_index == 0);
    assert(reloaded_rich->locks[0].kind == ParameterLock::Kind::automation);
    assert(reloaded_rich->locks[1].parameter_id == "res/onance");
    assert(reloaded_rich->locks[1].parameter_index == 3);
    assert(reloaded_rich->locks[1].kind == ParameterLock::Kind::modulation);
    // Adding to a reloaded pattern must not collide with restored identifiers.
    Trigger appended;
    appended.start = 960;
    const auto appended_id = reloaded->pattern().add(appended);
    assert(appended_id > rich_id && appended_id > plain_id);

    // The reloaded CLAP slot rebuilds a plugin that behaves as it did on save.
    const auto& clap_slot = reloaded->song.tracks.front().instrument;
    assert(clap_slot.format == "CLAP" && clap_slot.identifier == "dev.blokkily.test");
    assert(clap_slot.state == project.song.tracks.front().instrument.state);
    auto clap_restored = ClapPluginInstance::create(
        clap_slot.path, clap_slot.identifier, &clap_error);
    assert(clap_restored != nullptr && clap_restored->load_state(clap_slot.state));
    assert(clap_restored->activate(48000.0, 1, 256));
    std::vector<float> restored_left(64, -1.0F), restored_right(64, -1.0F);
    const PluginEvent restored_note{PluginEvent::Type::note_on, 0, 60, 1.0};
    clap_restored->process({restored_left, restored_right}, std::span{&restored_note, 1});
    assert(restored_left.front() == 0.375F && restored_right.front() == 0.375F);

    // The reloaded VST3 slot does the same through the JUCE host adapter.
    const auto& vst_slot = reloaded->song.tracks.back().instrument;
    assert(vst_slot.format == "VST3" && !vst_slot.identifier.empty());
    // Base64 through the file must not perturb a single byte of plugin state.
    assert(vst_slot.state == vst_saved_state);
    auto vst_restored = Vst3PluginInstance::create(vst_slot.path, 0, &vst_error);
    assert(vst_restored != nullptr && vst_restored->load_state(vst_slot.state));
    assert(vst_restored->activate(48000.0, 1, 256));
    std::vector<float> vst_restored_left(64, -1.0F), vst_restored_right(64, -1.0F);
    vst_restored->process({vst_restored_left, vst_restored_right}, std::span{&restored_note, 1});
    assert(vst_restored_left.front() != 0.0F);
    assert(vst_restored_left.front() == vst_restored_right.front());

    // Every base64 padding class (0, 1, and 2 '=' characters) must survive the
    // file unchanged, so state blobs of any length round-trip byte for byte.
    for (std::size_t length = 0; length <= 12; ++length) {
        Project sized;
        std::vector<std::byte> blob(length);
        for (std::size_t index = 0; index < length; ++index)
            blob[index] = static_cast<std::byte>(index * 37 + 5);
        sized.song.tracks.front().instrument = {"CLAP", "/tmp/x.clap", "id", blob};
        const auto back = ProjectFile::parse(ProjectFile::serialize(sized), &project_error);
        assert(back.has_value() && back->song.tracks.size() == 1);
        assert(back->song.tracks.front().instrument.state == blob);
    }

    // Malformed projects are rejected with a reason rather than half-loaded.
    static constexpr const char* valid_prologue =
        "blokkily-project 4\npattern Verse 1920 480\ntrack T 0 0 0 0 CLAP /a.clap ~ ~\n";
    for (const char* broken : {"", "not-a-project 1\n",
                               "blokkily-project 99\npattern Verse 1920 480\n",
                               "blokkily-project 2\npattern Verse 1920 480\n",  // superseded
                               "blokkily-project 4\n",                    // no pattern record
                               "blokkily-project 4\npattern Verse 1920 480\n",  // no track record
                               "blokkily-project 4\npattern Verse 1920 480\nnonsense 1\n",
                               "blokkily-project 4\npattern 1920 480\n",  // v2 pattern record
                               "blokkily-project 4\npattern Verse 0 480\n",
                               // a lock, a trigger, or a clip pointing at nothing
                               "blokkily-project 4\npattern Verse 1920 480\n"
                               "lock 0 7 cutoff 0 automation 0.5\n",
                               "blokkily-project 4\npattern Verse 1920 480\n"
                               "trigger 0 1 0 120 0 1 1 0 note 60 1 0 0\n"
                               "lock 0 1 cutoff 0 sideways 0.5\n",
                               "blokkily-project 4\npattern Verse 1920 480\n"
                               "trigger 4 1 0 120 0 1 1 0 note 60 1 0 0\n",
                               "blokkily-project 4\npattern Verse 1920 480\n"
                               "track T 0 0 0 0 CLAP /a.clap ~ ~\nclip 0 9 0 1\n",
                               "blokkily-project 4\npattern Verse 1920 480\n"
                               "track T 0 0 0 0 CLAP /a.clap ~ ~\nclip 3 0 0 1\n",
                               "blokkily-project 4\npattern Verse 1920 480\n"
                               "track T 0 0 0 0 CLAP /a.clap ~ ~\nclip 0 0 0 0\n",
                               "blokkily-project 4\npattern Verse 1920 480\n"
                               "instrument CLAP /tmp/a.clap id AAA\n",  // superseded record
                               "blokkily-project 4\npattern Verse 1920 480\n"
                               "track T 0 0 2 0 CLAP /a.clap ~ ~\n",   // mute is a flag
                               "blokkily-project 4\npattern Verse 1920 480\n"
                               "track T 0 0 0 0 CLAP /a.clap ~ AAA\n"}) {
        project_error.clear();
        assert(!ProjectFile::parse(broken, &project_error).has_value());
        assert(!project_error.empty());
    }
    // The prologue those cases are built from must itself be accepted, so the
    // rejections above prove the defect and not a typo in the fixture.
    assert(ProjectFile::parse(valid_prologue, &project_error).has_value());

    // A session written in a tuning reloads in that tuning, and its notes keep
    // the pitch they were written at rather than the nearest key to it.
    Project tuned_project;
    tuned_project.name = "Quarter tones";
    tuned_project.song.tuning = equal_division(24, 1200.0, "24-EDO");
    tuned_project.song.scale = *scale_by_name("Maqam Rast");
    tuned_project.song.root_degree = 62;
    tuned_project.song.auto_scale = true;
    Trigger microtonal;
    microtonal.start = 0;
    microtonal.duration = 120;
    microtonal.musical_data = Note{61, 0.8F, 0.0F, -50.0};
    (void)tuned_project.song.patterns[0].pattern.add(microtonal);
    const auto tuned_reload = ProjectFile::parse(ProjectFile::serialize(tuned_project),
                                              &project_error);
    assert(tuned_reload.has_value());
    assert(tuned_reload->song.tuning.name == "24-EDO" && tuned_reload->song.tuning.divisions() == 24);
    assert(std::abs(degree_frequency(tuned_reload->song.tuning, 61) -
                    degree_frequency(tuned_project.song.tuning, 61)) < 1e-9);
    assert(tuned_reload->song.scale.name == "Maqam Rast");
    assert(tuned_reload->song.root_degree == 62 && tuned_reload->song.auto_scale);
    const auto& reloaded_note = std::get<Note>(
        tuned_reload->song.patterns[0].pattern.events().front().musical_data);
    assert(reloaded_note.key == 61 && std::abs(reloaded_note.cents + 50.0) < 1e-9);

    std::filesystem::remove(project_file);

    // --- Mixer maths ------------------------------------------------------
    assert(db_to_linear(0.0) == 1.0);
    assert(std::abs(db_to_linear(-6.0) - 0.5011872336) < 1e-9);
    assert(db_to_linear(minimum_audible_db) == 0.0);   // the fader bottom is silence
    assert(db_to_linear(-140.0) == 0.0);
    assert(std::abs(linear_to_db(1.0)) < 1e-12);
    assert(linear_to_db(0.0) == minimum_audible_db);

    // Constant-power panning: centre sits 3 dB down per side, a hard pan is
    // unity on its side and silent on the other, and the sweep holds power.
    const auto centred = strip_gain({0.0, 0.0, false, false}, false);
    assert(std::abs(centred.left - centred.right) < 1e-6F);
    assert(std::abs(centred.left - 0.70710678F) < 1e-6F);
    const auto hard_left = strip_gain({0.0, -1.0, false, false}, false);
    assert(std::abs(hard_left.left - 1.0F) < 1e-6F && std::abs(hard_left.right) < 1e-6F);
    const auto hard_right = strip_gain({0.0, 1.0, false, false}, false);
    assert(std::abs(hard_right.right - 1.0F) < 1e-6F && std::abs(hard_right.left) < 1e-6F);
    for (double pan = -1.0; pan <= 1.0001; pan += 0.125) {
        const auto swept = strip_gain({0.0, pan, false, false}, false);
        const double power = static_cast<double>(swept.left) * swept.left +
                             static_cast<double>(swept.right) * swept.right;
        assert(std::abs(power - 1.0) < 1e-6);
    }
    assert(std::abs(strip_gain({-6.0, -1.0, false, false}, false).left - 0.5011872F) < 1e-6F);

    // Mute, solo, and the interaction between them.
    assert(audible({0.0, 0.0, false, false}, false));
    assert(!audible({0.0, 0.0, true, false}, false));
    assert(strip_gain({0.0, 0.0, true, false}, false).silent());
    assert(audible({0.0, 0.0, false, true}, true));
    assert(!audible({0.0, 0.0, false, false}, true));  // another track is soloed
    assert(!audible({0.0, 0.0, true, true}, true));    // mute still wins on its own strip

    // --- Arrangement ------------------------------------------------------
    Song song;
    song.patterns = {PatternSlot{"Drums", Pattern(1920, 480)},
                     PatternSlot{"Bass", Pattern(1920, 480)}};
    song.tracks = {Track{"Drums", {}, {}}, Track{"Bass", {}, {}}};

    Trigger kick;
    kick.start = 0;
    kick.duration = 120;
    kick.musical_data = Note{36, 1.0F, 0.0F};
    (void)song.patterns[0].pattern.add(kick);
    Trigger fill;
    fill.start = 960;
    fill.duration = 120;
    fill.play_on_loop = 2;   // every other time round the pattern
    fill.musical_data = Note{38, 1.0F, 0.0F};
    (void)song.patterns[0].pattern.add(fill);
    Trigger bass_note;
    bass_note.start = 240;
    bass_note.duration = 240;
    bass_note.musical_data = Note{48, 0.8F, 0.0F};
    (void)song.patterns[1].pattern.add(bass_note);

    song.clips = {{0, 0, 0, 2}, {1, 1, 3840, 1}};
    assert(song.consistent());
    assert(song.length() == 5760);   // two repeats, then a clip from bar 3

    const auto drum_timeline = song.arrange(0);
    // Repeat one plays kick and fill; repeat two plays the kick only, because
    // each repetition counts as the next loop of the pattern.
    assert(drum_timeline.notes.size() == 3);
    assert(drum_timeline.notes[0].start == 0 && drum_timeline.notes[0].key == 36);
    assert(drum_timeline.notes[1].start == 960 && drum_timeline.notes[1].key == 38);
    assert(drum_timeline.notes[2].start == 1920 && drum_timeline.notes[2].key == 36);
    const auto bass_timeline = song.arrange(1);
    assert(bass_timeline.notes.size() == 1);
    assert(bass_timeline.notes[0].start == 3840 + 240 && bass_timeline.notes[0].key == 48);
    assert(song.arrange(9).notes.empty());   // a track with no clips plays nothing

    Song broken_song = song;
    broken_song.clips.push_back({7, 0, 0, 1});
    assert(!broken_song.consistent());

    // --- The mixer, proved through rendered audio -------------------------
    // The CLAP fixture holds a steady 0.25 while a note sounds, so what comes
    // out of the bus is exactly what the strip gains say it should be.
    Song mix_song;
    mix_song.patterns = {PatternSlot{"Tone", Pattern(1920, 480)}};
    mix_song.tracks = {Track{"One", {}, {}}, Track{"Two", {}, {}}};
    Trigger held;
    held.start = 0;
    held.duration = 1920;
    held.musical_data = Note{60, 1.0F, 0.0F};
    (void)mix_song.patterns[0].pattern.add(held);
    mix_song.clips = {{0, 0, 0, 1}, {1, 0, 0, 1}};

    SongEngine engine;
    for (std::size_t track = 0; track < 2; ++track) {
        auto voice = ClapPluginInstance::create(
            BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test", &clap_error);
        assert(voice != nullptr);
        engine.set_instrument(track, std::move(voice));
    }
    std::string engine_error;
    assert(engine.prepare(mix_song, 120.0, 48000.0, 512, 0, &engine_error));
    assert(engine_error.empty());
    assert(engine.track_count() == 2 && engine.has_instrument(0) && engine.has_instrument(1));
    assert(engine.song_samples() == 96000);   // 1920 ticks at 120 BPM, 48 kHz

    std::vector<float> bus_left(512), bus_right(512);
    const auto render_block = [&] {
        std::fill(bus_left.begin(), bus_left.end(), 0.0F);
        std::fill(bus_right.begin(), bus_right.end(), 0.0F);
        engine.process({bus_left, bus_right});
    };
    constexpr float voice_level = 0.25F;
    constexpr float centre_gain = 0.70710678F;

    engine.set_playing(true);
    render_block();
    // Two centred voices at unity sum on both sides.
    assert(std::abs(bus_left[100] - 2.0F * voice_level * centre_gain) < 1e-5F);
    assert(std::abs(bus_right[100] - bus_left[100]) < 1e-6F);
    assert(std::abs(engine.track_peak(0) - voice_level * centre_gain) < 1e-5F);
    assert(std::abs(engine.master_peak() - bus_left[100]) < 1e-5F);

    // Muting a track removes it from the bus without stopping the transport.
    mix_song.tracks[1].mix.mute = true;
    engine.apply_mix(mix_song);
    render_block();
    assert(std::abs(bus_left[100] - voice_level * centre_gain) < 1e-5F);
    assert(engine.track_peak(1) == 0.0F);

    // Soloing the muted-out track flips which one is heard.
    mix_song.tracks[1].mix.mute = false;
    mix_song.tracks[1].mix.solo = true;
    engine.apply_mix(mix_song);
    render_block();
    assert(std::abs(bus_left[100] - voice_level * centre_gain) < 1e-5F);
    assert(engine.track_peak(0) == 0.0F);
    assert(std::abs(engine.track_peak(1) - voice_level * centre_gain) < 1e-5F);

    // Pan and gain land on the bus as the constant-power law describes, and the
    // master fader scales what the tracks summed to.
    mix_song.tracks[1].mix.solo = false;
    mix_song.tracks[0].mix = {0.0, -1.0, false, false};
    mix_song.tracks[1].mix = {-6.0, 1.0, false, false};
    mix_song.master_gain_db = -6.0;
    engine.apply_mix(mix_song);
    render_block();
    const float master = static_cast<float>(db_to_linear(-6.0));
    assert(std::abs(bus_left[100] - voice_level * master) < 1e-5F);
    assert(std::abs(bus_right[100] - voice_level * master * master) < 1e-5F);

    // An edit made while the song plays reaches the engine without restarting
    // it. Scenario: "An edit while the song plays is heard without restarting
    // it" — a producer writing into the tracker or the piano roll during
    // playback must hear the change on the next block, from where the song
    // already was, on the instrument that is already loaded.
    {
        Song live_song;
        live_song.patterns = {PatternSlot{"Live", Pattern(1920, 480)}};
        live_song.tracks = {Track{"One", {}, {}}};
        live_song.clips = {{0, 0, 0, 1}};
        // A note at the very end of the bar, so the bar starts silent and the
        // song still has a length to play.
        Trigger tail_note;
        tail_note.start = 1848;
        tail_note.duration = 72;
        tail_note.musical_data = Note{60, 1.0F, 0.0F};
        const auto tail_id = live_song.patterns[0].pattern.add(tail_note);

        SongEngine live;
        auto live_voice = ClapPluginInstance::create(
            BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test", &clap_error);
        assert(live_voice != nullptr);
        live.set_instrument(0, std::move(live_voice));
        std::string live_error;
        assert(live.prepare(live_song, 120.0, 48000.0, 512, 0, &live_error));
        assert(live.song_samples() == 96000);

        std::vector<float> left(512, 0.0F), right(512, 0.0F);
        const auto render = [&] {
            std::fill(left.begin(), left.end(), 0.0F);
            std::fill(right.begin(), right.end(), 0.0F);
            live.process({left, right});
            float loudest = 0.0F;
            for (const float sample : left) loudest = std::max(loudest, std::abs(sample));
            return loudest;
        };
        live.set_playing(true);
        // Play a quarter of the way into the bar. Nothing is written there yet.
        while (live.sample_position() < 24000) assert(render() == 0.0F);
        const auto played_to = live.sample_position();
        assert(played_to >= 24000 && played_to < 48000);

        // The producer writes a note halfway through the bar — sample 48000 at
        // 120 BPM and 48 kHz — while the song is running.
        Trigger written;
        written.start = 960;
        written.duration = 480;
        written.musical_data = Note{60, 1.0F, 0.0F};
        const auto written_id = live_song.patterns[0].pattern.add(written);
        assert(live.recompile(live_song, 120.0, 0, &live_error));
        assert(live_error.empty());
        // The edit does not move the playhead: the song is exactly where it was.
        assert(live.sample_position() == played_to);

        // It keeps playing silence until the note the producer just wrote.
        while (live.sample_position() < 47616) assert(render() == 0.0F);
        // And then sounds it, from the arrangement rather than from a live key.
        assert(render() > 0.0F);
        assert(live.sample_position() > 47616);

        // Erasing a step that has not been reached yet is heard as its absence.
        assert(live_song.patterns[0].pattern.remove(written_id));
        Trigger later;
        later.start = 1440;
        later.duration = 240;
        later.musical_data = Note{60, 1.0F, 0.0F};
        (void)live_song.patterns[0].pattern.add(later);
        assert(live.recompile(live_song, 120.0, 0, &live_error));
        // The note-off of what was already sounding still arrives, then the bar
        // is silent up to the step that replaced it: tick 1440 is sample 72000.
        while (live.sample_position() < 60000) (void)render();
        while (live.sample_position() < 71680) assert(render() == 0.0F);

        // Scenario: "An edit does not rebuild the audio graph". The instrument
        // is the same instance across a recompile, so a synth the producer has
        // dialled in is not reset by writing a note next to it.
        {
            const PluginEvent dialled{PluginEvent::Type::parameter_value, 0, 0, 0.375};
            // Reach the instrument directly, the way an inspector edit does.
            assert(live.play_live(0, dialled));
            (void)render();
            const auto before = live.save_track_state(0);
            assert(!before.empty());
            // The state must say something a fresh instance would not, or
            // carrying it across the recompile would prove nothing.
            auto untouched = ClapPluginInstance::create(
                BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test", &clap_error);
            assert(untouched != nullptr && untouched->save_state() != before);
            assert(live.recompile(live_song, 120.0, 0, &live_error));
            (void)render();
            assert(live.save_track_state(0) == before);
        }

        // A recompile that would need instruments the engine does not hold is
        // refused, so the caller rebuilds instead of playing the wrong graph.
        Song wider = live_song;
        wider.tracks.push_back(Track{"Two", {}, {}});
        assert(!live.recompile(wider, 120.0, 0, &live_error));
        assert(!live_error.empty());

        // A shorter arrangement is published as the length the next bounce will
        // measure, not as the one the callback is midway through.
        assert(live_song.patterns[0].pattern.remove(tail_id));
        live_song.clips = {{0, 0, 0, 1}};
        assert(live.recompile(live_song, 240.0, 0, &live_error));
        assert(live.song_samples() == 48000);   // the same bar at twice the tempo
    }

    // A host is free to ask for a bigger block than the engine was prepared
    // for. Regression: the playing path split the block only at the loop point,
    // so a longer callback wrote past the per-track buffers prepare() sized,
    // and what it rendered depended on the caller's block size.
    {
        SongEngine chunked;
        auto voice = ClapPluginInstance::create(
            BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test", &clap_error);
        assert(voice != nullptr);
        chunked.set_instrument(0, std::move(voice));
        Song one_track = mix_song;
        one_track.tracks.resize(1);
        one_track.clips = {{0, 0, 0, 1}};
        std::string chunk_error;
        assert(chunked.prepare(one_track, 120.0, 48000.0, 64, 0, &chunk_error));
        chunked.set_playing(true);

        std::vector<float> whole_left(256, 0.0F), whole_right(256, 0.0F);
        chunked.rewind();
        chunked.process({whole_left, whole_right});

        std::vector<float> piece_left(256, 0.0F), piece_right(256, 0.0F);
        chunked.rewind();
        for (std::size_t offset = 0; offset < 256; offset += 64) {
            const std::span<float> left{piece_left.data() + offset, 64};
            const std::span<float> right{piece_right.data() + offset, 64};
            chunked.process({left, right});
        }
        assert(whole_left == piece_left && whole_right == piece_right);
    }

    // --- Bouncing the arrangement to a file -------------------------------
    mix_song.tracks[0].mix = {0.0, 0.0, false, false};
    mix_song.tracks[1].mix = {0.0, 0.0, false, false};
    mix_song.master_gain_db = 0.0;
    engine.apply_mix(mix_song);

    const std::filesystem::path artifacts{BLOKKILY_TEST_ARTIFACTS};
    const auto mixdown = artifacts / "mixdown.wav";
    std::string bounce_error;
    engine.set_playing(false);
    const auto position_before_bounce = engine.sample_position();
    const auto report = bounce_song(engine, mixdown, WaveFormat::float32, 4800, &bounce_error);
    assert(report.has_value() && bounce_error.empty());
    assert(report->frames == engine.song_samples() + 4800);
    assert(report->peak > 0.3F && !report->clipped);
    // Bouncing borrows the transport and puts it back where it found it.
    assert(!engine.is_playing() && engine.sample_position() == position_before_bounce);

    auto rendered = read_wave(mixdown, &bounce_error);
    assert(rendered.has_value() && bounce_error.empty());
    assert(rendered->channels == 2 && rendered->sample_rate == 48000);
    assert(rendered->format == WaveFormat::float32);
    assert(rendered->frames == report->frames);

    // The file is the mix that was auditioned: rendering the same song through
    // the same engine again reproduces it sample for sample.
    engine.rewind();
    engine.set_playing(true);
    for (std::uint64_t frame = 0; frame < report->frames; frame += 512) {
        const auto frames = static_cast<std::size_t>(
            std::min<std::uint64_t>(512, report->frames - frame));
        const std::span<float> left{bus_left.data(), frames};
        const std::span<float> right{bus_right.data(), frames};
        engine.process({left, right});
        for (std::size_t index = 0; index < frames; ++index) {
            assert(rendered->interleaved[(frame + index) * 2] == left[index]);
            assert(rendered->interleaved[(frame + index) * 2 + 1] == right[index]);
        }
    }
    engine.set_playing(false);

    // A 16-bit bounce of the same mix quantizes rather than changing the sound.
    const auto mixdown16 = artifacts / "mixdown-16.wav";
    const auto report16 = bounce_song(engine, mixdown16, WaveFormat::pcm16, 0, &bounce_error);
    assert(report16.has_value() && report16->frames == engine.song_samples());
    auto rendered16 = read_wave(mixdown16, &bounce_error);
    assert(rendered16.has_value() && rendered16->format == WaveFormat::pcm16);
    assert(rendered16->frames == report16->frames);
    for (std::size_t index = 0; index < rendered16->interleaved.size(); ++index)
        assert(std::abs(rendered16->interleaved[index] - rendered->interleaved[index]) < 1e-4F);

    // Every supported depth survives the writer and reader unchanged in shape,
    // and a signal past full scale clamps instead of wrapping polarity.
    for (const auto depth : {WaveFormat::pcm16, WaveFormat::pcm24, WaveFormat::float32}) {
        const auto hot_file = artifacts / "wave-clamp.wav";
        WaveWriter writer;
        std::string writer_error;
        assert(writer.open(hot_file, 44100, depth, &writer_error));
        const std::vector<float> hot_left{2.0F, -2.0F, 0.5F};
        const std::vector<float> hot_right{-2.0F, 2.0F, -0.5F};
        assert(writer.write(hot_left, hot_right, &writer_error));
        assert(writer.close(&writer_error) && writer_error.empty());
        const auto hot = read_wave(hot_file, &writer_error);
        assert(hot.has_value() && hot->frames == 3 && hot->sample_rate == 44100);
        assert(hot->interleaved[0] > 0.99F && hot->interleaved[1] < -0.99F);
        assert(hot->interleaved[2] < -0.99F && hot->interleaved[3] > 0.99F);
        assert(std::abs(hot->interleaved[4] - 0.5F) < 1e-4F);
        std::filesystem::remove(hot_file);
    }
    assert(!read_wave(artifacts / "does-not-exist.wav").has_value());

    // A song nobody prepared cannot be bounced, and says why.
    SongEngine unprepared;
    bounce_error.clear();
    assert(!bounce_song(unprepared, artifacts / "never.wav", WaveFormat::float32, 0,
                        &bounce_error).has_value());
    assert(!bounce_error.empty());
    assert(!std::filesystem::exists(artifacts / "never.wav"));


    // ---- plugin discovery ------------------------------------------------
    // Enumeration is what the application itself is allowed to do: it reads
    // directory entries and loads nothing, so a plugin that hangs cannot be
    // reached from the process that owns the window.
    const auto clap_fixture = std::filesystem::path(BLOKKILY_TEST_CLAP_PATH);
    const auto candidates = enumerate_scan_candidates(
        {clap_fixture.parent_path()}, {BLOKKILY_TEST_VST3_PATH});
    assert(std::any_of(candidates.begin(), candidates.end(),
                       [&clap_fixture](const ScanCandidate& candidate) {
                           return candidate.format == "CLAP" &&
                                  candidate.path == clap_fixture;
                       }));
    // The hanging fixture sits beside the working one and is enumerated like
    // any other plugin; only the helper process ever loads it.
    assert(std::any_of(candidates.begin(), candidates.end(),
                       [](const ScanCandidate& candidate) {
                           return candidate.path.filename() == "blokkily-hang.clap";
                       }));
    assert(std::count_if(candidates.begin(), candidates.end(),
                         [](const ScanCandidate& candidate) {
                             return candidate.format == "VST3";
                         }) == 1);

    std::string scan_error;
    const auto described = scan_candidate({"CLAP", clap_fixture}, &scan_error);
    assert(scan_error.empty());
    assert(described.size() == 1);
    assert(described.front().name == "Blokkily Test Synth");
    assert(described.front().identifier == "dev.blokkily.test");
    scan_error.clear();
    assert(scan_candidate({"CLAP", clap_fixture.parent_path() / "absent.clap"},
                          &scan_error).empty());
    assert(!scan_error.empty());

    // An installation is browsed by typing a few letters of a name, so the
    // letters need only appear in order, anywhere in what the browser shows.
    assert(browser_match_score("", "Anything") == 0);
    assert(browser_match_score("bass", "Fat Bass CLAP") > 0);
    assert(browser_match_score("fbs", "Fat Bass CLAP") > 0);
    assert(browser_match_score("zq", "Fat Bass CLAP") < 0);
    // Case is not something a producer should have to get right.
    assert(browser_match_score("BASS", "Fat Bass") > 0);
    // The closest match is the one offered first: letters that start the name
    // or one of its words beat the same letters found buried in it, and a
    // short name beats a long one that merely contains it.
    assert(browser_match_score("fat", "Fat Bass") >
           browser_match_score("fat", "Soft Attack"));
    assert(browser_match_score("bass", "Bass") >
           browser_match_score("bass", "Bass Station Reissue"));
    // A filter is searched over what the row shows, so an instrument is found
    // by its maker or its format as readily as by its own name.
    assert(browser_match_score("clap", "Fat Bass Blokkily CLAP") > 0);

    // A name carrying a separator must survive the trip from the helper
    // process and back off disk as one field, not two.
    const std::vector<ScanRecord> awkward{
        {"CLAP", "Odd\tName", "Vendor\nLine", "/tmp/odd.clap", "dev.odd", 0},
        {"VST3", "Plain", "Vendor", "/tmp/plain.vst3", "abcd", 2}};
    const auto reread = read_scan_records(write_scan_records(awkward));
    assert(reread.size() == 2);
    assert(reread.front().name == "Odd\tName");
    assert(reread.front().vendor == "Vendor\nLine");
    assert(reread.back().index == 2);

    // The cache is what keeps a plugin that failed from being tried again, so
    // it must carry the failure as faithfully as the successes.
    std::vector<ScanCacheEntry> cache_entries;
    cache_entries.push_back({{"CLAP", clap_fixture}, scan_stamp(clap_fixture),
                             scan_size(clap_fixture), true, {}, described});
    cache_entries.push_back({{"CLAP", "/tmp/blokkily-hang.clap"}, 12, 34, false,
                             "scan timed out", {}});
    const auto cached = read_scan_cache(write_scan_cache(cache_entries));
    assert(cached.size() == 2);
    assert(cached.front().ok && cached.front().records.size() == 1);
    assert(cached.front().records.front().identifier == "dev.blokkily.test");
    assert(cached.front().stamp == scan_stamp(clap_fixture));
    assert(!cached.back().ok && cached.back().records.empty());
    assert(cached.back().failure == "scan timed out");
    assert(read_scan_cache("something else entirely\n").empty());

    // ---- tuning, scales, and playable surfaces ---------------------------
    const auto twelve = equal_division(12);
    assert(twelve.is_twelve_tone());
    assert(std::abs(degree_frequency(twelve, 69) - 440.0) < 1e-6);
    assert(std::abs(degree_frequency(twelve, 60) - 261.6255653) < 1e-4);
    assert(degree_name(twelve, 60) == "C4" && degree_name(twelve, 61) == "C#4");
    assert(degree_name(twelve, 48) == "C3" && degree_name(twelve, 71) == "B4");
    assert(degree_pitch(twelve, 64).key == 64 && degree_pitch(twelve, 64).cents == 0.0);

    // Quarter tones: the degree between two keys is the nearer key plus half a
    // semitone, and it must sound exactly halfway.
    const auto quarter = equal_division(24, 1200.0, "24-EDO");
    assert(!quarter.is_twelve_tone());
    const auto quarter_pitch = degree_pitch(quarter, 61);
    assert(std::abs(quarter_pitch.cents) == 50.0);
    assert(std::abs(pitch_frequency(quarter_pitch) - degree_frequency(quarter, 61)) < 1e-6);
    assert(std::abs(degree_frequency(quarter, 60) - degree_frequency(twelve, 60)) < 1e-9);
    assert(std::abs(degree_frequency(quarter, 84) - 2.0 * degree_frequency(quarter, 60)) < 1e-6);

    // Nineteen tones: an octave is nineteen degrees, and its best fifth is
    // eleven of them rather than seven.
    const auto nineteen = equal_division(19);
    assert(nineteen.divisions() == 19);
    assert(std::abs(degree_frequency(nineteen, 79) - 2.0 * degree_frequency(nineteen, 60)) < 1e-6);
    assert(degree_name(nineteen, 63) == "3\\19.4");
    assert(std::abs(degree_cents(nineteen, 71) - 694.7) < 0.5);

    // A tuning written as ratios is the same object as one written in cents.
    const auto scala = parse_tuning("! comment\n3/2\n2/1\n", "Fifth and octave");
    assert(scala.has_value());
    assert(scala->degrees.size() == 2 && std::abs(scala->degrees[1] - 701.955) < 0.01);
    assert(std::abs(scala->period_cents - 1200.0) < 1e-9);
    assert(!parse_tuning("nonsense", "bad").has_value());
    assert(tuning_by_name("31-EDO").has_value());

    const auto major = *scale_by_name("Major");
    const auto major_keys = scale_degrees(twelve, major, 60);
    assert((major_keys == std::vector<int>{60, 62, 64, 65, 67, 69, 71}));
    assert(in_scale(twelve, major, 60, 74) && !in_scale(twelve, major, 60, 73));
    assert(snap_to_scale(twelve, major, 60, 61) == 62);
    assert(snap_to_scale(twelve, major, 60, 66) == 67);
    assert(snap_to_scale(twelve, major, 60, 67) == 67);
    // The same scale, said in cents, lands on the degrees a nineteen-tone
    // instrument actually has.
    const auto nineteen_major = scale_degrees(nineteen, major, 60);
    assert(nineteen_major.size() == 7);
    assert(nineteen_major[4] == 71); // eleven degrees is 19-EDO's fifth
    assert(in_scale(nineteen, major, 60, 71 + 19));

    const auto tonic = scale_chord(twelve, major, 60, 0);
    assert((tonic.degrees == std::vector<int>{60, 64, 67}));
    assert(tonic.name == "I" && tonic.quality == "major");
    const auto supertonic = scale_chord(twelve, major, 60, 1);
    assert((supertonic.degrees == std::vector<int>{62, 65, 69}));
    assert(supertonic.name == "ii" && supertonic.quality == "minor");
    const auto dominant_seventh = scale_chord(twelve, major, 60, 4, 4);
    assert((dominant_seventh.degrees == std::vector<int>{67, 71, 74, 77}));
    assert(dominant_seventh.name == "V7");
    assert(scale_chord(twelve, major, 60, 6).quality == "diminished");
    // An inversion is the same harmony with a different note underneath.
    const auto inverted = scale_chord(twelve, major, 60, 0, 3, 1);
    assert((inverted.degrees == std::vector<int>{64, 67, 72}));

    KeyboardSpec piano;
    piano.tuning = twelve;
    piano.scale = major;
    piano.range = {"Tenor", 40, 25};
    const auto piano_cells = keyboard_cells(piano);
    assert(piano_cells.size() == 25);
    assert(piano_cells.front().degree == 40 && piano_cells.back().degree == 64);
    assert(piano_cells.front().label == "E2");
    // Black keys are where twelve tones put them; scale membership is its own
    // question, so a keyboard can show both at once.
    assert(!piano_cells[0].accidental && !piano_cells[1].accidental); // E then F
    assert(piano_cells[2].accidental && piano_cells[2].label == "F#2");
    assert(piano_cells[20].root && piano_cells[20].degree == 60);
    // In a tuning with more degrees to the octave, the same register is more
    // keys rather than a wider span.
    piano.tuning = nineteen;
    piano.scale = major;
    assert(keyboard_cells(piano).size() > 30);

    KeyboardSpec grid;
    grid.kind = KeyboardKind::isomorphic;
    grid.tuning = twelve;
    grid.scale = major;
    grid.rows = 3;
    grid.columns = 6;
    grid.range = {"Treble", 60, 25};
    grid.isomorphic = isomorphic_layouts().front();  // Wicki-Hayden
    const auto grid_cells = keyboard_cells(grid);
    assert(grid_cells.size() == 18);
    // A step right is a whole tone and a step up is a fifth, so the same chord
    // shape works from any key.
    const auto cell_at = [&grid_cells](int row, int column) {
        for (const auto& cell : grid_cells)
            if (cell.row == row && cell.column == column) return cell.degree;
        return -1;
    };
    assert(cell_at(2, 1) - cell_at(2, 0) == 2);
    assert(cell_at(1, 0) - cell_at(2, 0) == 7);
    // In nineteen tones a whole tone is three degrees and a fifth is eleven,
    // and the grid follows the tuning rather than the semitone.
    grid.tuning = nineteen;
    const auto nineteen_grid = keyboard_cells(grid);
    const auto nineteen_at = [&nineteen_grid](int row, int column) {
        for (const auto& cell : nineteen_grid)
            if (cell.row == row && cell.column == column) return cell.degree;
        return -1;
    };
    assert(nineteen_at(2, 1) - nineteen_at(2, 0) == 3);
    assert(nineteen_at(1, 0) - nineteen_at(2, 0) == 11);

    KeyboardSpec fretboard;
    fretboard.kind = KeyboardKind::fretboard;
    fretboard.tuning = twelve;
    fretboard.scale = major;
    fretboard.frets = 12;
    for (const auto& tuning : string_tunings())
        if (tuning.name == "Tenor guitar (CGDA)") fretboard.strings = tuning;
    assert(fretboard.strings.strings.size() == 4);
    const auto frets = keyboard_cells(fretboard);
    assert(frets.size() == 4 * 13);
    // The highest string is the top row, and the twelfth fret is an octave.
    assert(frets.front().degree == 57);
    assert(frets[12].degree == 69);
    assert(frets.back().degree == 48);

    KeyboardSpec theory;
    theory.kind = KeyboardKind::theoryboard;
    theory.tuning = twelve;
    theory.scale = major;
    theory.inversions = 2;
    const auto pads = keyboard_cells(theory);
    assert(pads.size() == 14);
    assert(pads.front().label == "I" && pads.front().chord.size() == 3);
    assert(pads[1].label == "ii" && pads[4].label == "V");
    assert(pads[6].label == "vii\u00b0");
    // The second row is the same harmony, inverted.
    assert(pads[7].chord.front() != pads[0].chord.front());

    // A pitch an instrument is told reads back as the degree that produced it,
    // which is how a note already in a pattern is named in the song's tuning.
    for (const auto& tuning : {twelve, quarter, nineteen})
        for (int degree = 40; degree <= 80; ++degree)
            assert(degree_for_pitch(tuning, degree_pitch(tuning, degree)) == degree);
    // Read in another tuning, a note lands on that tuning's nearest degree
    // rather than being rejected or silently moved.
    assert(degree_for_pitch(twelve, degree_pitch(quarter, 60)) == 60);
    assert(degree_name(twelve, degree_for_pitch(twelve, degree_pitch(quarter, 64))) == "D4");

    // ---- a chord is retuned voice by voice --------------------------------
    // A chord of a microtonal scale must sound the chord it is, so each voice
    // carries its own retune from the key its instrument is told.
    {
        Pattern chord_pattern(1920, 480);
        Trigger chord_trigger;
        chord_trigger.start = 0;
        chord_trigger.duration = 240;
        Chord quarter_chord;
        quarter_chord.root = 60;
        quarter_chord.intervals = {0, 4, 7};
        quarter_chord.cents = {0.0, -50.0, 0.0};
        chord_trigger.musical_data = quarter_chord;
        (void)chord_pattern.add(chord_trigger);
        const auto voices = Scheduler{}.render_loop(chord_pattern, 1, 0);
        assert(voices.size() == 3);
        assert(voices[0].key == 60 && voices[0].cents == 0.0);
        assert(voices[1].key == 64 && voices[1].cents == -50.0);
        assert(voices[2].key == 67 && voices[2].cents == 0.0);

        Project chord_project;
        chord_project.song.patterns[0].pattern = chord_pattern;
        std::string chord_error;
        const auto chord_back =
            ProjectFile::parse(ProjectFile::serialize(chord_project), &chord_error);
        assert(chord_back.has_value());
        const auto& reloaded_chord = std::get<Chord>(
            chord_back->song.patterns[0].pattern.events().front().musical_data);
        assert(reloaded_chord.intervals == quarter_chord.intervals);
        assert(reloaded_chord.cents == quarter_chord.cents);
    }

    // ---- microtonal pitch reaches the instruments ------------------------
    // A retuned note must sound retuned. Each format is told in its own
    // dialect, so each one is measured from the audio it produced.
    std::string tone_error;
    auto clap_tone = ClapPluginInstance::create(BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test",
                                                &tone_error);
    assert(clap_tone && clap_tone->activate(48000.0, 1, 512));
    const double clap_concert = sounded_frequency(*clap_tone, 69, 0.0);
    assert(std::abs(clap_concert - 440.0) < 4.0);
    const double clap_quarter = sounded_frequency(*clap_tone, 69, 50.0);
    // A quarter tone above A440 is 452.89 Hz, and nothing else is.
    assert(std::abs(clap_quarter - 452.89) < 5.0);
    const double clap_flat = sounded_frequency(*clap_tone, 69, -31.174);
    assert(std::abs(clap_flat - 432.0) < 4.0);

    auto vst_tone = Vst3PluginInstance::create(BLOKKILY_TEST_VST3_PATH, 0, &tone_error);
    assert(vst_tone && vst_tone->activate(48000.0, 1, 512));
    const double vst_concert = sounded_frequency(*vst_tone, 69, 0.0);
    assert(std::abs(vst_concert - 440.0) < 4.0);
    const double vst_quarter = sounded_frequency(*vst_tone, 69, 50.0);
    assert(std::abs(vst_quarter - 452.89) < 5.0);

    // ---- the song plays what the scale says ------------------------------
    // A microtonal note is a note, not an annotation: the arrangement carries
    // its tuning into the engine, and the engine into the instrument.
    Song tuned_song;
    tuned_song.tracks = {Track{}};
    tuned_song.patterns = {PatternSlot{}};
    tuned_song.clips = {{0, 0, 0, 1}};
    Trigger tuned_note;
    tuned_note.start = 0;
    tuned_note.duration = 1920;
    // A quarter tone above middle C: a pitch that lies between two keys and so
    // cannot be reached by rounding to either of them.
    const auto tuned_pitch = degree_pitch(quarter, 61);
    tuned_note.musical_data = Note{tuned_pitch.key, 1.0F, 0.0F, tuned_pitch.cents};
    (void)tuned_song.patterns[0].pattern.add(tuned_note);

    SongEngine tuned_engine;
    {
        auto voice = ClapPluginInstance::create(BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test",
                                                &clap_error);
        assert(voice != nullptr);
        tuned_engine.set_instrument(0, std::move(voice));
    }
    std::string tuned_error;
    assert(tuned_engine.prepare(tuned_song, 120.0, 48000.0, 512, 0, &tuned_error));
    // Ask the instrument for a tone before the note starts, so what is measured
    // is pitch rather than a steady level.
    assert(tuned_engine.play_live(0, {PluginEvent::Type::parameter_value, 0, 1, 1.0}));
    tuned_engine.set_playing(true);
    std::vector<float> tuned_left(16384, 0.0F);
    std::vector<float> tuned_right(16384, 0.0F);
    for (std::size_t rendered = 0; rendered < tuned_left.size(); rendered += 512)
        tuned_engine.process({std::span<float>{tuned_left.data() + rendered, 512},
                              std::span<float>{tuned_right.data() + rendered, 512}});
    const double sung = estimate_frequency(tuned_left, 48000.0, degree_frequency(quarter, 61));
    assert(std::abs(sung - degree_frequency(quarter, 61)) < 3.0);
    // Neither of the keys it sits between, so the tuning cannot have been
    // rounded away somewhere along the path.
    assert(std::abs(sung - degree_frequency(twelve, 60)) > 5.0);
    assert(std::abs(sung - degree_frequency(twelve, 61)) > 5.0);

    // ---- a keyboard sounds on a stopped song ------------------------------
    // Pressing a key must play, whether or not the transport is running, and
    // must not disturb where the song is.
    tuned_engine.set_playing(false);
    tuned_engine.rewind();
    // Release what the arrangement left sounding, so what follows measures the
    // keyboard rather than the tail of the song.
    assert(tuned_engine.play_live(0, {PluginEvent::Type::note_off, 0, tuned_pitch.key, 0.0}));
    std::vector<float> settle_left(512, 0.0F);
    std::vector<float> settle_right(512, 0.0F);
    tuned_engine.process({settle_left, settle_right});
    std::vector<float> idle_left(4096, 0.0F);
    std::vector<float> idle_right(4096, 0.0F);
    for (std::size_t rendered = 0; rendered < idle_left.size(); rendered += 512)
        tuned_engine.process({std::span<float>{idle_left.data() + rendered, 512},
                              std::span<float>{idle_right.data() + rendered, 512}});
    // Stopped and silent until a key is pressed.
    assert(std::all_of(idle_left.begin(), idle_left.end(),
                       [](float sample) { return sample == 0.0F; }));

    assert(tuned_engine.play_live(0, {PluginEvent::Type::note_on, 0, 69, 1.0, 50.0}));
    std::vector<float> live_left(16384, 0.0F);
    std::vector<float> live_right(16384, 0.0F);
    for (std::size_t rendered = 0; rendered < live_left.size(); rendered += 512)
        tuned_engine.process({std::span<float>{live_left.data() + rendered, 512},
                              std::span<float>{live_right.data() + rendered, 512}});
    assert(std::any_of(live_left.begin(), live_left.end(),
                       [](float sample) { return sample != 0.0F; }));
    // A quarter tone above A440, played from the keyboard with the song stopped.
    assert(std::abs(estimate_frequency(live_left, 48000.0, 452.89) - 452.89) < 3.0);
    assert(tuned_engine.sample_position() == 0);
    assert(tuned_engine.play_live(0, {PluginEvent::Type::note_off, 0, 69, 0.0, 50.0}));

#ifdef BLOKKILY_TEST_SF2_PATH
    SoundFontSynth synth;
    assert(synth.load(BLOKKILY_TEST_SF2_PATH));
    assert(synth.activate(48000.0, 1, 512));
    std::vector<float> left(512), right(512);
    const PluginEvent note{PluginEvent::Type::note_on, 0, 60, 1.0};
    synth.process({left, right}, std::span{&note, 1});
    float energy = 0.0F;
    for (const auto sample : left) energy += sample < 0 ? -sample : sample;
    assert(energy > 0.0F);
    const auto state = synth.save_state();
    SoundFontSynth restored;
    assert(restored.load_state(state));

    // A SoundFont is retuned by bending the voice that plays it, so a quarter
    // tone is a quarter tone here too, measured from the rendered note.
    // Each measurement gets an instrument of its own, so nothing a previous
    // note left ringing can be mistaken for the pitch under test.
    const auto sampled_pitch = [](std::int16_t key, double cents) {
        SoundFontSynth voice;
        assert(voice.load(BLOKKILY_TEST_SF2_PATH));
        assert(voice.activate(48000.0, 1, 512));
        return sounded_frequency(voice, key, cents);
    };
    const auto cents_between = [](double higher, double lower) {
        return 1200.0 * std::log2(higher / lower);
    };
    const double sampled_concert = sampled_pitch(69, 0.0);
    const double sampled_quarter = sampled_pitch(69, 50.0);
    const double sampled_semitone = sampled_pitch(69, 100.0);
    const double sampled_next_key = sampled_pitch(70, 0.0);
    assert(sampled_concert > 0.0 && sampled_quarter > 0.0);
    assert(std::abs(cents_between(sampled_concert, 440.0)) < 3.0);
    // Retuning a key by a whole semitone must sound like the next key, which is
    // the strongest statement that can be made without trusting the SoundFont's
    // own idea of pitch.
    assert(std::abs(cents_between(sampled_semitone, sampled_next_key)) < 3.0);
    // A quarter tone lands between the two. A sampler resamples to reach a
    // pitch it has no sample for, so it is held to a looser tolerance than a
    // synthesised tone, but it must be a quarter tone and not a semitone.
    const double quarter_shift = cents_between(sampled_quarter, sampled_concert);
    assert(std::abs(quarter_shift - 50.0) < 15.0);
    assert(sampled_quarter > sampled_concert && sampled_quarter < sampled_semitone);
    // Downward too, so the offset is a signed retune rather than a rise.
    assert(cents_between(sampled_pitch(69, -50.0), sampled_concert) < -35.0);
#endif
}
