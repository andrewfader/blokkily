#pragma once

// DAWproject (github.com/bitwig/dawproject, version 1.0): the open exchange
// format Bitwig Studio, Studio One, Cubase and others read. A .dawproject is
// a ZIP holding project.xml, metadata.xml, the plugins' states and the audio
// the song plays.
//
// A song leaves as what the engine plays. Each track's notes, controller
// movements, per-note expression and step locks are those Song::arrange
// compiles for the render callback and the bounce, grouped into one clip per
// placement of a part. The mixer, the tempo map, the meters and the
// automation lanes go to their DAWproject counterparts. What the format has
// no place for is listed in the report, never silently dropped.

#include "blokkily/model/song.hpp"
#include "blokkily/plugins/plugin.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace blokkily {

struct DawProjectOptions {
    // What each processor is as another host loads it, from the running
    // instances (PluginInstance::portable_state). A plugin with no entry is
    // written without its state, and the report says so.
    std::vector<std::pair<ProcessorAddress, PortableState>> states;
    std::string application_version = "0";
    std::string title;
};

struct DawProjectReport {
    std::size_t tracks = 0;
    std::size_t clips = 0;
    std::size_t notes = 0;
    std::size_t automation_points = 0;
    std::size_t plugin_states = 0;
    std::size_t audio_files = 0;
    // One line for each thing of the song the file does not carry.
    std::vector<std::string> omitted;
};

[[nodiscard]] std::optional<DawProjectReport> write_dawproject(
    const std::filesystem::path& path, const Song& song, const DawProjectOptions& options = {},
    std::string* error = nullptr);

// --- Reading a .dawproject back ---------------------------------------------
// Enough of the format to check what an export holds; times in quarter notes.

struct DawProjectPoint {
    double time = 0.0;
    double value = 0.0;
    std::string interpolation; // "hold", "linear" or empty
    // A TimeSignaturePoint's meter; 0 for any other point.
    int numerator = 0;
    int denominator = 0;
};

struct DawProjectPoints {
    std::string target_parameter;  // the id of the parameter moved, or empty
    std::string expression;        // pitchBend, channelController, transpose...
    int controller = -1;
    int key = -1;
    std::string unit;
    std::vector<DawProjectPoint> points;
};

struct DawProjectNote {
    double time = 0.0;
    double duration = 0.0;
    int channel = 0;
    int key = 60;
    double velocity = 0.0;
    std::vector<DawProjectPoints> expressions; // times relative to the note
};

struct DawProjectWarp {
    double time = 0.0;
    double content_time = 0.0;
};

struct DawProjectClip {
    double time = 0.0;
    double duration = 0.0;
    std::string name;
    bool enabled = true;
    std::vector<DawProjectNote> notes;   // times relative to the clip
    std::vector<DawProjectPoints> points;
    // An audio clip's file, its length in seconds and its warp markers.
    std::string audio_path;
    double audio_seconds = 0.0;
    int audio_channels = 0;
    int audio_sample_rate = 0;
    double fade_in = 0.0;
    double fade_out = 0.0;
    std::vector<DawProjectWarp> warps;
};

struct DawProjectParameter {
    std::string id;
    std::string name;
    std::int64_t parameter_id = -1;
    double value = 0.0;
    double min = 0.0;
    double max = 1.0;
};

struct DawProjectDevice {
    std::string element; // ClapPlugin, Vst3Plugin, Device, BuiltinDevice...
    std::string id;
    std::string device_id;
    std::string device_name;
    std::string device_role;
    std::string state_path;
    std::vector<std::byte> state; // the file state_path names, read from the archive
    std::vector<DawProjectParameter> parameters;
};

struct DawProjectSend {
    std::string destination;
    double volume = 0.0; // linear
    std::string type;
};

struct DawProjectChannel {
    std::string id;
    std::string role;
    std::string destination;
    std::string volume_id, pan_id, mute_id;
    double volume = 1.0; // linear
    double pan = 0.5;    // normalized: 0 left, 1 right
    bool mute = false;
    bool solo = false;
    std::vector<DawProjectDevice> devices;
    std::vector<DawProjectSend> sends;
};

struct DawProjectTrack {
    std::string id;
    std::string name;
    std::string content_type;
    DawProjectChannel channel;
    std::vector<DawProjectClip> clips;
    std::vector<DawProjectPoints> automation;
};

struct DawProjectFile {
    std::string application;
    std::string tempo_id;
    double tempo = 120.0;
    int numerator = 4;
    int denominator = 4;
    std::vector<DawProjectPoint> tempo_automation;
    std::vector<DawProjectPoint> meter_automation;
    std::vector<DawProjectTrack> tracks; // in Structure order; the master last
    std::string title;
    std::string comment;
    std::vector<std::string> entries; // every file in the archive
    std::string project_xml;

    [[nodiscard]] const DawProjectTrack* track(const std::string& name) const;
    [[nodiscard]] const DawProjectTrack* track_of_channel(const std::string& channel_id) const;
};

[[nodiscard]] std::optional<DawProjectFile> read_dawproject(const std::filesystem::path& path,
                                                            std::string* error = nullptr);

// --- Opening a .dawproject as a song (features/dawproject.feature) ---------
// What another host wrote, as Blokkily plays it: the tempo and meter maps,
// each track's clips as sections and placements, notes with their velocity,
// retune and per-note expression, controller movements, the instruments and
// effects the device lists name (found among the installed plugins), the
// mixer, returns and sends, audio clips with their files, and the strip and
// parameter automation. Clips of the same name and length become one
// section, a part per track; a placement whose notes differ becomes a section
// of its own. What the song has no place for is named in the report.

// A placeholder device's own description (format, path, identifier, state),
// written beside it so a Blokkily file opened again loads it.
[[nodiscard]] std::vector<std::byte> dawproject_slot_bytes(const PluginSlot& slot);
[[nodiscard]] std::optional<PluginSlot> dawproject_slot(std::span<const std::byte> bytes);

// The installed plugin a ClapPlugin or Vst3Plugin device names (its deviceID:
// a CLAP id, a VST3 class id), as the slot that loads it, or nothing.
using DawProjectLocator = std::function<std::optional<PluginSlot>(const DawProjectDevice&)>;

struct DawProjectImport {
    Song song;
    // One line for each thing of the file the song does not carry.
    std::vector<std::string> omitted;
    // VST3 states arrive as .vstpreset files: the caller loads each into its
    // plugin and keeps the state the plugin then reports (the slot at
    // `where` has none until then).
    struct Preset {
        ProcessorAddress where;
        std::vector<std::byte> preset;
    };
    std::vector<Preset> presets;
    std::size_t tracks = 0;
    std::size_t clips = 0;
    std::size_t notes = 0;
    std::size_t devices = 0;
    std::size_t audio_clips = 0;
    std::size_t automation_points = 0;
};

// Reads `path` into a song. Audio files the archive holds are written into
// `audio_dir`, which the song's audio clips then name.
[[nodiscard]] std::optional<DawProjectImport> import_dawproject(
    const std::filesystem::path& path, const std::filesystem::path& audio_dir,
    const DawProjectLocator& locate, std::string* error = nullptr);

} // namespace blokkily

