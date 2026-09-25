// A track's audio input (plan item 3.2; decisions 3, 5 and 8). The strip's input
// chip says what the track records - MIDI, or a pair or a single input of the
// audio device - and its monitor chip whether the input is heard through the
// track. Like the arm, both are how the session is wired rather than the
// music: saved with the song, never a step of history.

#include "song_model.hpp"

#include "blokkily/audio/audio_input.hpp"

#include <QtGlobal>

#include <algorithm>
#include <vector>

namespace {

// One choice of the input chip: MIDI (no audio), or `channels` device inputs
// from `first`.
struct InputChoice {
    int first = 0;
    int channels = 0;
};

// MIDI, then each pair followed by its two inputs alone.
std::vector<InputChoice> input_choices(int device_channels) {
    std::vector<InputChoice> choices{{0, 0}};
    const int inputs = std::max(2, device_channels);
    for (int first = 0; first < inputs; first += 2) {
        if (first + 1 < inputs) choices.push_back({first, 2});
        choices.push_back({first, 1});
        if (first + 1 < inputs) choices.push_back({first + 1, 1});
    }
    return choices;
}

} // namespace

QString SongModel::audioInputText(const blokkily::TrackInput& input) {
    if (!blokkily::takes_audio(input)) return QStringLiteral("MIDI");
    const int first = input.audio_first_channel + 1;
    return input.audio_channels >= 2 ? QString("IN %1-%2").arg(first).arg(first + 1)
                                     : QString("IN %1").arg(first);
}

QString SongModel::monitorText(const blokkily::TrackInput& input) {
    switch (input.monitor) {
    case blokkily::TrackInput::Monitor::on:
        return QStringLiteral("MON");
    case blokkily::TrackInput::Monitor::off:
        return QStringLiteral("MON OFF");
    case blokkily::TrackInput::Monitor::automatic:
        break;
    }
    return QStringLiteral("MON AUTO");
}

void SongModel::setAudioInputChannels(int channels) {
    const int wanted = std::max(2, channels);
    if (wanted == audio_input_channels_) return;
    audio_input_channels_ = wanted;
    emit inputChanged();
}

void SongModel::setAudioInput(int track, int first, int channels) {
    if (!validTrack(track) || first < 0 || first > 0xFFFF || channels < 0 || channels > 2) return;
    auto& input = song_.tracks[static_cast<std::size_t>(track)].input;
    auto wanted = input;
    if (channels == 0) {
        wanted.source = blokkily::TrackInput::Source::midi;
    } else {
        // An audio track records audio only, so a keyboard played while it is
        // armed does not write notes into it.
        wanted.source = blokkily::TrackInput::Source::audio;
        wanted.audio_first_channel = static_cast<std::uint16_t>(first);
        wanted.audio_channels = static_cast<std::uint8_t>(channels);
    }
    if (wanted == input) return;
    input = wanted;
    emit inputChanged();
    emit songChanged();
}

void SongModel::cycleAudioInput(int track, int step) {
    if (!validTrack(track)) return;
    const auto& input = song_.tracks[static_cast<std::size_t>(track)].input;
    const auto choices = input_choices(audio_input_channels_);
    const int count = static_cast<int>(choices.size());
    int current = 0;
    if (blokkily::takes_audio(input))
        for (int index = 1; index < count; ++index)
            if (choices[static_cast<std::size_t>(index)].first == input.audio_first_channel &&
                choices[static_cast<std::size_t>(index)].channels == input.audio_channels)
                current = index;
    const auto& next = choices[static_cast<std::size_t>(((current + step) % count + count) % count)];
    setAudioInput(track, next.first, next.channels);
}

void SongModel::cycleMonitor(int track, int step) {
    if (!validTrack(track)) return;
    using Monitor = blokkily::TrackInput::Monitor;
    // AUTO, ON, OFF and round.
    constexpr Monitor order[] = {Monitor::automatic, Monitor::on, Monitor::off};
    auto& input = song_.tracks[static_cast<std::size_t>(track)].input;
    int current = 0;
    for (int index = 0; index < 3; ++index)
        if (order[index] == input.monitor) current = index;
    input.monitor = order[((current + step) % 3 + 3) % 3];
    emit inputChanged();
    emit songChanged();
}

void SongModel::relocateAudioFiles(
    const std::map<std::filesystem::path, std::filesystem::path>& moved) {
    if (moved.empty()) return;
    const auto relocate = [&moved](blokkily::Song& song) {
        for (auto& file : song.audio_files)
            if (const auto found = moved.find(file.path); found != moved.end())
                file.path = found->second;
    };
    relocate(song_);
    // Every step of history names the file where it now is, so undoing back
    // to a take that was recorded before the save still finds its audio.
    for (auto& step : undo_) relocate(step.song);
    for (auto& step : redo_) relocate(step.song);
    emit audioClipsChanged();
}
