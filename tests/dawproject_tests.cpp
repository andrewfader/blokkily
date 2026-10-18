// features/dawproject.feature: a song exported as a .dawproject. Every case
// reads the archive back. Notes are compared with what Song::arrange gives
// the render callback, a song rebuilt from the file alone is bounced against
// the original through the same engine, and the CLAP and VST3 fixtures'
// states are loaded from the archive into fresh instances that must then
// sound like the originals.
#include "engine_graph.hpp"
#include "blokkily/audio/bounce.hpp"
#include "blokkily/audio/mixer.hpp"
#include "blokkily/audio/wave_file.hpp"
#include "blokkily/plugins/clap_instance.hpp"
#include "blokkily/plugins/vst3_instance.hpp"
#include "blokkily/project/dawproject.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

using namespace blokkily;
namespace {

void require(bool ok, const std::string& why) {
    if (!ok) throw std::runtime_error(why);
}

const std::filesystem::path work = BLOKKILY_DAWPROJECT_WORK;

std::filesystem::path fresh(const std::string& name) {
    std::filesystem::create_directories(work);
    const auto path = work / name;
    std::filesystem::remove(path);
    return path;
}

// The CLAP fixture's state stream with its oscillator on (its version 2
// layout: a tag and every parameter), so a render has a pitch to compare.
std::vector<std::byte> fixture_state(float level, float tone) {
    struct {
        std::array<char, 4> tag{'B', 'K', 'S', '2'};
        float level, tone, velocity_mode = 0.0F;
    } state;
    state.level = level;
    state.tone = tone;
    std::vector<std::byte> bytes(sizeof state);
    std::memcpy(bytes.data(), &state, sizeof state);
    return bytes;
}

InstrumentSlot clap_slot(float level = 0.25F) {
    return {"CLAP", BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test", fixture_state(level, 1.0F)};
}

Trigger note_at(Tick start, Tick duration, int key, float velocity) {
    Trigger trigger;
    trigger.start = start;
    trigger.duration = duration;
    trigger.musical_data = Note{static_cast<std::uint8_t>(key), velocity};
    return trigger;
}

// A song that uses most of what the format can hold: two CLAP tracks in one
// section placed twice, a return fed by a send, a tempo ramp, notes off the
// grid, a probability, a ratchet, a retune with pitch expression, a
// controller and value step locks on the instrument.
Song make_song() {
    Song song;
    song.patterns.clear();
    song.tracks.clear();
    song.clips.clear();
    for (const char* name : {"Lead", "Bass"}) {
        Track track;
        track.name = name;
        track.instrument = clap_slot();
        song.tracks.push_back(track);
    }
    song.tracks[0].mix = {-3.0, -0.5, false, false};
    song.tracks[1].mix = {-6.0, 0.4, false, false};
    song.returns.resize(1);
    song.returns[0].name = "Room";
    song.returns[0].mix.gain_db = -2.0;
    song.tracks[0].sends.push_back({0, -9.0, false});
    song.master_gain_db = -1.0;
    const auto section = song.add_section("VERSE", 1920, 480);
    auto& lead = song.patterns[*song.part(section, 0)].pattern;
    auto& bass = song.patterns[*song.part(section, 1)].pattern;
    for (int step = 0; step < 4; ++step) {
        auto trigger = note_at(step * 480, 300, 64 + step * 3, 0.5F + 0.1F * static_cast<float>(step));
        trigger.micro_offset = step == 1 ? 17 : 0;
        if (step == 2) trigger.probability = 0.5F;
        if (step == 3) trigger.ratchets = 2;
        (void)lead.add(trigger);
    }
    auto retuned = note_at(1680, 200, 76, 0.9F);
    std::get<Note>(retuned.musical_data).cents = 31.0;
    retuned.expression = {{0, 60, NoteExpression::Kind::pitch, 0.5F},
                          {0, 120, NoteExpression::Kind::timbre, 0.25F}};
    (void)lead.add(retuned);
    lead.add_continuous({240, ContinuousEvent::Kind::control_change, 1, 100});
    lead.add_continuous({900, ContinuousEvent::Kind::pitch_bend, 0, 12000});
    for (int step = 0; step < 2; ++step) {
        auto trigger = note_at(step * 960, 720, 36 + step * 7, 1.0F);
        trigger.locks = {{"level", 0, step == 0 ? 0.2 : 0.3, ParameterLock::Kind::automation}};
        (void)bass.add(trigger);
    }
    (void)song.place_section(section, 0);
    (void)song.place_section(section, 1920);
    song.tempo.set({0, 110.0, true});
    song.tempo.set({1920, 130.0, false});
    song.notes = "Exported for another DAW";
    return song;
}

struct Engine {
    SongEngine engine;
    GraphBuild graph;
};

std::unique_ptr<Engine> prepare(const Song& song) {
    auto built = std::make_unique<Engine>();
    built->graph = populate_graph(built->engine, song, {}, {});
    require(built->graph.error.empty(), built->graph.error);
    std::string error;
    require(built->engine.prepare(song, 48000, 512, 0, &error, {}), error);
    load_fresh_state(built->engine, song, built->graph);
    return built;
}

// What the running processors are as another host loads them: the states an
// export is given.
DawProjectOptions options_from(const SongEngine& engine) {
    DawProjectOptions options;
    options.application_version = "test";
    options.title = "Song";
    for (const auto where : engine.processor_addresses())
        if (auto* instance = engine.processor(where))
            if (auto state = instance->portable_state()) options.states.emplace_back(where, std::move(*state));
    return options;
}

std::vector<float> bounce(SongEngine& engine, const std::string& name) {
    const auto path = fresh(name);
    std::string error;
    require(bounce_song(engine, path, WaveFormat::float32, 4800, &error).has_value(), error);
    const auto wave = read_wave(path, &error);
    require(wave.has_value(), error);
    return wave->interleaved;
}

Tick ticks(double beats, Tick tpb) { return static_cast<Tick>(std::llround(beats * static_cast<double>(tpb))); }

bool near(double a, double b, double tolerance = 1e-9) { return std::abs(a - b) <= tolerance; }

const DawProjectFile read_back(const std::filesystem::path& path) {
    std::string error;
    auto file = read_dawproject(path, &error);
    require(file.has_value(), "read back: " + error);
    return *file;
}

// Runs xmllint against the official DAWproject schema (vendored from
// github.com/bitwig/dawproject, MIT).
void require_valid(const std::string& xml, const std::string& schema, const std::string& name) {
    const auto file = fresh(name);
    std::ofstream(file, std::ios::binary) << xml;
    const auto command = std::string("\"") + BLOKKILY_XMLLINT + "\" --noout --schema \"" +
                         BLOKKILY_DAWPROJECT_SCHEMA_DIR + "/" + schema + "\" \"" + file.string() + "\"";
    require(std::system(command.c_str()) == 0, name + " does not validate against " + schema);
}

std::string entry_text(const std::filesystem::path& archive, const std::string& entry) {
    // Unpacked by CMake's libarchive, so the archive is proved a plain ZIP
    // another tool opens, not one only our own reader understands.
    const auto folder = work / (archive.stem().string() + "-unpacked");
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    const auto command = std::string("\"") + BLOKKILY_CMAKE_COMMAND + "\" -E chdir \"" + folder.string() + "\" \"" +
                         BLOKKILY_CMAKE_COMMAND + "\" -E tar xf \"" + archive.string() + "\"";
    require(std::system(command.c_str()) == 0, "unpack " + archive.string());
    std::ifstream in(folder / entry, std::ios::binary);
    require(in.good(), entry + " is in the archive");
    return std::string(std::istreambuf_iterator<char>(in), {});
}

// --- Scenarios --------------------------------------------------------------

// The notes, controllers and locks the engine plays, clip by clip.
void notes() {
    const auto song = make_song();
    auto engine = prepare(song);
    const auto path = fresh("notes.dawproject");
    std::string error;
    const auto report = write_dawproject(path, song, options_from(engine->engine), &error);
    require(report.has_value(), error);
    const auto file = read_back(path);
    const Tick tpb = song.ticks_per_beat();
    require(file.application == "Blokkily", "application");
    require(file.tracks.size() == 4 && file.tracks[2].channel.role == "effect" &&
                file.tracks[3].channel.role == "master",
            "tracks, the return and the master, in mixer order");
    std::size_t notes_seen = 0;
    for (std::size_t index = 0; index < song.tracks.size(); ++index) {
        const auto& track = file.tracks[index];
        require(track.name == song.tracks[index].name, "track name");
        require(track.clips.size() == 2, "a clip per placement");
        require(track.clips[0].name == "VERSE" && near(track.clips[1].time, 4.0), "clip names and places");
        const auto played = song.arrange(index, 0);
        std::vector<std::tuple<Tick, Tick, int, double>> exported;
        for (const auto& clip : track.clips)
            for (const auto& note : clip.notes)
                exported.emplace_back(ticks(clip.time + note.time, tpb), ticks(note.duration, tpb), note.key,
                                      note.velocity);
        std::sort(exported.begin(), exported.end());
        std::vector<std::tuple<Tick, Tick, int, double>> expected;
        for (const auto& note : played.notes)
            expected.emplace_back(note.start, note.duration, note.key, note.velocity);
        std::sort(expected.begin(), expected.end());
        require(exported.size() == expected.size(), "the engine's note count on " + track.name);
        for (std::size_t n = 0; n < expected.size(); ++n) {
            require(std::get<0>(exported[n]) == std::get<0>(expected[n]) &&
                        std::get<1>(exported[n]) == std::get<1>(expected[n]) &&
                        std::get<2>(exported[n]) == std::get<2>(expected[n]),
                    "the engine's note timing and pitch");
            require(near(std::get<3>(exported[n]), std::get<3>(expected[n]), 1e-6), "velocity");
        }
        notes_seen += exported.size();
    }
    require(report->notes == notes_seen, "the report counts the notes written");
    // The ratchet's two hits and the probability as the engine decided it.
    require(notes_seen == song.arrange(0, 0).notes.size() + song.arrange(1, 0).notes.size(), "every note");

    // The retuned note carries its retune and pitch expression as one
    // transpose lane, and its timbre on a lane of its own.
    const DawProjectNote* retuned = nullptr;
    for (const auto& note : file.tracks[0].clips[0].notes)
        if (note.key == 76) retuned = &note;
    require(retuned != nullptr && retuned->expressions.size() == 2, "a retuned note's lanes");
    const auto& transpose = retuned->expressions[0];
    require(transpose.expression == "transpose" && transpose.points.size() == 2 &&
                near(transpose.points[0].value, 0.31) && near(transpose.points[1].value, 0.81) &&
                near(transpose.points[1].time, 60.0 / 480.0),
            "retune and pitch expression in semitones");
    require(retuned->expressions[1].expression == "timbre" && near(retuned->expressions[1].points[0].value, 0.25),
            "timbre expression");

    // The controllers and the locks, in the clips that play them.
    const auto& lead_clip = file.tracks[0].clips[0];
    bool cc = false, bend = false;
    for (const auto& lane : lead_clip.points) {
        if (lane.expression == "channelController" && lane.controller == 1 && lane.points.size() == 1 &&
            near(lane.points[0].value, 100.0 / 127.0) && near(lane.points[0].time, 0.5))
            cc = true;
        if (lane.expression == "pitchBend" && near(lane.points[0].value, 12000.0 / 16383.0)) bend = true;
    }
    require(cc && bend, "controller movements in the lead's clip");
    const auto& bass = file.tracks[1];
    require(bass.channel.devices.size() == 1, "the bass instrument");
    const auto& level = bass.channel.devices[0].parameters;
    require(level.size() == 1 && level[0].name == "Level" && level[0].parameter_id == 0, "the locked parameter is listed");
    const auto& lock_lane = bass.clips[1].points.at(0);
    require(lock_lane.target_parameter == level[0].id && lock_lane.points.size() == 2 &&
                near(lock_lane.points[0].value, 0.2) && near(lock_lane.points[1].value, 0.3) &&
                near(lock_lane.points[1].time, 2.0) && lock_lane.points[0].interpolation == "hold",
            "value locks hold from their step on");

    // Tempo, meter and the song's notes.
    require(near(file.tempo, 110.0) && file.tempo_automation.size() == 2 &&
                file.tempo_automation[0].interpolation == "linear" && near(file.tempo_automation[1].time, 4.0) &&
                near(file.tempo_automation[1].value, 130.0),
            "the tempo ramp");
    require(file.comment == song.notes && file.title == "Song", "metadata");
    require(report->omitted.empty(), "nothing omitted from a song the format holds");
}

// A song rebuilt from the file alone sounds like the song the engine plays.
void render() {
    auto song = make_song();
    // A muted track and a stepped gain lane, so the mixer is in the proof.
    Track muted;
    muted.name = "Muted";
    muted.instrument = clap_slot();
    muted.mix.mute = true;
    song.tracks.push_back(muted);
    song.add_track_parts(2);
    (void)song.patterns[*song.part(0, 2)].pattern.add(note_at(0, 900, 50, 1.0F));
    song.clips.push_back({2, *song.part(0, 2), 0, 1});
    AutomationLane gain;
    gain.target.kind = AutomationTarget::Kind::gain;
    gain.target.processor = {BusKind::track, 1, -1};
    gain.points = {{0, -6.0}, {1440, -6.0}, {1440, -12.0}};
    song.tracks[1].automation.push_back(gain);
    require(song.consistent(), "the song");

    auto engine = prepare(song);
    const auto path = fresh("render.dawproject");
    std::string error;
    require(write_dawproject(path, song, options_from(engine->engine), &error).has_value(), error);
    const auto file = read_back(path);
    const Tick tpb = song.ticks_per_beat();

    // The song again, from nothing but what the file says.
    Song rebuilt;
    rebuilt.patterns.clear();
    rebuilt.tracks.clear();
    rebuilt.clips.clear();
    rebuilt.tempo.points.clear();
    for (std::size_t index = 0; index < file.tempo_automation.size(); ++index) {
        const auto& point = file.tempo_automation[index];
        rebuilt.tempo.points.push_back({ticks(point.time, tpb), point.value, point.interpolation == "linear"});
    }
    std::map<std::string, std::size_t> returns;
    for (const auto& track : file.tracks)
        if (track.channel.role == "effect") {
            returns[track.channel.id] = rebuilt.returns.size();
            ReturnBus bus;
            bus.name = track.name;
            bus.mix.gain_db = linear_to_db(track.channel.volume);
            bus.mix.pan = track.channel.pan * 2.0 - 1.0;
            rebuilt.returns.push_back(bus);
        } else if (track.channel.role == "master") {
            rebuilt.master_gain_db = linear_to_db(track.channel.volume);
        }
    Tick length = 0;
    for (const auto& track : file.tracks)
        for (const auto& clip : track.clips) length = std::max(length, ticks(clip.time + clip.duration, tpb));
    length = (length + 1919) / 1920 * 1920;
    for (const auto& track : file.tracks) {
        if (track.channel.role != "regular") continue;
        const auto index = rebuilt.tracks.size();
        Track rebuilt_track;
        rebuilt_track.name = track.name;
        const auto& device = track.channel.devices.at(0);
        require(device.element == "ClapPlugin" && device.device_id == "dev.blokkily.test" && !device.state.empty(),
                "a CLAP instrument with its state");
        rebuilt_track.instrument = {"CLAP", BLOKKILY_TEST_CLAP_PATH, device.device_id, device.state};
        rebuilt_track.mix = {linear_to_db(track.channel.volume), track.channel.pan * 2.0 - 1.0,
                             track.channel.mute, track.channel.solo};
        for (const auto& send : track.channel.sends)
            rebuilt_track.sends.push_back({returns.at(send.destination), linear_to_db(send.volume), send.type == "pre"});
        for (const auto& lane : track.automation) {
            require(lane.target_parameter == track.channel.volume_id, "a volume lane");
            AutomationLane volume;
            volume.target.kind = AutomationTarget::Kind::gain;
            volume.target.processor = {BusKind::track, static_cast<std::uint32_t>(index), -1};
            for (const auto& point : lane.points)
                volume.points.push_back({ticks(point.time, tpb), linear_to_db(point.value)});
            rebuilt_track.automation.push_back(volume);
        }
        rebuilt.tracks.push_back(rebuilt_track);
        PatternSlot slot;
        slot.name = track.name;
        slot.pattern = Pattern(length, tpb);
        slot.track = index;
        for (const auto& clip : track.clips)
            for (const auto& note : clip.notes) {
                auto trigger = note_at(ticks(clip.time + note.time, tpb), ticks(note.duration, tpb), note.key,
                                       static_cast<float>(note.velocity));
                for (const auto& lane : note.expressions)
                    if (lane.expression == "transpose" && !lane.points.empty() && lane.points[0].time == 0.0)
                        std::get<Note>(trigger.musical_data).cents = lane.points[0].value * 100.0;
                    else
                        for (const auto& point : lane.points)
                            trigger.expression.push_back(
                                {0, ticks(point.time, tpb),
                                 lane.expression == "timbre" ? NoteExpression::Kind::timbre
                                                             : NoteExpression::Kind::pressure,
                                 static_cast<float>(point.value)});
                // Pitch expression past the retune, relative to it.
                for (const auto& lane : note.expressions)
                    if (lane.expression == "transpose")
                        for (std::size_t p = 1; p < lane.points.size(); ++p)
                            trigger.expression.push_back({0, ticks(lane.points[p].time, tpb),
                                                          NoteExpression::Kind::pitch,
                                                          static_cast<float>(lane.points[p].value - lane.points[0].value)});
                std::sort(trigger.expression.begin(), trigger.expression.end(),
                          [](const NoteExpression& a, const NoteExpression& b) { return a.offset < b.offset; });
                (void)slot.pattern.add(trigger);
            }
        for (const auto& clip : track.clips)
            for (const auto& lane : clip.points) {
                if (!lane.expression.empty()) {
                    for (const auto& point : lane.points) {
                        ContinuousEvent event;
                        event.tick = ticks(clip.time + point.time, tpb);
                        event.kind = lane.expression == "pitchBend" ? ContinuousEvent::Kind::pitch_bend
                                                                    : ContinuousEvent::Kind::control_change;
                        event.controller = static_cast<std::uint8_t>(std::max(0, lane.controller));
                        event.value = static_cast<std::uint16_t>(
                            std::lround(point.value * (event.kind == ContinuousEvent::Kind::pitch_bend ? 16383.0 : 127.0)));
                        slot.pattern.add_continuous(event);
                    }
                    continue;
                }
                // A lock lane: the parameter it names, locked on the note
                // that starts at each point's tick.
                for (const auto& parameter : device.parameters)
                    if (parameter.id == lane.target_parameter)
                        for (const auto& point : lane.points) {
                            const Tick at = ticks(clip.time + point.time, tpb);
                            for (const auto& event : slot.pattern.events())
                                if (event.start == at) {
                                    auto locked = event;
                                    locked.locks.push_back({parameter.name, static_cast<std::int32_t>(parameter.parameter_id),
                                                            point.value, ParameterLock::Kind::automation});
                                    require(slot.pattern.update(event.id, locked), "a lock on its note");
                                    break;
                                }
                        }
            }
        rebuilt.patterns.push_back(std::move(slot));
        rebuilt.clips.push_back({index, index, 0, 1});
    }
    std::string why;
    require(rebuilt.consistent(&why), "the rebuilt song: " + why);

    auto rebuilt_engine = prepare(rebuilt);
    const auto original = bounce(engine->engine, "render-original.wav");
    const auto again = bounce(rebuilt_engine->engine, "render-rebuilt.wav");
    require(original.size() == again.size(), "the same length");
    double peak = 0.0, worst = 0.0;
    for (std::size_t i = 0; i < original.size(); ++i) {
        peak = std::max(peak, static_cast<double>(std::abs(original[i])));
        worst = std::max(worst, static_cast<double>(std::abs(original[i] - again[i])));
    }
    std::cerr << "render: peak " << peak << ", largest difference " << worst << '\n';
    require(peak > 0.05, "the original is not silent");
    require(worst < 1e-6, "the rebuilt song renders the original sample for sample");
}

// The CLAP state in the archive is the plugin's state stream.
void clap_state() {
    std::string error;
    auto original = ClapPluginInstance::create(BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test", &error);
    require(original != nullptr, error);
    require(original->load_state(fixture_state(0.6F, 1.0F)), "the fixture takes a state");
    Song song;
    song.tracks[0].name = "Synth";
    song.tracks[0].instrument = clap_slot(0.6F);
    DawProjectOptions options;
    auto state = original->portable_state();
    require(state.has_value() && state->device_id == "dev.blokkily.test" && state->device_name == "Blokkily Test Synth" &&
                state->preset_extension == "clap-preset" && state->parameters.size() == 3,
            "the CLAP adapter's portable state");
    options.states.emplace_back(track_instrument(0), *state);
    const auto path = fresh("clap.dawproject");
    const auto report = write_dawproject(path, song, options, &error);
    require(report.has_value() && report->plugin_states == 1, error);
    const auto file = read_back(path);
    const auto& device = file.tracks.at(0).channel.devices.at(0);
    require(device.element == "ClapPlugin" && device.device_role == "instrument" && device.device_name == "Blokkily Test Synth",
            "a ClapPlugin device");
    require(device.state_path.ends_with(".clap-preset") && device.state == original->save_state(),
            "the state stream byte for byte");

    // A fresh instance given the archived state sounds like the original.
    auto restored = ClapPluginInstance::create(BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test", &error);
    require(restored != nullptr && restored->load_state(device.state), "the archived state loads");
    const auto play = [](PluginInstance& plugin) {
        require(plugin.activate(48000, 1, 256), "activate");
        std::vector<float> left(256), right(256), all;
        const std::array<PluginEvent, 1> on{PluginEvent{PluginEvent::Type::note_on, 0, 60, 1.0, 0}};
        for (int block = 0; block < 8; ++block) {
            std::fill(left.begin(), left.end(), 0.0F);
            std::fill(right.begin(), right.end(), 0.0F);
            plugin.process({left, right}, block == 0 ? std::span<const PluginEvent>(on) : std::span<const PluginEvent>());
            all.insert(all.end(), left.begin(), left.end());
        }
        return all;
    };
    const auto heard = play(*original);
    const auto again = play(*restored);
    const auto loud = std::any_of(heard.begin(), heard.end(), [](float v) { return std::abs(v) > 0.1F; });
    require(loud && heard == again, "the restored instance plays what the original plays");
}

// The VST3 state in the archive is a .vstpreset under the plugin's class id,
// loaded through the production JUCE adapter.
void vst3_state() {
    std::string error;
    auto original = Vst3PluginInstance::create(BLOKKILY_TEST_VST3_PATH, 0, &error);
    require(original != nullptr, error);
    const auto parameters = original->parameters();
    require(parameters.size() >= 2, "the VST3 fixture's parameters");
    // Its level moved from the default, through its own state.
    {
        auto clap_like = original->save_state();
        require(!clap_like.empty(), "the VST3 fixture saves a state");
    }
    PluginEvent move{PluginEvent::Type::parameter_value, 0, parameters[0].id, 0.8, 0};
    require(original->activate(48000, 1, 256), "activate the VST3");
    std::vector<float> left(256), right(256);
    original->process({left, right}, std::span<const PluginEvent>(&move, 1));
    require(near(original->parameter_value(parameters[0].id).value_or(0.0), 0.8, 1e-6), "the level moved");

    auto state = original->portable_state();
    require(state.has_value(), "the VST3 adapter's portable state");
    require(state->device_id.size() == 32 &&
                std::all_of(state->device_id.begin(), state->device_id.end(),
                            [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; }),
            "a 32-digit class id");
    require(state->preset_extension == "vstpreset" && state->preset.size() > 48 &&
                std::memcmp(state->preset.data(), "VST3", 4) == 0,
            "a .vstpreset");

    Song song;
    song.tracks[0].name = "Pad";
    song.tracks[0].instrument = {"VST3", BLOKKILY_TEST_VST3_PATH, "", original->save_state()};
    AutomationLane lane;
    lane.target.kind = AutomationTarget::Kind::parameter;
    lane.target.parameter_index = parameters[0].id;
    lane.target.processor = track_instrument(0);
    lane.points = {{0, 0.8}, {1920, 0.4}};
    song.tracks[0].automation.push_back(lane);
    DawProjectOptions options;
    options.states.emplace_back(track_instrument(0), *state);
    const auto path = fresh("vst3.dawproject");
    require(write_dawproject(path, song, options, &error).has_value(), error);
    const auto file = read_back(path);
    const auto& device = file.tracks.at(0).channel.devices.at(0);
    require(device.element == "Vst3Plugin" && device.device_id == state->device_id && device.state == state->preset,
            "a Vst3Plugin device with its preset");
    require(device.parameters.size() == 1 && device.parameters[0].parameter_id >= 0 &&
                device.parameters[0].name == parameters[0].name,
            "the automated parameter, by its VST3 ParamID");
    require(file.tracks[0].automation.size() == 1 &&
                file.tracks[0].automation[0].target_parameter == device.parameters[0].id &&
                file.tracks[0].automation[0].points.size() == 2,
            "its automation lane");

    auto restored = Vst3PluginInstance::create(BLOKKILY_TEST_VST3_PATH, 0, &error);
    require(restored != nullptr, error);
    require(!near(restored->parameter_value(parameters[0].id).value_or(0.0), 0.8, 1e-3), "a fresh instance starts elsewhere");
    require(restored->load_preset(device.state), "the archived preset loads");
    require(near(restored->parameter_value(parameters[0].id).value_or(0.0), 0.8, 1e-6),
            "the restored instance holds the original's level");
}

// An audio clip goes into the archive with warp markers that place its
// audio where the engine plays it.
void audio_clip() {
    const auto wav = fresh("loop.wav");
    {
        WaveWriter writer;
        std::string error;
        require(writer.open(wav, 44100, WaveFormat::pcm16, &error), error);
        std::vector<float> left(44100), right(44100);
        for (std::size_t i = 0; i < left.size(); ++i) left[i] = right[i] = 0.25F * std::sin(static_cast<float>(i) * 0.05F);
        require(writer.write(left, right, &error) && writer.close(&error), error);
    }
    Song song;
    song.tracks[0].name = "Drums";
    song.audio_files.push_back({wav, 44100, 44100, 2});
    AudioClip clip;
    clip.id = 1;
    clip.start = 960;
    clip.offset_frames = 4410;
    clip.length_frames = 30000;
    clip.fade_in_frames = 441;
    song.audio_clips.push_back(clip);
    song.tempo.set({0, 120.0, false});
    song.tempo.set({1440, 90.0, false});
    const auto path = fresh("audio.dawproject");
    std::string error;
    const auto report = write_dawproject(path, song, {}, &error);
    require(report.has_value() && report->audio_files == 1, error);
    const auto file = read_back(path);
    require(file.tracks[0].content_type == "notes audio", "a track of notes and audio");
    const DawProjectClip* audio = nullptr;
    for (const auto& candidate : file.tracks[0].clips)
        if (!candidate.audio_path.empty()) audio = &candidate;
    require(audio != nullptr && audio->audio_path == "audio/loop.wav", "the clip names its file in the archive");
    require(std::find(file.entries.begin(), file.entries.end(), "audio/loop.wav") != file.entries.end(), "the file is archived");
    std::ifstream in(wav, std::ios::binary);
    const std::string original(std::istreambuf_iterator<char>(in), {});
    require(entry_text(path, "audio/loop.wav") == original, "the archived audio is the file byte for byte");
    require(audio->audio_sample_rate == 44100 && audio->audio_channels == 2 && near(audio->audio_seconds, 1.0) &&
                near(audio->fade_in, 0.01),
            "the file's format and the fade");
    require(near(audio->time, 2.0), "the clip starts where it plays");
    const double length = 30000.0 / 44100.0;
    require(near(audio->duration, (audio_clip_tick_at(song, song.audio_clips[0], length) - 960.0) / 480.0, 1e-9),
            "the clip lasts as long as it sounds");
    require(audio->warps.size() >= 3, "warp markers across the tempo change");
    for (const auto& warp : audio->warps) {
        const double expected = audio_clip_seconds_at(song, song.audio_clips[0], 960.0 + warp.time * 480.0) + 0.1;
        require(near(warp.content_time, expected, 1e-9), "each marker is where the engine plays that audio");
    }
}

// What the format has no place for is reported, not dropped in silence.
void omissions() {
    Song song;
    song.tracks.resize(3);
    song.tracks[0].name = "Keys";
    song.tracks[0].instrument = {"SoundFont", "/nowhere/piano.sf2", "", {}};
    song.tracks[0].arp.enabled = true;
    song.tracks[1].name = "Synth";
    song.tracks[1].instrument = clap_slot();
    song.tracks[2].name = "Aux";
    song.tracks[2].source = InstrumentOutput{1, 1};
    song.add_track_parts(0);
    Modulator lfo;
    song.modulators.push_back(lfo);
    auto trigger = note_at(0, 120, 60, 1.0F);
    trigger.locks = {{"level", 0, 0.5, ParameterLock::Kind::modulation}};
    (void)song.patterns[0].pattern.add(trigger);
    song.clips = {{1, 0, 0, 1}};
    std::string why;
    require(song.consistent(&why), why);
    const auto path = fresh("omissions.dawproject");
    std::string error;
    const auto report = write_dawproject(path, song, {}, &error);
    require(report.has_value(), error);
    const auto said = [&](const std::string& text) {
        return std::any_of(report->omitted.begin(), report->omitted.end(),
                           [&](const std::string& line) { return line.find(text) != std::string::npos; });
    };
    for (const auto& line : report->omitted) std::cerr << "omitted: " << line << '\n';
    require(said("Track \"Keys\"'s instrument piano: SoundFont has no DAWproject device"), "a SoundFont");
    require(said("Track \"Keys\": its arpeggiator"), "the arpeggiator");
    require(said("Track \"Synth\"'s instrument Blokkily Test Synth: its state was not captured") ||
                said("Track \"Synth\"'s instrument dev.blokkily.test: its state was not captured"),
            "a plugin whose state was not given");
    require(said("Track \"Synth\": 1 modulation step locks"), "a modulation lock");
    require(said("Track \"Aux\": played by another track's instrument output"), "an instrument output");
    require(said("1 LFOs, macros and envelope followers"), "the modulators");
    const auto file = read_back(path);
    require(file.tracks[0].channel.devices.at(0).element == "Device" &&
                file.tracks[0].channel.devices[0].device_name == "piano",
            "the SoundFont as a placeholder named after its file");
}

// The archive is a plain ZIP whose XML the official schema accepts, and the
// same song writes the same bytes.
void schema() {
    const auto song = make_song();
    auto engine = prepare(song);
    const auto options = options_from(engine->engine);
    const auto first = fresh("schema-1.dawproject");
    const auto second = fresh("schema-2.dawproject");
    std::string error;
    require(write_dawproject(first, song, options, &error).has_value(), error);
    require(write_dawproject(second, song, options, &error).has_value(), error);
    require_valid(entry_text(first, "project.xml"), "Project.xsd", "project.xml");
    require_valid(entry_text(first, "metadata.xml"), "MetaData.xsd", "metadata.xml");
    for (const auto& entry : read_back(first).entries)
        if (entry.starts_with("plugins/")) require(!entry_text(first, entry).empty(), "a plugin state in the archive");
    std::ifstream a(first, std::ios::binary), b(second, std::ios::binary);
    require(std::string(std::istreambuf_iterator<char>(a), {}) == std::string(std::istreambuf_iterator<char>(b), {}),
            "one song, one archive");
    require(!std::filesystem::exists(first.string() + ".part"), "no partial file left behind");
    // A song the format holds less of still validates.
    Song audio;
    const auto wav = fresh("schema.wav");
    {
        WaveWriter writer;
        require(writer.open(wav, 48000, WaveFormat::pcm16, &error), error);
        std::vector<float> silence(4800);
        require(writer.write(silence, silence, &error) && writer.close(&error), error);
    }
    audio.audio_files.push_back({wav, 4800, 48000, 2});
    AudioClip clip;
    clip.id = 1;
    clip.length_frames = 4800;
    audio.audio_clips.push_back(clip);
    audio.tracks[0].instrument = {"SoundFont", "/nowhere/piano.sf2", "", {}};
    audio.tracks[0].inserts.push_back({{"Built-in", "", "compressor", {}}, false, {{0, 0.5}, {1, 4.0}}, {}});
    audio.meter.set({2, 7, 8});
    const auto third = fresh("schema-3.dawproject");
    require(write_dawproject(third, audio, {}, &error).has_value(), error);
    require_valid(entry_text(third, "project.xml"), "Project.xsd", "project-3.xml");
    const auto file = read_back(third);
    require(file.meter_automation.size() == 2 && file.meter_automation[1].numerator == 7 &&
                file.meter_automation[1].denominator == 8 && near(file.meter_automation[1].time, 8.0),
            "the meter change at its bar");
}

} // namespace

int main(int argc, char** argv) try {
    const std::map<std::string, std::function<void()>> cases{
        {"notes", notes},         {"render", render}, {"clap_state", clap_state}, {"vst3_state", vst3_state},
        {"audio_clip", audio_clip}, {"omissions", omissions}, {"schema", schema}};
    if (argc != 2 || cases.find(argv[1]) == cases.end()) {
        std::cerr << "usage: blokkily_dawproject_tests <case>\n";
        return 2;
    }
    cases.at(argv[1])();
    std::cout << "dawproject " << argv[1] << " passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "dawproject: " << error.what() << '\n';
    return 1;
}
