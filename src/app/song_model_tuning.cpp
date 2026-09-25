#include "song_model.hpp"

#include "blokkily/audio/mixer.hpp"

#include <QFileInfo>
#include <QtGlobal>

#include <algorithm>
#include <cmath>

QString SongModel::tuningName() const { return QString::fromStdString(song_.tuning.name); }

QStringList SongModel::tuningNames() const {
    QStringList names;
    for (const auto& tuning : blokkily::tuning_presets())
        names << QString::fromStdString(tuning.name);
    return names;
}

QString SongModel::scaleName() const { return QString::fromStdString(song_.scale.name); }

QStringList SongModel::scaleNames() const {
    QStringList names;
    for (const auto& scale : blokkily::scale_presets())
        names << QString::fromStdString(scale.name);
    return names;
}

QString SongModel::rootName() const { return degreeName(song_.root_degree); }

QString SongModel::degreeName(int degree) const {
    return QString::fromStdString(blokkily::degree_name(song_.tuning, degree));
}

// A note already in a pattern is stored as the pitch its instrument is told.
// Reading it back through the tuning is what lets one pattern be looked at in
// another tuning without any of its notes moving.
QString SongModel::pitchName(int key, double cents) const {
    return degreeName(blokkily::degree_for_pitch(
        song_.tuning, {static_cast<std::int16_t>(key), cents}));
}

blokkily::TunedPitch SongModel::pitchForDegree(int degree) const {
    return blokkily::degree_pitch(song_.tuning, degree);
}

int SongModel::snapDegree(int degree) const {
    if (!song_.auto_scale) return degree;
    return blokkily::snap_to_scale(song_.tuning, song_.scale, song_.root_degree, degree);
}

void SongModel::setTuning(const QString& name) {
    const auto tuning = blokkily::tuning_by_name(name.toStdString());
    if (!tuning || tuning->name == song_.tuning.name) return;
    checkpoint();
    // The root keeps its pitch class rather than its number: degree 60 is the
    // anchor in every tuning, so a root chosen as the fifth stays the fifth.
    const double from_root = blokkily::degree_cents(song_.tuning, song_.root_degree);
    song_.tuning = *tuning;
    song_.root_degree = blokkily::degree_for_pitch(
        song_.tuning, {static_cast<std::int16_t>(song_.tuning.anchor_key), from_root});
    emit tuningChanged();
    emit songChanged();
}

void SongModel::setScale(const QString& name) {
    const auto scale = blokkily::scale_by_name(name.toStdString());
    if (!scale || scale->name == song_.scale.name) return;
    checkpoint();
    song_.scale = *scale;
    emit tuningChanged();
    emit songChanged();
}

void SongModel::setRootDegree(int degree) {
    const int divisions = std::max(1, song_.tuning.divisions());
    // A root is a pitch class: it names the key the song is in, not an octave.
    const int anchored = song_.tuning.anchor_key +
                         ((degree - song_.tuning.anchor_key) % divisions + divisions) % divisions;
    if (anchored == song_.root_degree) return;
    checkpoint();
    song_.root_degree = anchored;
    emit tuningChanged();
    emit songChanged();
}

void SongModel::toggleAutoScale() {
    checkpoint();
    song_.auto_scale = !song_.auto_scale;
    emit tuningChanged();
    emit songChanged();
}
