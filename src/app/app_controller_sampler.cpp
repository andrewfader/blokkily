// The sampler's side of the controller (plan item 2.3): its panel's property
// and edits, instrument state handed to a running processor without a
// rebuild, and undo giving a running sampler its earlier program back.
//
// The program a sampler plays is its instrument state, a SamplerProgram blob
// kept on the track like any plugin's state, so the song stays the one copy
// of it. Every panel edit rewrites that blob and pushes it to the running
// SamplerInstrument, which swaps kits between two blocks: the engine, the
// arrangement and the playhead are untouched, and a note already sounding
// keeps the sound it started with.

#include "app_controller.hpp"
#include "sampler_processor.hpp"

#include "blokkily/instruments/sampler.hpp"
#include "blokkily/project/paths.hpp"

#include <QFileInfo>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace {

QString local_file(const QString& value) {
    const QUrl url(value);
    return url.isLocalFile() ? url.toLocalFile() : value;
}

const char* loop_name(blokkily::LoopMode mode) {
    switch (mode) {
    case blokkily::LoopMode::forward: return "forward";
    case blokkily::LoopMode::ping_pong: return "ping_pong";
    case blokkily::LoopMode::off: break;
    }
    return "off";
}

// The first key a kit's pads start from: C2, where General MIDI puts its kick.
constexpr int first_pad_key = 36;

} // namespace

void AppController::connectSampler() {
    // The panel reads the selected track's program, so anything that changes
    // the song or the selection may change what it shows.
    QObject::connect(song_, &SongModel::arrangementChanged, this, &AppController::samplerChanged);
    // Undo and redo restore the song, and with it a sampler's earlier program:
    // restoreInstrumentStates gives the running sampler that program, still
    // without rebuilding anything, and the panel reads it back.
    QObject::connect(song_, &SongModel::instrumentStatesRestored, this,
                     &AppController::samplerChanged);
}

bool AppController::pushInstrumentState(int track, std::span<const std::byte> state,
                                        bool stop_if_needed) {
    if (!engine_ || track < 0 || static_cast<std::size_t>(track) >= engine_->track_count())
        return false;
    const auto where = blokkily::track_instrument(static_cast<std::uint32_t>(track));
    auto* processor = engine_->processor(where);
    if (processor == nullptr || state.empty()) return false;
    if (engine_->update_processor_state(where, state)) return true;
    if (!stop_if_needed) return false;
    // A processor that cannot take state while it runs is given it with the
    // callback stopped for the moment of the load, never mid-block.
    const bool running = audio_output_ && audio_output_->is_running();
    if (running) audio_output_->stop();
    const bool loaded = processor->load_state(state);
    if (running) {
        std::string error;
        if (!audio_output_->start(&error)) {
            status_ = QString("Audio restart failed · %1").arg(QString::fromStdString(error));
            emit statusChanged();
        }
    }
    return loaded;
}

bool AppController::setInstrumentState(int track, std::vector<std::byte> state,
                                       const QString& merge) {
    if (song_ == nullptr || track < 0 || track >= song_->trackCount()) return false;
    if (song_->song().tracks[static_cast<std::size_t>(track)].instrument.format.empty())
        return false;
    // Pushed first, so a processor that refuses the state leaves the song as
    // it was rather than holding a state nothing plays.
    if (engine_ && builtFromCurrentGraph() && !pushInstrumentState(track, state, true))
        return false;
    if (!song_->setInstrumentState(track, std::move(state), merge)) return false;
    emit samplerChanged();
    return true;
}

bool AppController::setInstrumentState(int track, const QByteArray& state) {
    const auto* bytes = reinterpret_cast<const std::byte*>(state.constData());
    return setInstrumentState(track, std::vector<std::byte>(bytes, bytes + state.size()));
}

std::optional<blokkily::SamplerProgram> AppController::samplerProgram(int track) const {
    if (song_ == nullptr || track < 0 || track >= song_->trackCount()) return std::nullopt;
    const auto& slot = song_->song().tracks[static_cast<std::size_t>(track)].instrument;
    if (slot.format != blokkily::sampler_format) return std::nullopt;
    if (auto program = blokkily::parse_sampler(slot.state)) return program;
    // A sampler not yet given a program plays the empty one of its kind.
    return blokkily::default_sampler(slot.identifier == blokkily::sampler_kit_identifier
                                         ? blokkily::SamplerProgram::Mode::kit
                                         : blokkily::SamplerProgram::Mode::keyed);
}

QVariantMap AppController::sampler() const {
    QVariantMap view;
    const int track = song_ != nullptr ? song_->selectedTrack() : -1;
    const auto program = samplerProgram(track);
    view["active"] = program.has_value();
    view["track"] = track;
    if (!program) return view;
    const bool kit = program->mode == blokkily::SamplerProgram::Mode::kit;
    const int zones = static_cast<int>(program->zones.size());
    const int zone = zones == 0 ? -1 : std::clamp(sampler_zone_, 0, zones - 1);
    view["mode"] = kit ? "kit" : "keyed";
    view["zones"] = zones;
    view["zone"] = zone;
    // What the running sampler could not load, so a moved file is seen.
    QStringList missing;
    if (engine_ && track >= 0 && static_cast<std::size_t>(track) < engine_->track_count())
        if (const auto* running = dynamic_cast<const blokkily::SamplerInstrument*>(
                engine_->processor(blokkily::track_instrument(static_cast<std::uint32_t>(track)))))
            for (const auto& reason : running->missing_samples())
                missing.push_back(QString::fromStdString(reason));
    view["missingSamples"] = missing;
    if (zone < 0) {
        view["sample"] = QString();
        view["missing"] = false;
        return view;
    }
    const auto& selected = program->zones[static_cast<std::size_t>(zone)];
    const QString prefix = QString("zone %1:").arg(zone);
    view["sample"] = QFileInfo(QString::fromStdString(selected.sample)).fileName();
    view["missing"] = std::any_of(missing.begin(), missing.end(),
                                  [&](const QString& reason) { return reason.startsWith(prefix); });
    view["rootKey"] = selected.root_key;
    view["lowKey"] = selected.low_key;
    view["highKey"] = selected.high_key;
    view["loop"] = loop_name(selected.loop);
    view["attack"] = selected.envelope.attack_s;
    view["decay"] = selected.envelope.decay_s;
    view["sustain"] = selected.envelope.sustain;
    view["release"] = selected.envelope.release_s;
    view["trackPitch"] = selected.track_pitch;
    view["oneShot"] = selected.one_shot;
    view["lowVelocity"] = selected.low_velocity;
    view["highVelocity"] = selected.high_velocity;
    view["start"] = static_cast<double>(selected.start);
    view["end"] = static_cast<double>(selected.end);
    view["loopStart"] = static_cast<double>(selected.loop_start);
    view["loopEnd"] = static_cast<double>(selected.loop_end);
    // The file's length, for the waveform's markers.
    std::uint64_t frames = 0;
    if (const auto audio = [&] {
            blokkily::SamplerInstrument probe(assets_.get());
            probe.set_base_directory(processorContext().project_dir);
            return probe.asset_for(selected);
        }())
        frames = audio->frames;
    view["frames"] = static_cast<double>(frames);
    // Every zone's keys and velocities, for the map that shows the layers.
    QVariantList map;
    for (std::size_t index = 0; index < program->zones.size(); ++index) {
        const auto& zone = program->zones[index];
        map.append(QVariantMap{{"zone", static_cast<int>(index)}, {"lowKey", zone.low_key},
                               {"highKey", zone.high_key}, {"lowVelocity", zone.low_velocity},
                               {"highVelocity", zone.high_velocity}, {"rootKey", zone.root_key}});
    }
    view["map"] = map;
    return view;
}

bool AppController::editSampler(const QString& merge,
                                const std::function<bool(blokkily::SamplerProgram&)>& edit) {
    if (song_ == nullptr) return false;
    const int track = song_->selectedTrack();
    auto program = samplerProgram(track);
    if (!program || !edit(*program)) return false;
    std::string error;
    if (!blokkily::validate_sampler(*program, &error)) {
        status_ = QString("Sampler unchanged · %1").arg(QString::fromStdString(error));
        emit statusChanged();
        return false;
    }
    return setInstrumentState(track, blokkily::serialize_sampler(*program), merge);
}

namespace {
// The zone the panel edits, or nullptr when the program has none.
blokkily::SamplerZone* selected_zone(blokkily::SamplerProgram& program, int zone) {
    if (program.zones.empty()) return nullptr;
    return &program.zones[static_cast<std::size_t>(
        std::clamp(zone, 0, static_cast<int>(program.zones.size()) - 1))];
}
} // namespace

bool AppController::selectSamplerZone(int zone) {
    const auto program = samplerProgram(song_ != nullptr ? song_->selectedTrack() : -1);
    if (!program || zone < 0 || zone >= static_cast<int>(program->zones.size())) return false;
    sampler_zone_ = zone;
    emit samplerChanged();
    return true;
}

bool AppController::setSamplerRootKey(int key) {
    if (key < 0 || key > 127) return false;
    return editSampler("sampler-root", [&](blokkily::SamplerProgram& program) {
        auto* zone = selected_zone(program, sampler_zone_);
        if (zone == nullptr) return false;
        zone->root_key = key;
        return true;
    });
}

bool AppController::setSamplerKeyRange(int low, int high) {
    if (low < 0 || high > 127 || low > high) return false;
    return editSampler("sampler-range", [&](blokkily::SamplerProgram& program) {
        auto* zone = selected_zone(program, sampler_zone_);
        if (zone == nullptr) return false;
        zone->low_key = low;
        zone->high_key = high;
        return true;
    });
}

bool AppController::setSamplerLoop(const QString& mode) {
    const auto loop = mode == "forward"     ? blokkily::LoopMode::forward
                    : mode == "ping_pong"   ? blokkily::LoopMode::ping_pong
                    : mode == "off"         ? blokkily::LoopMode::off
                                            : static_cast<blokkily::LoopMode>(255);
    if (static_cast<int>(loop) == 255) return false;
    return editSampler({}, [&](blokkily::SamplerProgram& program) {
        auto* zone = selected_zone(program, sampler_zone_);
        if (zone == nullptr) return false;
        zone->loop = loop;
        // A loop turned on over a file that names none loops the whole region.
        if (loop != blokkily::LoopMode::off && zone->loop_end <= zone->loop_start) {
            std::uint64_t end = zone->end;
            if (end == 0) {
                std::string error;
                blokkily::SamplerInstrument probe(assets_.get());
                probe.set_base_directory(processorContext().project_dir);
                const auto asset = probe.asset_for(*zone, &error);
                if (!asset) return false;
                end = asset->frames;
            }
            zone->loop_start = zone->start;
            zone->loop_end = end;
        }
        return true;
    });
}

bool AppController::setSamplerEnvelope(const QString& stage, double value) {
    if (!std::isfinite(value) || value < 0.0) return false;
    return editSampler("sampler-" + stage, [&](blokkily::SamplerProgram& program) {
        auto* zone = selected_zone(program, sampler_zone_);
        if (zone == nullptr) return false;
        auto& envelope = zone->envelope;
        if (stage == "attack") envelope.attack_s = value;
        else if (stage == "decay") envelope.decay_s = value;
        else if (stage == "sustain") envelope.sustain = std::min(value, 1.0);
        else if (stage == "release") envelope.release_s = value;
        else return false;
        return true;
    });
}

bool AppController::setSamplerMode(const QString& mode) {
    if (mode != "keyed" && mode != "kit") return false;
    const bool kit = mode == "kit";
    return editSampler({}, [&](blokkily::SamplerProgram& program) {
        program.mode = kit ? blokkily::SamplerProgram::Mode::kit
                           : blokkily::SamplerProgram::Mode::keyed;
        // A pad plays its file at its own pitch to the end; a keyed zone
        // follows the key and stops when it is let go.
        for (auto& zone : program.zones) {
            zone.track_pitch = !kit;
            zone.one_shot = kit;
        }
        return true;
    });
}

bool AppController::loadSamplerSample(const QString& path) {
    const auto file = std::filesystem::path(local_file(path).toStdString());
    std::string error;
    blokkily::SamplerInstrument probe(assets_.get());
    probe.set_base_directory(processorContext().project_dir);
    auto made = probe.make_zone(file, &error);
    if (!made) {
        status_ = QString("Sample not loaded · %1").arg(QString::fromStdString(error));
        emit statusChanged();
        return false;
    }
    int selected = sampler_zone_;
    const bool edited = editSampler({}, [&](blokkily::SamplerProgram& program) {
        if (program.mode == blokkily::SamplerProgram::Mode::kit) {
            // A new pad on the first key no pad answers yet.
            int key = first_pad_key;
            while (key <= 127 && !blokkily::zones_for(program, key, 127).empty()) ++key;
            if (key > 127) return false;
            made->low_key = made->high_key = key;
            made->track_pitch = false;
            made->one_shot = true;
            program.zones.push_back(*made);
            selected = static_cast<int>(program.zones.size()) - 1;
            return true;
        }
        // Keyed: the selected zone plays the new file, keeping its keys and
        // envelope; its root key and loop come from the file.
        if (auto* zone = selected_zone(program, sampler_zone_)) {
            made->low_key = zone->low_key;
            made->high_key = zone->high_key;
            made->low_velocity = zone->low_velocity;
            made->high_velocity = zone->high_velocity;
            made->envelope = zone->envelope;
            made->gain_db = zone->gain_db;
            made->pan = zone->pan;
            *zone = *made;
        } else {
            program.zones.push_back(*made);
            selected = 0;
        }
        return true;
    });
    if (edited) {
        sampler_zone_ = selected;
        emit samplerChanged();
    }
    return edited;
}

bool AppController::sliceSampler(int count) {
    if (count < 1 || count > 128) return false;
    const bool edited = editSampler({}, [&](blokkily::SamplerProgram& program) {
        auto* zone = selected_zone(program, sampler_zone_);
        if (zone == nullptr) return false;
        // The whole file is chopped, not just the region this zone plays, so
        // chopping a slice again starts from the loop it came from.
        blokkily::SamplerZone source = *zone;
        source.start = 0;
        source.end = 0;
        std::string error;
        blokkily::SamplerInstrument probe(assets_.get());
        probe.set_base_directory(processorContext().project_dir);
        const auto asset = probe.asset_for(source, &error);
        if (!asset) return false;
        return blokkily::slice_evenly(program, source, asset->frames, count, first_pad_key);
    });
    if (edited) {
        sampler_zone_ = 0;
        emit samplerChanged();
    }
    return edited;
}

void AppController::rebaseSamplers(const std::filesystem::path& directory) {
    if (song_ == nullptr) return;
    const auto from = processorContext().project_dir;
    if (from == directory) return;
    auto& tracks = song_->song().tracks;
    for (std::size_t track = 0; track < tracks.size(); ++track) {
        auto& slot = tracks[track].instrument;
        if (slot.format != blokkily::sampler_format) continue;
        auto* running = engine_ && track < engine_->track_count()
            ? dynamic_cast<blokkily::SamplerInstrument*>(
                  engine_->processor(blokkily::track_instrument(static_cast<std::uint32_t>(track))))
            : nullptr;
        auto program = running != nullptr ? std::optional(running->program())
                                           : blokkily::parse_sampler(slot.state);
        if (!program) continue;
        // Every path is made whole against the folder it was relative to, and
        // then relative again to the folder the project is moving to.
        for (auto& zone : program->zones)
            zone.sample = blokkily::to_project_relative(
                              blokkily::resolve_project_path(zone.sample, from), directory)
                              .generic_string();
        slot.state = blokkily::serialize_sampler(*program);
        if (running != nullptr) {
            running->set_base_directory(directory);
            (void)running->set_program(*program);
        }
    }
}

// --- Multi-sample import, velocity layers and the waveform -------------------
// (features/sampler_editing.feature)

int AppController::importMultisample(const QStringList& paths) {
    if (paths.isEmpty()) return 0;
    blokkily::SamplerInstrument probe(assets_.get());
    probe.set_base_directory(processorContext().project_dir);
    std::vector<blokkily::MultisampleSource> sources;
    QStringList refused;
    int unnamed = 0;
    for (const auto& path : paths) {
        const auto file = std::filesystem::path(local_file(path).toStdString());
        std::string error;
        auto made = probe.make_zone(file, &error);
        if (!made) {
            refused << QFileInfo(local_file(path)).fileName();
            continue;
        }
        const auto name = file.filename().string();
        blokkily::MultisampleSource source;
        // The key the name says, else the one the file says (its smpl
        // chunk), else the next key up from C3 for a file that says neither.
        const auto named = blokkily::key_from_name(name);
        source.root_key = named ? *named : made->root_key != 60 ? made->root_key : 48 + unnamed++;
        source.layer = blokkily::layer_from_name(name).value_or(0);
        source.zone = *made;
        sources.push_back(std::move(source));
    }
    if (sources.empty()) {
        status_ = QString("No sample could be read · %1").arg(refused.join(", "));
        emit statusChanged();
        return 0;
    }
    const auto program = blokkily::multisample_program(sources);
    const int zones = static_cast<int>(program.zones.size());
    const bool edited = editSampler({}, [&](blokkily::SamplerProgram& current) {
        const int polyphony = current.polyphony;
        current = program;
        current.polyphony = polyphony;
        return true;
    });
    if (!edited) return 0;
    sampler_zone_ = 0;
    int layers = 0;
    for (const auto& zone : program.zones)
        if (zone.low_key == 0) ++layers;
    status_ = QString("%1 samples mapped · %2 velocity layer%3").arg(zones).arg(layers).arg(layers == 1 ? "" : "s");
    if (!refused.isEmpty()) status_ += QString(" · not read: %1").arg(refused.join(", "));
    emit statusChanged();
    emit samplerChanged();
    return zones;
}

bool AppController::setSamplerVelocityRange(int low, int high) {
    if (low < 1 || high > 127 || low > high) return false;
    return editSampler("sampler-velocity", [&](blokkily::SamplerProgram& program) {
        auto* zone = selected_zone(program, sampler_zone_);
        if (zone == nullptr) return false;
        zone->low_velocity = low;
        zone->high_velocity = high;
        return true;
    });
}

namespace {
// The frames of the selected zone's file, read through the sampler's own
// loader so the path resolves as the sampler resolves it.
blokkily::AudioAssetPtr zone_audio(blokkily::AudioAssetCache* assets,
                                   const std::filesystem::path& base, const blokkily::SamplerZone& zone) {
    blokkily::SamplerInstrument probe(assets);
    probe.set_base_directory(base);
    return probe.asset_for(zone);
}
} // namespace

bool AppController::setSamplerRegion(double start, double end) {
    const auto program = samplerProgram(song_ != nullptr ? song_->selectedTrack() : -1);
    if (!program || program->zones.empty()) return false;
    const auto& selected = program->zones[static_cast<std::size_t>(std::clamp(
        sampler_zone_, 0, static_cast<int>(program->zones.size()) - 1))];
    const auto audio = zone_audio(assets_.get(), processorContext().project_dir, selected);
    if (!audio) return false;
    const auto frames = static_cast<double>(audio->frames);
    const auto first = static_cast<std::uint64_t>(std::llround(std::clamp(start, 0.0, frames - 1.0)));
    const auto last = static_cast<std::uint64_t>(std::llround(std::clamp(end, 0.0, frames)));
    if (last <= first + 1) return false;
    return editSampler("sampler-region", [&](blokkily::SamplerProgram& edited) {
        auto* zone = selected_zone(edited, sampler_zone_);
        if (zone == nullptr) return false;
        zone->start = first;
        zone->end = last == audio->frames ? 0 : last;
        // A loop stays inside what plays.
        if (zone->loop != blokkily::LoopMode::off) {
            zone->loop_start = std::clamp(zone->loop_start, first, last - 1);
            zone->loop_end = std::clamp(zone->loop_end, zone->loop_start + 1, last);
        }
        return true;
    });
}

bool AppController::setSamplerLoopPoints(double start, double end) {
    const auto program = samplerProgram(song_ != nullptr ? song_->selectedTrack() : -1);
    if (!program || program->zones.empty()) return false;
    const auto& selected = program->zones[static_cast<std::size_t>(std::clamp(
        sampler_zone_, 0, static_cast<int>(program->zones.size()) - 1))];
    const auto audio = zone_audio(assets_.get(), processorContext().project_dir, selected);
    if (!audio) return false;
    const auto region_end = selected.end == 0 ? audio->frames : selected.end;
    const auto first = static_cast<std::uint64_t>(std::llround(std::max(start, static_cast<double>(selected.start))));
    const auto last = static_cast<std::uint64_t>(std::llround(std::min(end, static_cast<double>(region_end))));
    if (last <= first) return false;
    return editSampler("sampler-loop-points", [&](blokkily::SamplerProgram& edited) {
        auto* zone = selected_zone(edited, sampler_zone_);
        if (zone == nullptr) return false;
        zone->loop_start = first;
        zone->loop_end = last;
        if (zone->loop == blokkily::LoopMode::off) zone->loop = blokkily::LoopMode::forward;
        return true;
    });
}

QVariantList AppController::samplerPeaks(int columns) const {
    QVariantList peaks;
    const auto program = samplerProgram(song_ != nullptr ? song_->selectedTrack() : -1);
    if (!program || program->zones.empty() || columns <= 0) return peaks;
    const auto& selected = program->zones[static_cast<std::size_t>(std::clamp(
        sampler_zone_, 0, static_cast<int>(program->zones.size()) - 1))];
    const auto audio = zone_audio(assets_.get(), processorContext().project_dir, selected);
    if (!audio || audio->left.empty()) return peaks;
    const auto frames = audio->left.size();
    for (int column = 0; column < columns; ++column) {
        const auto from = frames * static_cast<std::size_t>(column) / static_cast<std::size_t>(columns);
        const auto to = std::max(from + 1, frames * static_cast<std::size_t>(column + 1) / static_cast<std::size_t>(columns));
        float low = 0.0F, high = 0.0F;
        for (auto frame = from; frame < to && frame < frames; ++frame) {
            const float sample = audio->right.empty() ? audio->left[frame]
                                                      : 0.5F * (audio->left[frame] + audio->right[frame]);
            low = std::min(low, sample);
            high = std::max(high, sample);
        }
        peaks.append(low);
        peaks.append(high);
    }
    return peaks;
}
