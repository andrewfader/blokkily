// MusicXML in and out of the window (features/musicxml_window.feature).

#include "app_controller.hpp"
#include "song_model.hpp"

#include "blokkily/instruments/soundfont_catalog.hpp"
#include "blokkily/notation/musicxml.hpp"
#include "blokkily/notation/pattern_bridge.hpp"
#include "blokkily/notation/song_score.hpp"
#include "blokkily/sequencer/scheduler.hpp"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <tuple>

namespace {

QString local_file(const QString& value) {
    const QUrl url(value);
    return url.isLocalFile() ? url.toLocalFile() : value;
}

// What the scheduler plays from a pattern, note by note.
using Played = std::tuple<blokkily::Tick, int, blokkily::Tick, long long, int>;
void play_into(const blokkily::Pattern& pattern, std::vector<Played>& notes) {
    for (const auto& note : blokkily::Scheduler{}.render_loop(pattern, 0, 0))
        notes.emplace_back(note.start, note.key, note.duration, std::llround(note.cents * 1000.0),
                           static_cast<int>(std::lround(note.velocity * 1000.0F)));
}

} // namespace

bool AppController::importMusicXmlFile(const QString& path) {
    if (song_ == nullptr) return false;
    const QString file_path = local_file(path);
    QFile file(file_path);
    blokkily::notation::Score score;
    try {
        if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error(file.errorString().toStdString());
        score = blokkily::notation::parse(file.readAll().toStdString());
    } catch (const std::exception& error) {
        status_ = QString("MusicXML import failed · %1").arg(QString::fromUtf8(error.what()));
        emit statusChanged();
        return false;
    }
    if (score.title.empty()) score.title = QFileInfo(file_path).completeBaseName().toUpper().toStdString();
    // The whole import is prepared before the one step of undo.
    auto imported = song_->song();
    const auto landing = blokkily::notation::add_score(imported, score);
    if (landing.notes == 0) {
        status_ = QStringLiteral("MusicXML import failed · No notes in the score");
        emit statusChanged();
        return false;
    }
    // New tracks sing on the song's first instrument, or the default sound.
    const blokkily::InstrumentSlot* voice = nullptr;
    for (const auto& track : imported.tracks)
        if (!track.instrument.format.empty()) {
            voice = &track.instrument;
            break;
        }
    std::optional<blokkily::InstrumentSlot> sound = voice ? std::optional(*voice) : std::nullopt;
    for (const auto track : landing.tracks) {
        auto& slot = imported.tracks[track].instrument;
        if (!slot.format.empty()) continue;
        if (!sound) {
            const auto bank = findDefaultBank(blokkily::SoundFontCatalog::system_paths());
            if (bank.empty()) break;
            sound = defaultSlot(bank, imported.tracks[track].name);
        }
        slot = *sound;
    }
    song_->checkpoint();
    song_->song() = std::move(imported);
    song_->refreshStructure();
    song_->selectSection(static_cast<int>(landing.section));
    song_->selectTrack(static_cast<int>(landing.tracks.front()));
    status_ = QString("Imported %1 notes on %2 tracks from %3%4")
                  .arg(landing.notes)
                  .arg(landing.tracks.size())
                  .arg(QFileInfo(file_path).fileName(),
                       landing.dropped > 0 ? QString(" · %1 out of range").arg(landing.dropped) : QString());
    emit statusChanged();
    return true;
}

bool AppController::exportMusicXmlFile(const QString& path) {
    if (song_ == nullptr) return false;
    const QString file_path = local_file(path);
    const auto& song = song_->song();
    const QString title = projectName().isEmpty() ? QFileInfo(file_path).completeBaseName() : projectName();
    const auto score = blokkily::notation::song_score(song, title.toStdString(),
                                                      static_cast<std::size_t>(std::max(0, song_->currentSection())));
    const auto fail = [this](const QString& why) {
        export_status_ = QString("MusicXML export failed · %1").arg(why);
        emit exportStatusChanged();
        return false;
    };
    if (score.parts.empty()) return fail(QStringLiteral("Nothing to write"));
    const std::string xml = blokkily::notation::write(score);
    QSaveFile out(file_path);
    if (!out.open(QIODevice::WriteOnly) ||
        out.write(xml.c_str(), static_cast<qint64>(xml.size())) != static_cast<qint64>(xml.size()) || !out.commit())
        return fail(out.errorString());

    // The export rule: the file is read back, and what it plays is compared
    // with what the song plays.
    QFile written(file_path);
    if (!written.open(QIODevice::ReadOnly)) return fail(QStringLiteral("could not read it back"));
    blokkily::notation::Score back;
    try {
        back = blokkily::notation::parse(written.readAll().toStdString());
    } catch (const std::exception& error) {
        return fail(QString::fromUtf8(error.what()));
    }
    std::vector<Played> heard;
    for (const auto& part : back.parts)
        for (const auto& staff : part.staves) play_into(blokkily::notation::to_pattern(staff, 480), heard);
    std::vector<Played> sung;
    blokkily::Tick end = 0;
    for (const auto& clip : song.clips)
        if (clip.pattern < song.patterns.size())
            end = std::max(end, clip.start + clip.span(song.patterns[clip.pattern].pattern.length()));
    if (end > 0) {
        for (std::size_t track = 0; track < song.tracks.size(); ++track)
            play_into(blokkily::notation::track_timeline(song, track, end), sung);
    } else {
        for (std::size_t track = 0; track < song.tracks.size(); ++track)
            if (const auto part = song.part(static_cast<std::size_t>(song_->currentSection()), track))
                play_into(song.patterns[*part].pattern, sung);
    }
    std::sort(heard.begin(), heard.end());
    std::sort(sung.begin(), sung.end());
    last_score_export_matches_ = heard == sung;
    export_status_ = QString("%1 · %2 parts · %3")
                         .arg(QFileInfo(file_path).fileName())
                         .arg(score.parts.size())
                         .arg(last_score_export_matches_ ? QStringLiteral("plays back the same")
                                                         : QStringLiteral("differs where notation cannot say it"));
    emit exportStatusChanged();
    return true;
}
