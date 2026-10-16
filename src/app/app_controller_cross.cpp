#include "app_controller.hpp"
#include "pattern_model.hpp"
#include "song_model.hpp"
#include "sampler_processor.hpp"
#include "blokkily/instruments/sampler.hpp"
#include "blokkily/instruments/sampler_program.hpp"
#include "blokkily/project/paths.hpp"

#include <QVariantMap>

#include <cmath>
#include <map>
#include <optional>
#include <system_error>

namespace {
// Slot -1 is a track's instrument; every other slot is an insert.
std::optional<blokkily::ProcessorAddress> address(const QString& kind, int bus, int slot) {
    if (bus < 0 || slot < -1 || (slot == -1 && kind != "track")) return {};
    using blokkily::BusKind;
    if (kind != "track" && kind != "return" && kind != "master") return {};
    return blokkily::ProcessorAddress{kind == "track" ? BusKind::track :
        kind == "return" ? BusKind::ret : BusKind::master,
        static_cast<std::uint32_t>(bus), slot};
}
}

bool AppController::insertEditorOpen(const QString& kind, int bus, int slot) const {
    const auto where = address(kind, bus, slot);
    return where && windows_ && windows_->isOpen(*where);
}

bool AppController::toggleInsertEditor(const QString& kind, int bus, int slot) {
    const auto where = address(kind, bus, slot);
    if (!where) return false;
    if (windows_ && windows_->isOpen(*where)) return windows_->close(*where);
    return openProcessorEditor(*where);
}

QVariantList AppController::insertParameters(const QString& kind, int bus, int slot) const {
    QVariantList result;
    const auto where = address(kind, bus, slot);
    const auto* instance = where && engine_ ? engine_->processor(*where) : nullptr;
    if (instance)
        for (const auto& parameter : instance->parameters())
            if (parameter.automatable) result.push_back(QVariantMap{{"id", static_cast<int>(parameter.id)},
                {"name", QString::fromStdString(parameter.name)},
                {"minimum", parameter.min}, {"maximum", parameter.max},
                {"default", parameter.default_value}});
    return result;
}

bool AppController::automateInsert(const QString& kind, int bus, int slot, int parameter) {
    const auto where = address(kind, bus, slot);
    const auto* instance = where && engine_ ? engine_->processor(*where) : nullptr;
    if (song_ == nullptr || instance == nullptr) return false;
    for (const auto& info : instance->parameters()) {
        if (info.id != parameter || !info.automatable) continue;
        // A track's lane lives on that track; a return's or the master's on
        // the track the producer is working on.
        const int owner = kind == "track" ? bus : song_->selectedTrack();
        const int lane = song_->addParameterLane(owner, *where, parameter,
                                                  info.default_value);
        if (lane < 0) return false;
        if (owner == song_->selectedTrack()) song_->selectLane(lane);
        return true;
    }
    return false;
}

bool AppController::setStepLock(int track, int pattern, int step, int slot, int parameter,
                                double value, bool modulation) {
    return setInsertStepLock(track, pattern, step, "track", track, slot, parameter, value,
                             modulation);
}

bool AppController::setInsertStepLock(int track, int pattern, int step, const QString& kind,
                                      int bus, int slot, int parameter, double value,
                                      bool modulation) {
    if (song_ == nullptr || pattern_ == nullptr || song_->partOf(pattern, track) < 0) return false;
    const auto where = address(kind, kind == "track" ? track : bus, slot);
    const auto* instance = where && engine_ ? engine_->processor(*where) : nullptr;
    if (instance == nullptr) return false;
    for (const auto& info : instance->parameters()) {
        if (info.id != parameter || !info.automatable) continue;
        // Modulation offsets the value, up or down by at most the whole range.
        const double span = info.max - info.min;
        if (modulation ? !(std::abs(value) <= span) : !(value >= info.min && value <= info.max))
            return false;
        song_->selectSection(pattern);
        song_->selectTrack(track);
        return pattern_->lockStep(
            step, {info.name, parameter, value,
                   modulation ? blokkily::ParameterLock::Kind::modulation
                              : blokkily::ParameterLock::Kind::automation,
                   slot, where->kind,
                   where->kind == blokkily::BusKind::track ? 0U : where->bus});
    }
    return false;
}

bool AppController::clearStepLocks(int track, int pattern, int step) {
    if (song_ == nullptr || pattern_ == nullptr || song_->partOf(pattern, track) < 0) return false;
    song_->selectSection(pattern);
    song_->selectTrack(track);
    return pattern_->clearStepLocks(step);
}

QVariantMap AppController::parameterRange(const QString& kind, int bus, int slot, int parameter) const {
    const auto where = address(kind, bus, slot);
    auto* instance = where && engine_ ? engine_->processor(*where) : nullptr;
    if (instance)
        for (const auto& info : instance->parameters())
            if (info.id == parameter) return {{"minimum", info.min}, {"maximum", info.max}};
    return {};
}

bool AppController::collectAudio() {
    if (song_ == nullptr || project_path_.isEmpty() || take_writer_->active()) {
        status_ = "Save the project and stop recording before collecting audio";
        emit statusChanged();
        return false;
    }
    // Construct a complete edit before changing the song. Failed copies leave
    // all references intact. Originals are kept, so undo remains playable.
    auto collected = song_->song();
    const auto base = processorContext().project_dir;
    const auto target = recordingDirectory();
    std::map<std::filesystem::path, std::filesystem::path> copies;
    std::vector<std::filesystem::path> created;
    std::error_code error;
    std::filesystem::create_directories(target, error);
    const auto copy = [&](const std::filesystem::path& stored) -> std::filesystem::path {
        const auto source = blokkily::resolve_project_path(stored, base).lexically_normal();
        if (const auto found = copies.find(source); found != copies.end()) return found->second;
        if (source.parent_path() == target) return source;
        auto destination = target / source.filename();
        for (int n = 2; !error && std::filesystem::exists(destination, error); ++n)
            destination = target / (source.stem().string() + "-" + std::to_string(n) +
                                     source.extension().string());
        if (!error && std::filesystem::copy_file(source, destination, error)) {
            created.push_back(destination);
            copies.emplace(source, destination);
        }
        return destination;
    };
    (void)collected.prune_audio_files();
    for (auto& file : collected.audio_files) {
        if (error) break;
        file.path = copy(file.path);
    }
    for (auto& track : collected.tracks) {
        if (error) break;
        if (track.instrument.format != blokkily::sampler_format) continue;
        auto program = blokkily::parse_sampler(track.instrument.state);
        if (!program) continue;
        for (auto& zone : program->zones) {
            if (error) break;
            zone.sample = copy(zone.sample).generic_string();
        }
        track.instrument.state = blokkily::serialize_sampler(*program);
    }
    if (error) {
        for (const auto& file : created) { std::error_code ignored; std::filesystem::remove(file, ignored); }
        status_ = QString("Collect audio failed · %1").arg(QString::fromStdString(error.message()));
        emit statusChanged();
        return false;
    }
    if (!copies.empty()) song_->commitSongEdit(std::move(collected));
    status_ = QString("Collected %1 audio files · save to keep the new references").arg(copies.size());
    emit statusChanged();
    return true;
}
