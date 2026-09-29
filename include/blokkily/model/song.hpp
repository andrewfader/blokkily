#pragma once

#include "blokkily/model/metronome.hpp"
#include "blokkily/model/modulation.hpp"
#include "blokkily/model/pattern.hpp"
#include "blokkily/model/processor_address.hpp"
#include "blokkily/model/scale.hpp"
#include "blokkily/model/scene_launcher.hpp"
#include "blokkily/model/timebase.hpp"
#include "blokkily/model/tuning.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace blokkily {

// One instrument the project expects to reconstruct on load. The state blob is
// whatever the adapter's save_state() produced, kept opaque here so that
// format-specific details stay behind the plugin adapters.
struct InstrumentSlot {
    std::string format;     // "CLAP", "VST3", or "SoundFont"
    std::string path;       // module, bundle, or SF2/SF3 file
    std::string identifier; // CLAP plugin id or VST3 identifier; may be empty
    std::vector<std::byte> state;

    friend bool operator==(const InstrumentSlot&, const InstrumentSlot&) = default;
};

// An effect is loaded exactly like an instrument, so it is described the same
// way (plan §F-E).
using PluginSlot = InstrumentSlot;

// A channel strip. Gain is in decibels because that is the unit a mixer is
// operated in; the engine converts once, at the boundary.
struct MixerStrip {
    double gain_db = 0.0;
    double pan = 0.0; // -1 hard left, 0 centre, +1 hard right
    bool mute = false;
    bool solo = false;

    friend bool operator==(const MixerStrip&, const MixerStrip&) = default;
};

// --- Effects ---------------------------------------------------------------

// One saved parameter value of an effect, in the plugin's own units. Plugins
// that keep their settings in their state blob leave the list empty; built-in
// effects, which have no blob, store their settings here.
struct EffectParameter {
    std::int32_t id = 0;
    double value = 0.0;

    friend bool operator==(const EffectParameter&, const EffectParameter&) = default;
};

// One insert on a track, a return bus, or the master bus. Inserts run in
// vector order. A bypassed slot keeps its plugin loaded but passes audio
// through untouched. `sidechain` names the track whose signal keys the effect
// (wave 5.2): that track's audio after its own inserts and before its fader,
// so muting or pulling down the key track never stops the ducking. A track's
// insert cannot key from its own track, and keys may not form a loop.
struct EffectSlot {
    PluginSlot plugin;
    bool bypass = false;
    std::vector<EffectParameter> parameters; // ids unique within the slot
    std::optional<std::uint32_t> sidechain{};

    friend bool operator==(const EffectSlot&, const EffectSlot&) = default;
};

// A track's send to a return bus (`bus` indexes Song::returns). Post-fader
// sends are taken after the strip gain and before pan (decision 10); pre-fader
// sends are taken after the insert chain and before the strip gain. A track
// sends to each return at most once.
struct Send {
    std::size_t bus = 0;
    double level_db = 0.0;
    bool pre_fader = false;

    friend bool operator==(const Send&, const Send&) = default;
};

// A return bus: effects fed by the tracks' sends, mixed into the master bus.
// Returns are solo-safe (decision 10): soloing a track never silences them.
struct ReturnBus {
    std::string name = "Return";
    std::vector<EffectSlot> inserts;
    MixerStrip mix;

    friend bool operator==(const ReturnBus&, const ReturnBus&) = default;
};

// --- Recording input --------------------------------------------------------

// What a track records and monitors. One R button (`armed`) arms notes and
// audio together (decision 3). Arm and input state are saved with the project
// but are not part of undo (decision 5).
struct TrackInput {
    enum class Source : std::uint8_t { none, midi, audio, midi_and_audio };
    enum class Monitor : std::uint8_t { off, automatic, on }; // automatic: while armed

    bool armed = false;
    Source source = Source::midi;
    std::int8_t midi_channel = -1;         // -1 = every channel, else 0..15
    std::uint16_t audio_first_channel = 0; // first device input channel, 0-based
    std::uint8_t audio_channels = 2;       // 1 = mono, 2 = stereo pair
    Monitor monitor = Monitor::automatic;

    friend bool operator==(const TrackInput&, const TrackInput&) = default;
};

// --- Automation -------------------------------------------------------------

// What an automation lane drives. `gain` (dB), `pan` (-1..+1) and `mute` (0
// heard, 1 muted; 0.5 and above mutes) drive the strip of the bus `processor`
// names, whose slot must then be -1. `parameter`
// drives one parameter of the processor at `processor`: a track's instrument
// (slot -1) or an insert on any bus. The index and id say which parameter, the
// same way a ParameterLock does. Parameter values are in the plugin's units.
struct AutomationTarget {
    enum class Kind : std::uint8_t { parameter, gain, pan, mute };

    Kind kind = Kind::parameter;
    std::int32_t parameter_index = 0;
    std::string parameter_id;
    ProcessorAddress processor{};

    friend bool operator==(const AutomationTarget&, const AutomationTarget&) = default;
};

struct AutomationPoint {
    Tick at = 0;
    double value = 0.0;

    friend bool operator==(const AutomationPoint&, const AutomationPoint&) = default;
};

// How a track's lanes behave while the transport plays (decision 3). Read
// plays the lanes back; touch writes while a control is held and returns to
// the lane on release; latch keeps writing the last value until the transport
// stops; write overwrites everything the playhead crosses; off ignores the
// lanes and leaves the static mixer and parameter values in charge.
enum class AutomationMode : std::uint8_t { off, read, touch, latch, write };

// A breakpoint envelope. Points are sorted by tick and ramp linearly between
// neighbours. Two points may share a tick, which makes a jump: the first is
// the value arriving at the tick, the second the value leaving it. No tick
// holds more than two points. Before the first point the lane holds the first
// value; after the last it holds the last value.
struct AutomationLane {
    AutomationTarget target;
    std::vector<AutomationPoint> points;

    // The lane's value at `at` (the leaving value at a jump), or nothing when
    // the lane has no points.
    [[nodiscard]] std::optional<double> value_at(Tick at) const;
    // Replaces the envelope over [from, to] with a recorded pass (points
    // sorted by tick, all inside [from, to]) and keeps everything outside
    // unchanged: the old envelope runs up to `from` and resumes after `to`,
    // with a jump wherever the pass does not meet it. An empty pass, a pass
    // with a point outside the range or out of order, or to < from changes
    // nothing and returns false.
    bool write_pass(Tick from, Tick to, std::span<const AutomationPoint> pass);
    // Removes points the envelope does not need. A point goes when the ramp
    // between the points kept on either side passes within `tolerance` of it
    // and of every other point dropped between them. Jumps and the two end
    // points always stay.
    void thin(double tolerance);
    // Sorted, at most two points per tick, every value finite.
    [[nodiscard]] bool well_formed() const;

    friend bool operator==(const AutomationLane&, const AutomationLane&) = default;
};

struct Track {
    std::string name = "Track";
    InstrumentSlot instrument;
    MixerStrip mix;
    std::vector<EffectSlot> inserts;
    std::vector<Send> sends;
    TrackInput input;
    std::vector<AutomationLane> automation;
    AutomationMode automation_mode = AutomationMode::read;
};

struct PatternSlot {
    std::string name = "Pattern";
    Pattern pattern{1920, 480};
};

// One placement of a pattern on one track's timeline. `repeats` is how many
// times the pattern runs back to back from `start`; each repetition counts as
// the next loop, so probability and loop conditions keep working in a song.
struct Clip {
    std::size_t track = 0;
    std::size_t pattern = 0;
    Tick start = 0;
    std::uint32_t repeats = 1;
};

// --- Audio ------------------------------------------------------------------

// An audio file the song plays from, with what it looked like when it was
// imported or recorded, so a file that has since changed is reported missing
// rather than played wrong.
struct AudioFileRef {
    std::filesystem::path path; // absolute in memory; stored project-relative
    std::uint64_t frames = 0;
    std::uint32_t sample_rate = 0;
    std::uint16_t channels = 0;

    friend bool operator==(const AudioFileRef&, const AudioFileRef&) = default;
};

using AudioClipId = std::uint64_t;

// How an audio clip is stretched and pitched (item 3.6, decision 7). The
// default changes nothing: the clip plays its file's frames at the file's
// speed and pitch.
//
// With `follow_tempo` on and a `source_bpm` known, the clip follows the
// song's tempo map: a beat of the recording (60 / source_bpm seconds of it,
// counted from the clip's first frame) lasts `ratio` beats of the song,
// across tempo steps and ramps, without its pitch changing. Otherwise
// `ratio` is a plain stretch: the clip lasts `ratio` times as long as its
// frames do, whatever the tempo. `semitones` and `cents` shift the pitch
// without changing the length.
//
// A warped clip does not play its file directly: it plays a rendition of it
// rendered off the audio thread (clip_warp.hpp).
struct ClipWarp {
    bool follow_tempo = false;
    double source_bpm = 0.0;   // 0: not known yet
    double ratio = 1.0;
    std::int32_t semitones = 0;
    double cents = 0.0;

    static constexpr double minimum_ratio = 0.25;
    static constexpr double maximum_ratio = 4.0;
    static constexpr std::int32_t maximum_semitones = 24;
    static constexpr double maximum_cents = 100.0;
    static constexpr double minimum_source_bpm = 20.0;
    static constexpr double maximum_source_bpm = 400.0;

    // Following the tempo map: switched on and with a tempo to follow from.
    [[nodiscard]] bool follows() const noexcept { return follow_tempo && source_bpm > 0.0; }
    // True when the clip plays a rendition rather than its file.
    [[nodiscard]] bool active() const noexcept {
        return follows() || ratio != 1.0 || semitones != 0 || cents != 0.0;
    }
    // The frequency factor of the pitch shift: 2 for +12 semitones.
    [[nodiscard]] double pitch_scale() const noexcept;
    // Every value finite and inside its range.
    [[nodiscard]] bool valid() const noexcept;

    friend bool operator==(const ClipWarp&, const ClipWarp&) = default;
};

// One placement of a stretch of an audio file on a track. `id` is a stable
// identity (never 0, unique in the song) so a drag keeps hold of its clip
// while other clips are added or removed. Frames count at the file's own
// sample rate. Clips on one track may overlap, and overlapping clips sum
// (decision 6).
struct AudioClip {
    AudioClipId id = 0;
    std::size_t track = 0;
    std::size_t file = 0; // indexes Song::audio_files
    Tick start = 0;
    std::uint64_t offset_frames = 0; // first frame of the file that plays
    std::uint64_t length_frames = 0;
    double gain_db = 0.0;
    std::uint64_t fade_in_frames = 0;
    std::uint64_t fade_out_frames = 0;
    // Stretch and pitch (item 3.6). The frames above always count the file's
    // own frames at its own rate, warped or not.
    ClipWarp warp;

    friend bool operator==(const AudioClip&, const AudioClip&) = default;
};

// Where an audio clip's audio sounds in the song, warp included. `seconds`
// counts the clip's own audio at its file's speed from its first played frame
// (offset_frames). The tick is fractional. Seconds beyond the clip are
// extrapolated with the same rule, so trimming can ask where a hidden frame
// would sound.
[[nodiscard]] double audio_clip_tick_at(const Song& song, const AudioClip& clip, double seconds);
// The inverse: how many seconds of the clip's audio have sounded by `tick`
// (negative before the clip's start).
[[nodiscard]] double audio_clip_seconds_at(const Song& song, const AudioClip& clip, double tick);

// The arrangement: named patterns, tracks that play them, and the clips that
// say when. A song with one track, one pattern, and one clip is the pattern
// sequencer this grew out of, so nothing has to opt in to the timeline.
struct Song {
    std::vector<PatternSlot> patterns{PatternSlot{}};
    std::vector<Track> tracks{Track{}};
    std::vector<Clip> clips{Clip{}};
    double master_gain_db = 0.0;
    // The tuning and scale the song is written in. Editors, keyboards, and the
    // engine all read these, so nothing can be in a different key from the
    // notes it is editing.
    Tuning tuning{};
    Scale scale{};
    int root_degree = 60;
    // With auto-scale on, a note written anywhere lands on the nearest degree
    // of the scale rather than between its notes.
    bool auto_scale = false;
    std::vector<AudioFileRef> audio_files;
    std::vector<AudioClip> audio_clips;
    std::vector<ReturnBus> returns;
    std::vector<EffectSlot> master_inserts;
    // Added to the measured device round trip when a recording is placed
    // (plan C21): the correction for a device that misreports its latency.
    std::int64_t record_offset_samples = 0;
    // When each tick sounds, and how ticks group into bars (plan F-A). The
    // only tempo the song has: the engine, the transport and recording all
    // read these.
    TempoMap tempo;
    MeterMap meter;
    // The click and the count-in (item 3.7): session settings saved with the
    // song, kept as they are by undo.
    MetronomeSettings metronome;
    // The non-linear clip / scene launcher matrix (Item 5).
    SceneMatrix launcher;
    // LFOs, macros and envelope followers, and the parameters each moves
    // (modulation.hpp). At most maximum_modulators.
    std::vector<Modulator> modulators;

    [[nodiscard]] Pattern& pattern(std::size_t index = 0) { return patterns.at(index).pattern; }
    [[nodiscard]] const Pattern& pattern(std::size_t index = 0) const {
        return patterns.at(index).pattern;
    }
    // Ticks from the start of the song to the end of its last clip. An audio
    // clip ends where its last frame falls under the tempo map, found in
    // seconds so the song's length needs no sample rate. A song whose clips
    // are all empty still lasts one pattern, so transport has somewhere to run.
    [[nodiscard]] Tick length() const;
    // The resolution the song's ticks are counted at: the first pattern's.
    [[nodiscard]] Tick ticks_per_beat() const;
    [[nodiscard]] bool any_solo() const;
    // True when every cross-reference resolves and every value is in range:
    // clips name existing tracks and patterns; audio clips have unique non-zero
    // ids, name an existing track and file, stay inside the file and fit their
    // fades; sends name existing returns, once each; effect slots name a
    // plugin format; inputs and automation lanes are well formed and every
    // lane's target names a processor that exists; every insert's sidechain
    // names another track and the keys form no loop; every modulator is in
    // range, a follower follows a track that exists, and each modulation
    // target names a processor that exists, once per modulator. Overlapping
    // audio clips are allowed. When it returns false and `why` is given, `why` says which rule
    // failed.
    [[nodiscard]] bool consistent(std::string* why = nullptr) const;
    // Removes track `index` with everything that belongs to it (its clips, its
    // audio clips, and automation lanes elsewhere that target its strip or its
    // processors) and re-indexes what remains. Returns the old-to-new index of
    // every track (nothing for the removed one), or an empty vector, changing
    // nothing, when `index` does not exist or is the only track.
    std::vector<std::optional<std::size_t>> remove_track(std::size_t index);
    // Removes insert `slot` of the bus `kind`/`bus` names, with the automation
    // lanes and modulation targets that drive it; lanes and targets on the
    // inserts after it follow them to their new slots. False, changing
    // nothing, when there is no such insert.
    bool remove_insert(BusKind kind, std::size_t bus, std::size_t slot);
    // Removes return `bus`: the sends into it, and the automation lanes and
    // modulation targets on its inserts; later returns move down one.
    bool remove_return(std::size_t bus);
    // Drops audio files no audio clip plays and re-indexes the clips. Returns
    // the old-to-new index of every file. Run on save only, so that undo can
    // bring back a clip whose file is still listed.
    std::vector<std::optional<std::size_t>> prune_audio_files();
    // An id no audio clip in the song uses yet.
    [[nodiscard]] AudioClipId next_audio_clip_id() const;
    // Compiles one track's whole timeline into song-absolute ticks, expanding
    // every clip repetition. Returns nothing the track cannot play.
    [[nodiscard]] ScheduledEvents arrange(std::size_t track, std::uint64_t seed = 0) const;
};

} // namespace blokkily
