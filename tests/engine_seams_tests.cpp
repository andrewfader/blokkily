// Engine seams (plan F-D, item 1.1), each proved from rendered audio through
// the production render callback, with the real CLAP, VST3 and SoundFont
// processors where a processor's behaviour is the claim.
//
// features/audio_reliability.feature:
//   Scenario: A track without an instrument still plays through its strip
//   Scenario: A processor's own parameter edits are stamped where they happened
//   Scenario: Rebuilding the graph keeps the instruments that did not change
//   Scenario: A deleted track does not hand its instrument to its neighbour
//   Scenario: A running plugin is not sent a state it cannot take while running
//   Scenario: A burst of edits is one recompile of the latest song
//   Scenario: A busy engine is asked again rather than losing the edit
//   Scenario: The instrument a slot names is created by one factory

#include "audio/engine/test_access.hpp"
#include "engine_graph.hpp"
#include "processor_factory.hpp"
#include "recompile_coalescer.hpp"
#include "support/audio_probe.hpp"

#include "blokkily/audio/mixer.hpp"
#include "blokkily/audio/playback.hpp"
#include "blokkily/audio/song_engine.hpp"
#include "blokkily/instruments/soundfont_synth.hpp"
#include "blokkily/plugins/clap_instance.hpp"
#include "blokkily/plugins/dynamic_library.hpp"
#include "blokkily/plugins/vst3_instance.hpp"


#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using namespace blokkily;

namespace {

void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

constexpr double rate = 48000.0;
constexpr std::uint32_t block = 256;
// One tick per sample: 24000 ticks a beat at 120 BPM and 48 kHz.
constexpr Tick ticks_per_beat = 24000;

InstrumentSlot clap_slot() { return {"CLAP", BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test", {}}; }
InstrumentSlot vst3_slot() { return {"VST3", BLOKKILY_TEST_VST3_PATH, "", {}}; }
InstrumentSlot soundfont_slot() { return {"SoundFont", BLOKKILY_TEST_SF2_PATH, "", {}}; }

Song song_of(std::size_t tracks, Tick length = 4096) {
    Song song;
    song.patterns = {{"Seams", Pattern(length, ticks_per_beat)}};
    song.tracks.assign(tracks, Track{});
    for (std::size_t track = 0; track < tracks; ++track)
        song.tracks[track].name = "T" + std::to_string(track);
    song.clips = {{0, 0, 0, 1}};
    return song;
}

void add_note(Song& song, Tick start, Tick duration) {
    Trigger note;
    note.start = start;
    note.duration = duration;
    note.musical_data = Note{60, 1.0F, 0.0F};
    (void)song.patterns[0].pattern.add(note);
}

// The production callback, pumped deterministically: non-interleaved stereo.
struct Pumped {
    std::vector<float> left;
    std::vector<float> right;
};
Pumped pump(RtAudioOutput& output, std::size_t frames) {
    std::vector<float> stereo(frames * 2, -1.0F);
    require(output.pump(stereo), "the production callback must render");
    return {{stereo.begin(), stereo.begin() + static_cast<std::ptrdiff_t>(frames)},
            {stereo.begin() + static_cast<std::ptrdiff_t>(frames), stereo.end()}};
}

bool all_near(const std::vector<float>& samples, float expected, float tolerance = 1e-5F) {
    return std::all_of(samples.begin(), samples.end(),
                       [&](float sample) { return std::abs(sample - expected) <= tolerance; });
}

// The CLAP fixture's lifecycle log, read through its exported hook in the very
// module the adapter loaded.
struct FixtureLog {
    long created = 0;
    long destroyed = 0;
};
FixtureLog fixture_log() {
    static void* module = blokkily::dynamic_library::open(BLOKKILY_TEST_CLAP_PATH);
    require(module != nullptr, "the CLAP fixture must load");
    using Counts = void (*)(long*, long*);
    auto counts = reinterpret_cast<Counts>(
        blokkily::dynamic_library::symbol(module, "blokkily_test_instance_counts"));
    require(counts != nullptr, "the CLAP fixture must export its lifecycle log");
    FixtureLog log;
    counts(&log.created, &log.destroyed);
    return log;
}

// A source standing in for audio clips: a steady level on both channels.
void dc_source(void* context, StereoBlock track, std::uint64_t) noexcept {
    const float level = *static_cast<const float*>(context);
    for (auto& sample : track.left) sample += level;
    for (auto& sample : track.right) sample += level;
}

// An instrument whose knob moves on its own every block, the way a plugin's
// window reports a turn, and which may accept state while it runs.
class EditingInstrument final : public PluginInstance {
public:
    explicit EditingInstrument(bool accepts_running_state = false)
        : accepts_(accepts_running_state) {}
    bool activate(double, std::uint32_t, std::uint32_t) override { return true; }
    void process(StereoBlock audio, std::span<const PluginEvent>) noexcept override {
        std::fill(audio.left.begin(), audio.left.end(), 0.0F);
        std::fill(audio.right.begin(), audio.right.end(), 0.0F);
        pending_ = true;
        ++value_;
    }
    std::size_t take_parameter_edits(std::span<ParameterEdit> edits) noexcept override {
        if (!pending_ || edits.empty()) return 0;
        pending_ = false;
        edits[0] = {ParameterEdit::Kind::value, 3, value_, offset};
        return 1;
    }
    std::vector<std::byte> save_state() override { return state_; }
    bool load_state(std::span<const std::byte> state) override {
        state_.assign(state.begin(), state.end());
        return true;
    }
    bool accepts_state_while_running() const noexcept override { return accepts_; }
    std::string format() const override { return "Test"; }

    static constexpr std::uint32_t offset = 7;
    std::vector<std::byte> state_;

private:
    bool accepts_ = false;
    bool pending_ = false;
    double value_ = 0.0;
};

void play_level(SongEngine& engine, std::size_t track, double level) {
    require(engine.play_live(track, {PluginEvent::Type::parameter_value, 0, 0, level, 0.0}),
            "a parameter reaches the live queue");
    require(engine.play_live(track, {PluginEvent::Type::note_on, 0, 60, 1.0, 0.0}),
            "a note reaches the live queue");
}

// features/audio_reliability.feature:
//   Scenario: A track without an instrument still plays through its strip
void instrumentless_track() {
    auto song = song_of(2);
    add_note(song, 0, 4000);
    song.tracks[0].instrument = clap_slot();
    song.tracks[0].mix.pan = -1.0;          // the instrument on the left
    song.tracks[1].mix.pan = 1.0;           // the bare track on the right
    song.tracks[1].mix.gain_db = -6.0;

    SongEngine engine;
    std::string error;
    engine.set_instrument(0, create_processor(song.tracks[0].instrument, {}, &error));
    require(engine.has_instrument(0) && !engine.has_instrument(1), "one instrument: " + error);
    float level = 0.5F;
    require(engine.prepare(song, rate, block, 0, &error), "prepare: " + error);
    require(engine::TestAccess::event_capacity(engine, 0) >= engine::maximum_events_per_chunk &&
                engine::TestAccess::event_capacity(engine, 1) >= engine::maximum_events_per_chunk,
            "prepare() sizes every track's event scratch");
    engine::TestAccess::set_test_source(engine, 1, &dc_source, &level);
    RtAudioOutput output(RtAudioOutput::Mode::deterministic);
    require(output.open(engine) && output.start(), "production output must start");

    const float expected = strip_gain(song.tracks[1].mix, false).right * level;
    require(expected > 0.1F && expected < level, "the strip scales the source");

    // Stopped, the arrangement contributes nothing: no clip, no source.
    auto stopped = pump(output, 1024);
    require(all_near(stopped.right, 0.0F), "a stopped song plays no clip region");

    engine.set_playing(true);
    auto playing = pump(output, 1024);
    require(all_near(playing.right, expected),
            "the bare track reaches the bus through its strip gain and pan");
    require(probe::peak(playing.left) > 0.2F, "the instrument track still plays beside it");
    require(std::abs(engine.track_peak(1) - expected) < 1e-4F,
            "the bare track's meter reads what it contributed");

    // Mute and solo act on the bare track's strip, proved from the bus.
    song.tracks[1].mix.mute = true;
    engine.apply_mix(song);
    require(all_near(pump(output, 512).right, 0.0F), "muting the bare track silences it");
    song.tracks[1].mix.mute = false;
    song.tracks[0].mix.solo = true;
    engine.apply_mix(song);
    require(all_near(pump(output, 512).right, 0.0F), "soloing another track silences it");
    song.tracks[0].mix.solo = false;
    song.tracks[1].mix.gain_db = 0.0;
    engine.apply_mix(song);
    require(all_near(pump(output, 512).right, strip_gain(song.tracks[1].mix, false).right * level),
            "its fader moves the bus");
}

// features/audio_reliability.feature:
//   Scenario: A processor's own parameter edits are stamped where they happened
void edit_ring() {
    auto song = song_of(1, 8192);
    SongEngine engine;
    engine.set_processor(track_instrument(0), std::make_unique<EditingInstrument>());
    std::string error;
    require(engine.prepare(song, rate, block, 0, &error), "prepare: " + error);

    // Stopped with the playhead resting at 1000.
    engine.seek(1000);
    std::vector<float> left(block), right(block);
    engine.process({left, right});
    PluginEditEvent edit;
    require(engine.take_plugin_edit(edit), "a stopped plugin's edit reaches the ring");
    require(edit.where == track_instrument(0) && !edit.rolling &&
                edit.song_sample == 1000 + EditingInstrument::offset &&
                edit.edit.parameter == 3 && edit.edit.value == 1.0,
            "a stopped edit is stamped where the playhead rests");
    require(!engine.take_plugin_edit(edit), "one edit per block");

    // Rolling: each block's edit lands at that block's song sample.
    engine.set_playing(true);
    for (std::uint64_t call = 0; call < 4; ++call) {
        engine.process({left, right});
        require(engine.take_plugin_edit(edit), "a rolling edit reaches the ring");
        require(edit.rolling && edit.song_sample == 1000 + call * block + EditingInstrument::offset,
                "a rolling edit is stamped with its song sample");
    }
    // Nobody draining: the ring fills and drops, it never blocks.
    for (int call = 0; call < 2000; ++call) engine.process({left, right});
    std::size_t queued = 0;
    while (engine.take_plugin_edit(edit)) ++queued;
    require(queued == 1024, "a full ring drops edits, keeping the oldest 1024");
}

// Builds `song` into a fresh engine, adopting `adopted`, and plays it through
// the production callback.
struct Built {
    std::unique_ptr<SongEngine> engine = std::make_unique<SongEngine>();
    std::unique_ptr<RtAudioOutput> output =
        std::make_unique<RtAudioOutput>(RtAudioOutput::Mode::deterministic);
    GraphBuild build;
};
Built build(const Song& song, std::vector<ReleasedProcessor> adopted) {
    Built built;
    built.build = populate_graph(*built.engine, song, std::move(adopted), {});
    std::string error;
    require(built.engine->prepare(song, rate, block, 0, &error), "prepare: " + error);
    load_fresh_state(*built.engine, song, built.build);
    require(built.output->open(*built.engine) && built.output->start(), "output must start");
    return built;
}

// Sets a track's level live (or leaves it) and plays one note; the loudest
// sample on the left of the bus, where the caller has muted every other track.
float level_heard(Built& built, std::size_t track, double level = -1.0) {
    if (level >= 0.0) play_level(*built.engine, track, level);
    else
        require(built.engine->play_live(track, {PluginEvent::Type::note_on, 0, 60, 1.0, 0.0}),
                "a note reaches the live queue");
    const auto heard = pump(*built.output, 512);
    (void)built.engine->play_live(track, {PluginEvent::Type::note_off, 0, 60, 0.0, 0.0});
    (void)pump(*built.output, 256);
    return probe::peak(heard.left);
}

// features/audio_reliability.feature:
//   Scenario: Rebuilding the graph keeps the instruments that did not change
void adoption() {
    auto song = song_of(2);
    song.tracks[0].instrument = clap_slot();
    song.tracks[1].instrument = clap_slot();
    song.tracks[1].mix.mute = true;   // only track 0 is measured on the bus
    const auto before = fixture_log();

    auto first = build(song, {});
    require(first.build.loaded == 2 && first.build.adopted == 0 && first.build.fresh.size() == 2,
            "a first build creates every processor: " + first.build.error);
    require(fixture_log().created == before.created + 2, "two fixture instances created");
    // The producer dials the level in.
    const float dialled = level_heard(first, 0, 0.6);
    require(std::abs(dialled - 0.6F * strip_gain(song.tracks[0].mix, false).left) < 1e-4F,
            "the dialled level is heard");
    auto* const kept = first.engine->processor(track_instrument(0));
    auto* const swapped = first.engine->processor(track_instrument(1));
    require(kept != nullptr && swapped != nullptr, "both processors are addressable");

    // Track 1 changes instrument; track 0 does not.
    const auto built_from = graph_signature(song);
    auto edited = song;
    edited.tracks[1].instrument = vst3_slot();
    const auto wanted = graph_signature(edited);
    require(!(built_from == wanted), "an instrument swap changes the signature");

    first.output->stop();
    const auto at_release = fixture_log();
    auto released = first.engine->release_processors();
    require(released.size() == 2 && first.engine->processor(track_instrument(0)) == nullptr,
            "release hands every processor back");
    auto adopted = adopt_processors(std::move(released), built_from, wanted, nullptr);
    require(adopted.size() == 1 && adopted.front().where == track_instrument(0),
            "only the unchanged processor is adopted");
    require(fixture_log().destroyed == at_release.destroyed + 1,
            "the replaced instrument is destroyed, on the control thread");
    first.output.reset();   // the old device and engine go
    first.engine.reset();

    auto second = build(edited, std::move(adopted));
    require(second.build.adopted == 1 && second.build.fresh.size() == 1 &&
                second.build.fresh.front() == track_instrument(1),
            "the rebuild adopts one and creates only the new one: " + second.build.error);
    require(second.engine->processor(track_instrument(0)) == kept,
            "the adopted instrument is the same instance");
    const auto after = fixture_log();
    require(after.created == at_release.created && after.destroyed == at_release.destroyed + 1,
            "the fixture log shows no instance created or destroyed for the adopted one");
    const float kept_level = level_heard(second, 0);
    require(std::abs(kept_level - dialled) < 1e-4F,
            "the adopted instrument keeps the level it was dialled to");

    // Without adoption the same song plays the fixture's default level, so
    // the kept level above is the instance's own and not a coincidence.
    auto fresh = build(edited, {});
    const float default_level = level_heard(fresh, 0);
    require(std::abs(default_level - 0.25F * strip_gain(edited.tracks[0].mix, false).left) < 1e-4F,
            "a fresh instance starts at the fixture's default level");
}

// features/audio_reliability.feature:
//   Scenario: A deleted track does not hand its instrument to its neighbour
void adoption_follows_remap() {
    auto song = song_of(3);
    for (auto& track : song.tracks) track.instrument = clap_slot();
    auto first = build(song, {});
    const double levels[] = {0.2, 0.4, 0.6};
    std::vector<PluginInstance*> instances;
    for (std::size_t track = 0; track < 3; ++track) {
        auto solo = song;
        for (std::size_t other = 0; other < 3; ++other) solo.tracks[other].mix.mute = other != track;
        first.engine->apply_mix(solo);
        (void)level_heard(first, track, levels[track]);
        instances.push_back(first.engine->processor(track_instrument(static_cast<std::uint32_t>(track))));
    }
    const auto built_from = graph_signature(song);
    auto edited = song;
    edited.tracks.erase(edited.tracks.begin());   // delete track 0
    const TrackRemap remap{std::nullopt, 0, 1};

    first.output->stop();
    auto adopted = adopt_processors(first.engine->release_processors(), built_from,
                                    graph_signature(edited), &remap);
    first.output.reset();
    first.engine.reset();
    auto second = build(edited, std::move(adopted));
    require(second.build.adopted == 2 && second.build.fresh.empty(), "both survivors adopted");
    require(second.engine->processor(track_instrument(0)) == instances[1] &&
                second.engine->processor(track_instrument(1)) == instances[2],
            "each surviving track keeps its own instance");
    for (std::size_t track = 0; track < 2; ++track) {
        auto solo = edited;
        for (std::size_t other = 0; other < 2; ++other) solo.tracks[other].mix.mute = other != track;
        second.engine->apply_mix(solo);
        const float heard = level_heard(second, track);
        const float wanted = static_cast<float>(levels[track + 1]) *
                             strip_gain(solo.tracks[track].mix, false).left;
        require(std::abs(heard - wanted) < 1e-4F,
                "track " + std::to_string(track) + " plays its own dialled level");
    }
}

// features/audio_reliability.feature:
//   Scenario: A running plugin is not sent a state it cannot take while running
void running_state() {
    auto song = song_of(4);
    song.tracks[0].instrument = clap_slot();
    song.tracks[1].instrument = vst3_slot();
    song.tracks[2].instrument = soundfont_slot();
    SongEngine engine;
    const auto build = populate_graph(engine, song, {}, {});
    require(build.loaded == 3, "the CLAP, VST3 and SoundFont processors load: " + build.error);
    engine.set_processor(track_instrument(3), std::make_unique<EditingInstrument>(true));
    std::string error;
    require(engine.prepare(song, rate, block, 0, &error), "prepare: " + error);
    for (std::uint32_t track = 0; track < 3; ++track) {
        auto* processor = engine.processor(track_instrument(track));
        require(processor != nullptr, "the processor is addressable");
        const auto state = processor->save_state();
        require(!engine.update_processor_state(track_instrument(track), state),
                processor->format() + " does not take state while running");
    }
    require(!engine.update_processor_state(track_instrument(9), {}),
            "an empty address takes nothing");
    require(!engine.update_processor_state({BusKind::master, 0, 0}, {}),
            "an address the engine cannot hold yet takes nothing");
    const std::vector<std::byte> pushed{std::byte{1}, std::byte{2}};
    require(engine.update_processor_state(track_instrument(3), pushed) &&
                engine.processor(track_instrument(3))->save_state() == pushed,
            "a processor that accepts running state is given it");
}

// features/audio_reliability.feature:
//   Scenario: The instrument a slot names is created by one factory
void processor_factory() {
    std::string error;
    require(is_internal_format("SoundFont") && !is_internal_format("CLAP"),
            "the SoundFont player is registered as an internal processor");
    auto synth = create_processor(soundfont_slot(), {}, &error);
    require(synth != nullptr && synth->format() == "SoundFont", "SoundFont created: " + error);
    // A real SF2 through FluidSynth: a note is non-silent audio.
    SongEngine engine;
    auto song = song_of(1);
    engine.set_instrument(0, std::move(synth));
    require(engine.prepare(song, rate, block, 0, &error), "prepare: " + error);
    RtAudioOutput output(RtAudioOutput::Mode::deterministic);
    require(output.open(engine) && output.start(), "output must start");
    require(engine.play_live(0, {PluginEvent::Type::note_on, 0, 60, 1.0, 0.0}), "note queued");
    const auto heard = pump(output, 4096);
    require(probe::rms(heard.left) > 1e-3 && probe::rms(heard.right) > 1e-3,
            "the factory's SoundFont plays a note as audio");

    require(create_processor(clap_slot(), {}, &error) != nullptr, "CLAP through its adapter");
    require(create_processor(vst3_slot(), {}, &error) != nullptr, "VST3 through its adapter");

    error.clear();
    require(create_processor({"Nothing", "", "", {}}, {}, &error) == nullptr && !error.empty(),
            "an unknown format is refused with a reason");

    // An internal processor registers itself and its browser entries.
    int made = 0;
    register_internal(
        "Seam Test",
        [&made](const InstrumentSlot&, const ProcessorContext& context, std::string*) {
            ++made;
            require(context.project_dir == "/projects/demo", "the context reaches the factory");
            return std::unique_ptr<PluginInstance>(std::make_unique<EditingInstrument>());
        },
        {{"Seam Tester", "instrument", {"Seam Test", "", "", {}}}});
    ProcessorContext context;
    context.project_dir = "/projects/demo";
    auto made_one = create_processor({"Seam Test", "", "", {}}, context, &error);
    require(made_one != nullptr && made == 1, "a registered factory creates its format");
    const auto catalog = internal_catalog();
    require(std::any_of(catalog.begin(), catalog.end(),
                        [](const CatalogEntry& entry) {
                            return entry.name == "Seam Tester" && entry.kind == "instrument" &&
                                   entry.slot.format == "Seam Test";
                        }),
            "its catalog entries are listed");
}

void graph_signature_rules() {
    auto song = song_of(2);
    song.tracks[0].instrument = clap_slot();
    const auto base = graph_signature(song);
    require(base.processors.size() == 2 && base.returns == 0, "one address per track");
    require(base.at(track_instrument(0)) != nullptr &&
                *base.at(track_instrument(0)) == identity_of(clap_slot()) &&
                base.at(track_instrument(1))->empty(),
            "each address names what sits there");

    auto played = song;
    played.tracks[0].instrument.state = {std::byte{9}};
    played.tracks[1].mix.gain_db = -12.0;
    add_note(played, 0, 10);
    require(graph_signature(played) == base, "state, mix and notes leave the graph alone");

    auto swapped = song;
    swapped.tracks[1].instrument = soundfont_slot();
    require(!(graph_signature(swapped) == base), "an instrument on a bare track changes it");
    auto other_id = song;
    other_id.tracks[0].instrument.identifier = "dev.blokkily.other";
    require(!(graph_signature(other_id) == base), "another plugin in the same file changes it");
    auto wider = song;
    wider.tracks.push_back(Track{});
    require(!(graph_signature(wider) == base), "a track added changes it");
    auto with_return = song;
    with_return.returns.push_back(ReturnBus{});
    require(graph_signature(with_return).returns == 1 && !(graph_signature(with_return) == base),
            "a return bus added changes it");
}

// A turn of an event loop, stood in for by a queue the test runs.
struct Turns {
    std::deque<std::function<void()>> queued;
    RecompileCoalescer::Defer defer() {
        return [this](std::function<void()> work) { queued.push_back(std::move(work)); };
    }
    void run_one() {
        auto now = std::move(queued);
        queued.clear();
        for (auto& work : now) work();
    }
};

// features/audio_reliability.feature:
//   Scenario: A burst of edits is one recompile of the latest song
void coalescing() {
    auto song = song_of(1, 2048);
    song.tracks[0].instrument = clap_slot();
    add_note(song, 10, 5);
    auto built = build(song, {});
    built.engine->set_playing(true);
    require(probe::first_nonzero(pump(*built.output, 2048).left, 1e-3F) == std::size_t{10},
            "the song plays its first version");

    Turns turns;
    int compiled = 0;
    RecompileCoalescer recompiler(
        [&]() -> RecompileCoalescer::Outcome {
            ++compiled;
            std::string error;
            if (built.engine->recompile(song, 0, &error)) return {true, {}};
            return {false, error};
        },
        turns.defer());

    // Fifty edits in one turn, each moving the note, each asking to recompile.
    for (int edit = 0; edit < 50; ++edit) {
        song.patterns[0].pattern = Pattern(2048, ticks_per_beat);
        add_note(song, 20 * edit + 30, 5);
        recompiler.request();
    }
    require(compiled == 0 && turns.queued.size() == 1 && recompiler.pending(),
            "requests in one turn schedule one recompile and run none");
    turns.run_one();
    require(compiled == 1 && recompiler.recompiles() == 1 && !recompiler.pending() &&
                turns.queued.empty(),
            "the turn runs exactly one recompile");
    // The engine picks the arrangement up at its next block; from the loop
    // point on, it plays the last edit and nothing else.
    built.engine->seek(0);
    const auto heard = pump(*built.output, 2048).left;
    require(probe::first_nonzero(heard, 1e-3F) == std::size_t{20 * 49 + 30} &&
                probe::rising_edges(heard, 0.1F) == 1,
            "the engine converges on the latest song");

    // The next turn's requests are another recompile.
    song.patterns[0].pattern = Pattern(2048, ticks_per_beat);
    add_note(song, 1500, 5);
    recompiler.request();
    recompiler.request();
    turns.run_one();
    require(compiled == 2, "a later turn recompiles again, once");
    built.engine->seek(0);
    require(probe::first_nonzero(pump(*built.output, 2048).left, 1e-3F) == std::size_t{1500},
            "and plays that edit");

    // A flush runs what is owed at once, and the scheduled turn then finds
    // nothing left to do.
    add_note(song, 100, 5);
    recompiler.request();
    recompiler.flush();
    require(compiled == 3 && !recompiler.pending(), "a flush recompiles immediately");
    turns.run_one();
    require(compiled == 3, "the turn after a flush has nothing owed");
}

// features/audio_reliability.feature:
//   Scenario: A busy engine is asked again rather than losing the edit
void coalescing_retry() {
    Turns turns;
    int attempts = 0;
    int refusals = 3;
    std::string refusal = RecompileCoalescer::busy_reason;
    RecompileCoalescer recompiler(
        [&]() -> RecompileCoalescer::Outcome {
            ++attempts;
            if (refusals > 0) {
                --refusals;
                return {false, refusal};
            }
            return {true, {}};
        },
        turns.defer());
    recompiler.request();
    for (int turn = 0; turn < 3; ++turn) {
        turns.run_one();
        require(recompiler.pending() && turns.queued.size() == 1 &&
                    recompiler.last_error() == RecompileCoalescer::busy_reason,
                "a busy refusal is retried on the next turn");
    }
    turns.run_one();
    require(attempts == 4 && !recompiler.pending() && turns.queued.empty() &&
                recompiler.last_error().empty(),
            "the retry converges once the engine has a free slot");

    // Any other refusal will not clear by itself and is not retried.
    refusals = 1;
    refusal = "song has no length";
    recompiler.request();
    turns.run_one();
    require(attempts == 5 && !recompiler.pending() && turns.queued.empty() &&
                recompiler.last_error() == "song has no length",
            "a refusal other than busy is reported, not retried");

    // A busy engine that never frees up is given up on, not retried for ever.
    refusals = 1000;
    refusal = RecompileCoalescer::busy_reason;
    recompiler.request();
    int turns_run = 0;
    while (!turns.queued.empty() && turns_run < 1000) {
        turns.run_one();
        ++turns_run;
    }
    require(turns_run == RecompileCoalescer::maximum_retries + 1 && !recompiler.pending(),
            "retries are bounded");
}

} // namespace

int main(int argc, char** argv) {
    const std::map<std::string, void (*)()> cases{
        {"instrumentless_track", instrumentless_track},
        {"edit_ring", edit_ring},
        {"adoption", adoption},
        {"adoption_follows_remap", adoption_follows_remap},
        {"running_state", running_state},
        {"processor_factory", processor_factory},
        {"graph_signature", graph_signature_rules},
        {"coalescing", coalescing},
        {"coalescing_retry", coalescing_retry},
    };
    if (argc != 2 || !cases.contains(argv[1])) {
        std::cerr << "usage: blokkily_engine_seams_tests <case>\n";
        return 2;
    }
    try {
        cases.at(argv[1])();
    } catch (const std::exception& failure) {
        std::cerr << argv[1] << ": " << failure.what() << '\n';
        return 1;
    }
    std::cout << argv[1] << ": ok\n";
    return 0;
}
