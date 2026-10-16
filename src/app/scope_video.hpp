#pragma once

// An oscilloscope video of a song, in the manner of corrscope: one lane per
// track and a wide master lane beneath them, each showing its own waveform
// held steady by a correlation trigger, with the master mix as the sound.
// Frames are drawn with QPainter and piped to ffmpeg, which encodes the
// picture (H.264) and the audio (AAC) into one file. Control thread only:
// it allocates, starts a process and writes files.

#include <QColor>
#include <QImage>
#include <QString>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace blokkily {

struct ScopeLane {
    QString name;
    QColor color;
    std::vector<float> samples; // mono, at the video's sample rate
    // What a sample is multiplied by before it is drawn; render_scope_video
    // fits it to the lane's peak so a quiet part still fills its lane.
    float scale = 1.0F;
};

struct ScopeVideoOptions {
    int width = 1280;
    int height = 720;
    int fps = 30;
    // How much of each waveform a lane shows, in seconds.
    double window_seconds = 0.04;
};

struct ScopeVideoReport {
    int frames = 0;
    double seconds = 0.0;
};

// The ffmpeg the export runs: $BLOKKILY_FFMPEG when set, else the first on
// PATH. Empty when there is none.
[[nodiscard]] QString find_ffmpeg();

// The sample a lane's frame is centred on: a rising zero crossing near
// `centre` whose window best matches `previous` (the samples the lane showed
// in the frame before), so a periodic wave stands still instead of
// scrolling. `centre` itself when there is no crossing. `previous` empty
// picks the crossing nearest `centre`.
[[nodiscard]] std::int64_t scope_trigger(const std::vector<float>& samples, std::int64_t centre,
                                         int window, const std::vector<float>& previous);

// Draws one frame, at `time_samples`, of the lanes over the master. `held`
// carries each lane's last window (lanes, then master) from frame to frame.
void draw_scope_frame(QImage& image, const std::vector<ScopeLane>& lanes,
                      const ScopeLane& master, std::int64_t time_samples, int window,
                      std::vector<std::vector<float>>& held);

// Renders the video to `output`, with `audio` (the master bounce, a file
// ffmpeg reads) as its sound. Lanes and master must hold the same sample
// rate as `audio`.
[[nodiscard]] std::optional<ScopeVideoReport> render_scope_video(
    const QString& ffmpeg, const std::filesystem::path& output, const std::filesystem::path& audio,
    double sample_rate, std::vector<ScopeLane> lanes, ScopeLane master,
    const ScopeVideoOptions& options,
    const std::function<void(int frame, int frames)>& progress, std::string* error);

// The colour a track's lane is drawn in.
[[nodiscard]] QColor scope_lane_color(std::size_t track);

} // namespace blokkily
