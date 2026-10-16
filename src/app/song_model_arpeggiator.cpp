// The track arpeggiator's settings (features/arpeggiator.feature). They reach
// the running engine like a fader does (mixChanged -> apply_mix), without a
// rebuild, and each change is a step of history.

#include "song_model.hpp"

#include <QVariantMap>

namespace {

constexpr const char* mode_names[] = {"UP", "DOWN", "UP/DN", "PLAYED", "RANDOM"};
struct Rate {
    const char* name;
    std::uint8_t division;
};
constexpr Rate rates[] = {{"1/4", 1}, {"1/8", 2}, {"1/8T", 3}, {"1/16", 4}, {"1/16T", 6}, {"1/32", 8}};

QString rate_name(std::uint8_t division) {
    for (const auto& rate : rates)
        if (rate.division == division) return rate.name;
    return "1/16";
}

} // namespace

QStringList SongModel::arpModes() const {
    QStringList names;
    for (const auto* name : mode_names) names << name;
    return names;
}

QStringList SongModel::arpRates() const {
    QStringList names;
    for (const auto& rate : rates) names << rate.name;
    return names;
}

QVariantList SongModel::arpeggiators() const {
    QVariantList list;
    for (const auto& track : song_.tracks) {
        const auto& arp = track.arp;
        list.append(QVariantMap{{"enabled", arp.enabled},
                                {"mode", mode_names[static_cast<int>(arp.mode)]},
                                {"rate", rate_name(arp.division)},
                                {"octaves", arp.octaves},
                                {"gate", static_cast<double>(arp.gate)},
                                {"latch", arp.latch}});
    }
    return list;
}

bool SongModel::editArpeggiator(int track, const std::function<void(blokkily::Arpeggiator&)>& edit) {
    if (!validTrack(track)) return false;
    auto changed = song_.tracks[static_cast<std::size_t>(track)].arp;
    edit(changed);
    if (!changed.valid() || changed == song_.tracks[static_cast<std::size_t>(track)].arp) return false;
    checkpoint();
    song_.tracks[static_cast<std::size_t>(track)].arp = changed;
    emit mixChanged();
    return true;
}

bool SongModel::setArpEnabled(int track, bool enabled) {
    return editArpeggiator(track, [enabled](auto& arp) { arp.enabled = enabled; });
}

bool SongModel::setArpMode(int track, const QString& mode) {
    for (int index = 0; index < static_cast<int>(std::size(mode_names)); ++index)
        if (mode == mode_names[index])
            return editArpeggiator(track, [index](auto& arp) {
                arp.mode = static_cast<blokkily::Arpeggiator::Mode>(index);
            });
    return false;
}

bool SongModel::setArpRate(int track, const QString& rate) {
    for (const auto& known : rates)
        if (rate == known.name)
            return editArpeggiator(track, [&known](auto& arp) { arp.division = known.division; });
    return false;
}

bool SongModel::setArpOctaves(int track, int octaves) {
    return editArpeggiator(track, [octaves](auto& arp) {
        arp.octaves = static_cast<std::uint8_t>(qBound(0, octaves, 255));
    });
}

bool SongModel::setArpGate(int track, double gate) {
    return editArpeggiator(track, [gate](auto& arp) { arp.gate = static_cast<float>(gate); });
}

bool SongModel::setArpLatch(int track, bool latch) {
    return editArpeggiator(track, [latch](auto& arp) { arp.latch = latch; });
}
