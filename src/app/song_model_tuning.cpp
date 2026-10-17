#include "song_model.hpp"

#include "blokkily/audio/mixer.hpp"
#include "blokkily/notation/clef.hpp"

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
    announceSong();
}

void SongModel::setScale(const QString& name) {
    const auto scale = blokkily::scale_by_name(name.toStdString());
    if (!scale || scale->name == song_.scale.name) return;
    checkpoint();
    song_.scale = *scale;
    emit tuningChanged();
    announceSong();
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
    announceSong();
}

void SongModel::toggleAutoScale() {
    checkpoint();
    song_.auto_scale = !song_.auto_scale;
    emit tuningChanged();
    announceSong();
}

namespace {

// The clef's own name for a name in any case, or empty when it names none of
// the eleven.
QString canonicalClef(const QString& name) {
    for (int kind = 0; kind <= static_cast<int>(blokkily::notation::ClefKind::Percussion); ++kind) {
        const auto clef = blokkily::notation::Clef(static_cast<blokkily::notation::ClefKind>(kind));
        const auto own = QString::fromUtf8(clef.name().data(), static_cast<qsizetype>(clef.name().size()));
        if (own.compare(name.trimmed(), Qt::CaseInsensitive) == 0) return own;
    }
    return {};
}

bool beatUnit(int unit) { return unit == 1 || unit == 2 || unit == 4 || unit == 8 || unit == 16 || unit == 32; }

}  // namespace

QVariantMap SongModel::notationSettings(int track, int section) const {
    if (section < 0 || static_cast<std::size_t>(section) >= song_.sections.size()) return {};
    const auto part = song_.part(static_cast<std::size_t>(section), static_cast<std::size_t>(std::max(0, track)));
    if (track < 0 || !part) return {};
    const auto& written = song_.sections[static_cast<std::size_t>(section)];
    // The song's meter where this part first plays, unless one is set.
    int beats = written.meter_beats;
    int unit = written.meter_unit;
    if (beats == 0) {
        std::int32_t bar = 0;
        for (const auto& clip : song_.clips)
            if (clip.pattern == *part) {
                bar = song_.meter.bar_at(clip.start);
                break;
            }
        const auto& meter = song_.meter.meter_in(bar);
        beats = meter.numerator;
        unit = meter.denominator;
    }
    return {{"clef", QString::fromStdString(song_.patterns[*part].clef)},
            {"keyFifths", written.key_fifths},
            {"timeBeats", beats},
            {"timeUnit", unit},
            {"unmetered", written.unmetered},
            {"meterSet", written.meter_beats != 0}};
}

bool SongModel::setNotationSettings(int track, int section, const QVariantMap& settings) {
    return setNotationSettings(track, section, settings, true);
}

bool SongModel::setNotationSettings(int track, int section, const QVariantMap& settings, bool own_step) {
    if (track < 0 || section < 0 || static_cast<std::size_t>(section) >= song_.sections.size()) return false;
    const auto part = song_.part(static_cast<std::size_t>(section), static_cast<std::size_t>(track));
    if (!part) return false;
    auto written = song_.sections[static_cast<std::size_t>(section)];
    std::string clef = song_.patterns[*part].clef;
    bool ok = true;
    const auto whole = [&ok](const QVariant& value) {
        bool read = false;
        const int number = value.toInt(&read);
        if (!read || value.typeId() == QMetaType::Bool) ok = false;
        return number;
    };
    if (settings.contains("clef")) {
        const QString named = canonicalClef(settings.value("clef").toString());
        if (named.isEmpty()) return false;
        clef = named.toStdString();
    }
    if (settings.contains("keyFifths")) {
        const int fifths = whole(settings.value("keyFifths"));
        if (!ok || fifths < -7 || fifths > 7) return false;
        written.key_fifths = fifths;
    }
    if (settings.contains("timeBeats") || settings.contains("timeUnit")) {
        const auto current = notationSettings(track, section);
        const int beats = whole(settings.value("timeBeats", current.value("timeBeats")));
        const int unit = whole(settings.value("timeUnit", current.value("timeUnit")));
        if (!ok || beats < 1 || beats > 64 || !beatUnit(unit)) return false;
        written.meter_beats = beats;
        written.meter_unit = unit;
    }
    if (settings.contains("meterSet") && !settings.value("meterSet").toBool()) {
        written.meter_beats = 0;
        written.meter_unit = 4;
    }
    if (settings.contains("unmetered")) written.unmetered = settings.value("unmetered").toBool();
    if (written == song_.sections[static_cast<std::size_t>(section)] && clef == song_.patterns[*part].clef)
        return true;
    if (own_step) checkpoint();
    song_.sections[static_cast<std::size_t>(section)] = written;
    song_.patterns[*part].clef = clef;
    emit notationChanged();
    announceSong();
    return true;
}
