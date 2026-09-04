#include "blokkily/model/pattern.hpp"
#include "blokkily/plugins/clap_catalog.hpp"
#include "blokkily/plugins/clap_instance.hpp"
#include "blokkily/instruments/soundfont_synth.hpp"
#include "blokkily/instruments/soundfont_catalog.hpp"
#include "blokkily/audio/playback.hpp"
#include "blokkily/plugins/vst3_instance.hpp"
#include "blokkily/sequencer/scheduler.hpp"
#include "blokkily/project/project.hpp"
#include "blokkily/model/song.hpp"
#include "blokkily/audio/mixer.hpp"
#include "blokkily/audio/song_engine.hpp"
#include "blokkily/audio/bounce.hpp"
#include "blokkily/audio/wave_file.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

using namespace blokkily;

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
        "blokkily-project 3\npattern Verse 1920 480\ntrack T 0 0 0 0 CLAP /a.clap ~ ~\n";
    for (const char* broken : {"", "not-a-project 1\n",
                               "blokkily-project 99\npattern Verse 1920 480\n",
                               "blokkily-project 2\npattern Verse 1920 480\n",  // superseded
                               "blokkily-project 3\n",                    // no pattern record
                               "blokkily-project 3\npattern Verse 1920 480\n",  // no track record
                               "blokkily-project 3\npattern Verse 1920 480\nnonsense 1\n",
                               "blokkily-project 3\npattern 1920 480\n",  // v2 pattern record
                               "blokkily-project 3\npattern Verse 0 480\n",
                               // a lock, a trigger, or a clip pointing at nothing
                               "blokkily-project 3\npattern Verse 1920 480\n"
                               "lock 0 7 cutoff 0 automation 0.5\n",
                               "blokkily-project 3\npattern Verse 1920 480\n"
                               "trigger 0 1 0 120 0 1 1 0 note 60 1 0\n"
                               "lock 0 1 cutoff 0 sideways 0.5\n",
                               "blokkily-project 3\npattern Verse 1920 480\n"
                               "trigger 4 1 0 120 0 1 1 0 note 60 1 0\n",
                               "blokkily-project 3\npattern Verse 1920 480\n"
                               "track T 0 0 0 0 CLAP /a.clap ~ ~\nclip 0 9 0 1\n",
                               "blokkily-project 3\npattern Verse 1920 480\n"
                               "track T 0 0 0 0 CLAP /a.clap ~ ~\nclip 3 0 0 1\n",
                               "blokkily-project 3\npattern Verse 1920 480\n"
                               "track T 0 0 0 0 CLAP /a.clap ~ ~\nclip 0 0 0 0\n",
                               "blokkily-project 3\npattern Verse 1920 480\n"
                               "instrument CLAP /tmp/a.clap id AAA\n",  // superseded record
                               "blokkily-project 3\npattern Verse 1920 480\n"
                               "track T 0 0 2 0 CLAP /a.clap ~ ~\n",   // mute is a flag
                               "blokkily-project 3\npattern Verse 1920 480\n"
                               "track T 0 0 0 0 CLAP /a.clap ~ AAA\n"}) {
        project_error.clear();
        assert(!ProjectFile::parse(broken, &project_error).has_value());
        assert(!project_error.empty());
    }
    // The prologue those cases are built from must itself be accepted, so the
    // rejections above prove the defect and not a typo in the fixture.
    assert(ProjectFile::parse(valid_prologue, &project_error).has_value());

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
#endif
}
