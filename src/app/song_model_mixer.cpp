#include "song_model.hpp"

#include "blokkily/audio/mixer.hpp"

#include <QFileInfo>
#include <QtGlobal>

#include <algorithm>
#include <cmath>

void SongModel::setTrackGain(int track, double decibels) {
    if (!validTrack(track)) return;
    checkpointStrip(track, QString("gain:%1").arg(track));
    auto& mix = song_.tracks[static_cast<std::size_t>(track)].mix;
    const double previous = mix.gain_db;
    mix.gain_db = qBound(blokkily::minimum_audible_db, decibels, 6.0);
    emit mixChanged();
    emit stripMoved(track, 0, mix.gain_db, previous);
    emit songChanged();
}

void SongModel::setTrackPan(int track, double pan) {
    if (!validTrack(track)) return;
    checkpointStrip(track, QString("pan:%1").arg(track));
    auto& mix = song_.tracks[static_cast<std::size_t>(track)].mix;
    const double previous = mix.pan;
    mix.pan = qBound(-1.0, pan, 1.0);
    emit mixChanged();
    emit stripMoved(track, 1, mix.pan, previous);
    emit songChanged();
}

void SongModel::toggleMute(int track) {
    if (!validTrack(track)) return;
    checkpointStrip(track, {});
    auto& mix = song_.tracks[static_cast<std::size_t>(track)].mix;
    mix.mute = !mix.mute;
    emit mixChanged();
    emit stripMoved(track, 2, mix.mute ? 1.0 : 0.0, mix.mute ? 0.0 : 1.0);
    emit songChanged();
}

void SongModel::toggleSolo(int track) {
    if (!validTrack(track)) return;
    checkpoint();
    auto& mix = song_.tracks[static_cast<std::size_t>(track)].mix;
    mix.solo = !mix.solo;
    emit mixChanged();
    emit songChanged();
}

void SongModel::setMasterGain(double decibels) {
    checkpoint(QStringLiteral("master"));
    song_.master_gain_db = qBound(blokkily::minimum_audible_db, decibels, 6.0);
    emit mixChanged();
    emit songChanged();
}

QVariantList SongModel::meters() const {
    QVariantList fractions;
    for (std::size_t index = 0; index < song_.tracks.size(); ++index) {
        const double peak = index < peaks_.size() ? static_cast<double>(peaks_[index]) : 0.0;
        // A meter reads in decibels; the bar is that mapped onto the last 60 dB.
        fractions.push_back(peak <= 0.0 ? 0.0
                                         : qBound(0.0, (blokkily::linear_to_db(peak) + 60.0) / 60.0,
                                                  1.0));
    }
    return fractions;
}

void SongModel::setMeters(const std::vector<float>& track_peaks, float master_peak) {
    peaks_ = track_peaks;
    peaks_.resize(song_.tracks.size(), 0.0F);
    master_peak_ = master_peak;
    emit metersChanged();
}
