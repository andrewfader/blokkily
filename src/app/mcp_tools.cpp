#include "mcp_tools.hpp"

#include "../guide/guide_controller.hpp"
#include "app_controller.hpp"
#include "blokkily/guide/event_bus.hpp"
#include "blokkily/model/song.hpp"
#include "blokkily/notation/clef.hpp"
#include "blokkily/notation/duration.hpp"
#include "blokkily/notation/layout.hpp"
#include "blokkily/notation/lyrics.hpp"
#include "blokkily/notation/musicxml.hpp"
#include "blokkily/notation/pattern_bridge.hpp"
#include "blokkily/notation/pitch.hpp"
#include "blokkily/notation/signature.hpp"
#include "blokkily/notation/spelling.hpp"
#include "blokkily/sequencer/scheduler.hpp"
#include "mcp_protocol.hpp"
#include "pattern_model.hpp"
#include "song_model.hpp"
#include "transport.hpp"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSaveFile>

#include <cmath>
#include <algorithm>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <variant>
#include <vector>

namespace {

using blokkily::mcp::jsonResult;
using blokkily::mcp::textResult;

QJsonObject failure(const QString& text) { return textResult(text, true); }

// How far a note may be pushed off its step, in ticks: under half a
// sixteenth either way, as the step editor's microtiming allows.
constexpr int max_offset = 59;

// "C4" is 60, as the window names keys; a sharp or flat follows the letter.
std::optional<int> keyOf(const QJsonValue& value) {
    if (value.isDouble()) {
        const double key = value.toDouble();
        if (key < 0 || key > 127 || key != std::floor(key)) return std::nullopt;
        return static_cast<int>(key);
    }
    static const QRegularExpression name(QStringLiteral("^([A-Ga-g])([#b]?)(-?\\d+)$"));
    const auto match = name.match(value.toString().trimmed());
    if (!match.hasMatch()) return std::nullopt;
    static const int semitones[] = {9, 11, 0, 2, 4, 5, 7};  // A B C D E F G
    int key = semitones[match.captured(1).toUpper().at(0).unicode() - 'A'];
    if (match.captured(2) == "#") ++key;
    if (match.captured(2) == "b") --key;
    key += (match.captured(3).toInt() + 1) * 12;
    if (key < 0 || key > 127) return std::nullopt;
    return key;
}

QJsonObject pick(const QVariantMap& row, std::initializer_list<const char*> keys) {
    QJsonObject picked;
    for (const char* key : keys)
        if (row.contains(key)) picked.insert(key, QJsonValue::fromVariant(row.value(key)));
    return picked;
}

// The processor a parameter tool addresses: a track's instrument or insert by
// default, or an insert on a return or the master.
struct Bus {
    QString kind;
    int bus = 0;
    blokkily::BusKind address_kind = blokkily::BusKind::track;
    QString error;
};

Bus busOf(const QJsonObject& arguments, int selected_track, int tracks, int returns) {
    const bool on_return = arguments.contains("return");
    const bool on_master = arguments.value("master").toBool();
    if (int(arguments.contains("track")) + int(on_return) + int(on_master) > 1)
        return {.error = QStringLiteral("Name a track, a return or the master, not more than one.")};
    if (on_master) return {QStringLiteral("master"), 0, blokkily::BusKind::master, {}};
    if (on_return) {
        const int bus = arguments.value("return").toInt(-1);
        if (bus < 0 || bus >= returns) return {.error = QStringLiteral("There is no return %1.").arg(bus)};
        return {QStringLiteral("return"), bus, blokkily::BusKind::ret, {}};
    }
    const int track = arguments.value("track").toInt(selected_track);
    if (track < 0 || track >= tracks) return {.error = QStringLiteral("There is no track %1.").arg(track)};
    return {QStringLiteral("track"), track, blokkily::BusKind::track, {}};
}

// Says in a result which bus was addressed, as the arguments did.
void nameBus(QJsonObject& result, const Bus& bus) {
    if (bus.kind == "master") result.insert("master", true);
    else result.insert(bus.kind, bus.bus);
}

QString busName(const Bus& bus) {
    return bus.kind == "master" ? QStringLiteral("The master")
         : bus.kind == "return" ? QStringLiteral("Return %1").arg(bus.bus)
                                : QStringLiteral("Track %1").arg(bus.bus);
}

bool listed(const QMetaMethod& method) {
    return method.access() == QMetaMethod::Public &&
           (method.methodType() == QMetaMethod::Method || method.methodType() == QMetaMethod::Slot);
}

QString signatureOf(const QMetaMethod& method) {
    QStringList parameters;
    const auto names = method.parameterNames();
    for (int index = 0; index < method.parameterCount(); ++index)
        parameters << QStringLiteral("%1 %2")
                          .arg(QString::fromLatin1(method.parameterTypeName(index)),
                               QString::fromLatin1(names.value(index)));
    return QStringLiteral("%1 %2(%3)")
        .arg(QString::fromLatin1(method.typeName()), QString::fromLatin1(method.name()),
             parameters.join(QStringLiteral(", ")));
}

}  // namespace

McpTools::McpTools(SongModel& song, PatternModel& pattern, AppController& controller,
                   Transport& transport, QQuickWindow* window, GuideController* guide)
    : song_(song), pattern_(pattern), controller_(controller), transport_(transport),
      window_(window), guide_(guide) {}

QJsonObject McpTools::call(const QString& name, const QJsonObject& arguments) {
    // The guide's narration feed attributes what follows: everything the call
    // does is stamped source=mcp, and the stamp it found is put back
    // afterwards, so a later click is never misattributed and a call that
    // lands while the assistant is applying its proposal leaves that work
    // stamped as the assistant's.
    // Both models are stamped: tools reach the pattern model as well as the
    // controller (e.g. through the generic call/invoke tool).
    // The stamp is put back on the way out however the call ends, a throw
    // included, as the guide's own Acting does.
    class Stamped {
    public:
        Stamped(AppController& controller, PatternModel& pattern)
            : controller_(controller), pattern_(pattern), controller_source_(controller.eventSource()),
              pattern_source_(pattern.eventSource()) {
            controller_.setEventSource(blokkily::guide::EventSource::mcp);
            pattern_.setEventSource(blokkily::guide::EventSource::mcp);
        }
        ~Stamped() {
            controller_.setEventSource(controller_source_);
            pattern_.setEventSource(pattern_source_);
        }
        Stamped(const Stamped&) = delete;
        Stamped& operator=(const Stamped&) = delete;

    private:
        AppController& controller_;
        PatternModel& pattern_;
        blokkily::guide::EventSource controller_source_;
        blokkily::guide::EventSource pattern_source_;
    };
    const Stamped stamped(controller_, pattern_);
    return dispatch(name, arguments);
}

QJsonObject McpTools::dispatch(const QString& name, const QJsonObject& arguments) {
    if (name == "get_session") return getSession();
    if (name == "get_pattern") return getPattern(arguments);
    if (name == "write_notes") return writeNotes(arguments);
    if (name == "set_section") return setSection(arguments);
    if (name == "set_lyrics") return setLyrics(arguments);
    if (name == "place_section") return placeSection(arguments);
    if (name == "transport") return runTransport(arguments);
    if (name == "list_plugins") return listPlugins();
    if (name == "set_instrument") return setInstrument(arguments);
    if (name == "list_parameters") return listParameters(arguments);
    if (name == "set_parameter") return setParameter(arguments);
    if (name == "set_soundfont_preset") return setSoundFontPreset(arguments);
    if (name == "screenshot") return screenshot();
    if (name == "analyze_render") return analyzeRender(arguments);
    if (name == "describe") return describe(arguments);
    if (name == "call") return invoke(arguments);
    if (name == "get_property") return getProperty(arguments);
    if (name == "get_score") return getScore(arguments);
    if (name == "set_clef") return setClef(arguments);
    if (name == "set_key_signature") return setKeySignature(arguments);
    if (name == "set_time_signature") return setTimeSignature(arguments);
    if (name == "set_meter") return setMeter(arguments);
    if (name == "export_musicxml") return exportMusicXml(arguments);
    if (name == "import_musicxml") return importMusicXml(arguments);
    if (name == "guide_events") return guideEvents(arguments);
    if (name == "guide_lesson") return guideLesson(arguments);
    if (name == "guide_ask") return guideAsk(arguments);
    return failure(QStringLiteral("Unknown tool: %1").arg(name));
}

QObject* McpTools::object(const QString& name) const {
    if (name == "songModel") return &song_;
    if (name == "patternModel") return &pattern_;
    if (name == "appController") return &controller_;
    if (name == "transport") return &transport_;
    return nullptr;
}

QJsonObject McpTools::transportState() const {
    return {{"playing", transport_.playing()},
            {"bar", transport_.bar()},
            {"position", transport_.position()},
            {"bpm", transport_.bpm()},
            {"meter", transport_.meterText()},
            {"recordArmed", controller_.recordArmed()}};
}

QJsonArray McpTools::effects(const std::vector<blokkily::EffectSlot>& chain) const {
    QJsonArray rows;
    for (std::size_t slot = 0; slot < chain.size(); ++slot)
        rows.append(QJsonObject{{"slot", static_cast<int>(slot)},
                                {"name", song_.effectLabel(chain[slot].plugin)},
                                {"bypass", chain[slot].bypass}});
    return rows;
}

QJsonObject McpTools::getSession() const {
    const auto& song = song_.song();
    QJsonArray tracks;
    const auto rows = song_.tracks();
    for (int index = 0; index < rows.size(); ++index) {
        auto track = pick(rows.at(index).toMap(), {"index", "name", "instrument", "hasInstrument", "gainDb",
                                                   "pan", "mute", "solo", "armed", "selected", "inserts"});
        if (static_cast<std::size_t>(index) < song.tracks.size()) {
            const auto& model = song.tracks[static_cast<std::size_t>(index)];
            track.insert("effects", effects(model.inserts));
            QJsonArray sends;
            for (const auto& send : model.sends)
                sends.append(QJsonObject{{"return", static_cast<int>(send.bus)},
                                         {"levelDb", send.level_db},
                                         {"preFader", send.pre_fader}});
            track.insert("sends", sends);
        }
        tracks.append(track);
    }
    QJsonArray returns;
    for (std::size_t index = 0; index < song.returns.size(); ++index) {
        const auto& bus = song.returns[index];
        returns.append(QJsonObject{{"index", static_cast<int>(index)},
                                   {"name", QString::fromStdString(bus.name)},
                                   {"gainDb", bus.mix.gain_db},
                                   {"pan", bus.mix.pan},
                                   {"mute", bus.mix.mute},
                                   {"effects", effects(bus.inserts)}});
    }
    QJsonArray patterns;
    const auto sections = song_.sections();
    for (int index = 0; index < sections.size(); ++index) {
        auto section = pick(sections.at(index).toMap(), {"index", "name", "events", "current"});
        const int part = song_.partOf(index, 0);
        if (part >= 0) {
            const auto& pattern = song.patterns.at(static_cast<std::size_t>(part)).pattern;
            section.insert("steps", static_cast<int>(pattern.length() / std::max<blokkily::Tick>(1, pattern.ticks_per_beat() / 4)));
            section.insert("swing", pattern.swing());
        }
        patterns.append(section);
    }
    QJsonArray clips;
    for (const auto& row : song_.clips())
        clips.append(pick(row.toMap(), {"track", "name", "bar", "bars"}));
    QJsonArray meters;
    for (const auto& change : song.meter.changes)
        meters.append(QJsonObject{{"bar", change.bar},
                                  {"numerator", change.numerator},
                                  {"denominator", change.denominator}});
    // Where the last clip ends, in bars: the song an export renders. `bars`
    // is what the arrangement shows, a spare bar and at least eight.
    const auto length = song.length();
    const int length_bars = length > 0 ? song.meter.bar_at(length - 1) + 1 : 0;
    const auto& open = song_.editPattern();
    return jsonResult(QJsonObject{
        {"project", controller_.projectPath()},
        {"unsavedChanges", song_.dirty()},
        {"canUndo", song_.canUndo()},
        {"canRedo", song_.canRedo()},
        {"tracks", tracks},
        {"patterns", patterns},
        {"clips", clips},
        {"bars", song_.bars()},
        {"lengthBars", length_bars},
        {"meters", meters},
        {"selectedTrack", song_.selectedTrack()},
        {"openPattern", song_.currentSection()},
        {"openPatternSteps",
         static_cast<int>(open.length() / std::max<blokkily::Tick>(1, open.ticks_per_beat() / 4))},
        {"tuning", song_.tuningName()},
        {"scale", song_.scaleName()},
        {"root", song_.rootName()},
        {"masterGainDb", song_.masterGainDb()},
        {"returns", returns},
        {"masterEffects", effects(song.master_inserts)},
        {"transport", transportState()},
        {"status", controller_.status()},
    });
}

QJsonObject McpTools::getPattern(const QJsonObject& arguments) const {
    const int track = arguments.value("track").toInt(song_.selectedTrack());
    const int section = arguments.value("section").toInt(song_.currentSection());
    if (track < 0 || track >= song_.trackCount())
        return failure(QStringLiteral("There is no track %1.").arg(track));
    if (section < 0 || section >= song_.sections().size())
        return failure(QStringLiteral("There is no pattern %1.").arg(section));
    const int part = song_.partOf(section, track);
    if (part < 0) return failure(QStringLiteral("Track %1 has no part of pattern %2.").arg(track).arg(section));
    const auto& pattern = song_.song().patterns.at(static_cast<std::size_t>(part)).pattern;
    const double step = static_cast<double>(std::max<blokkily::Tick>(1, pattern.ticks_per_beat() / 4));
    QJsonArray notes;
    for (const auto& trigger : pattern.events()) {
        QJsonObject entry{{"step", static_cast<double>(trigger.start) / step},
                          {"length", static_cast<double>(trigger.duration) / step}};
        if (trigger.micro_offset != 0) entry.insert("offset", static_cast<int>(trigger.micro_offset));
        if (const auto* note = std::get_if<blokkily::Note>(&trigger.musical_data)) {
            entry.insert("key", note->key);
            entry.insert("velocity", note->velocity);
            if (note->cents != 0.0) entry.insert("cents", note->cents);
        } else if (const auto* chord = std::get_if<blokkily::Chord>(&trigger.musical_data)) {
            QJsonArray keys;
            for (const auto interval : chord->intervals) keys.append(chord->root + interval);
            entry.insert("keys", keys);
        }
        if (!trigger.lyric.empty()) entry.insert("lyric", QString::fromStdString(trigger.lyric));
        if (!trigger.locks.empty()) {
            QJsonArray locks;
            for (const auto& lock : trigger.locks)
                locks.append(QJsonObject{
                    {"slot", lock.slot},
                    {"bus", lock.bus_kind == blokkily::BusKind::ret      ? QString("return %1").arg(lock.bus)
                            : lock.bus_kind == blokkily::BusKind::master ? QString("master")
                                                                         : QString("track")},
                    {"parameter", lock.parameter_index},
                    {"name", QString::fromStdString(lock.parameter_id)},
                    {"value", lock.value},
                    {"modulation", lock.kind == blokkily::ParameterLock::Kind::modulation}});
            entry.insert("locks", locks);
        }
        notes.append(entry);
    }
    return jsonResult(QJsonObject{
        {"track", track},
        {"pattern", section},
        {"name", song_.sections().at(section).toMap().value("name").toString()},
        {"steps", static_cast<double>(pattern.length()) / step},
        {"notes", notes},
    });
}

QJsonObject McpTools::writeNotes(const QJsonObject& arguments) {
    const int track = arguments.value("track").toInt(song_.selectedTrack());
    const int section = arguments.value("section").toInt(song_.currentSection());
    if (track < 0 || track >= song_.trackCount())
        return failure(QStringLiteral("There is no track %1.").arg(track));
    if (section < 0 || section >= song_.sections().size())
        return failure(QStringLiteral("There is no pattern %1.").arg(section));
    // Every note is checked before anything is written, so a bad one leaves
    // the pattern as it was rather than half-written.
    struct Wanted {
        double step;
        double length;
        int key;
        float velocity;
        double cents;
        std::string lyric;
        blokkily::Tick offset;
    };
    std::vector<Wanted> wanted;
    const auto notes = arguments.value("notes").toArray();
    for (int index = 0; index < notes.size(); ++index) {
        const auto note = notes.at(index).toObject();
        const auto key = keyOf(note.value("key"));
        const double step = note.value("step").toDouble(-1.0);
        const double length = note.value("length").toDouble(1.0);
        const double velocity = note.value("velocity").toDouble(0.8);
        const double cents = note.value("cents").toDouble(0.0);
        const double offset = note.value("offset").toDouble(0.0);
        if (!key) return failure(QStringLiteral("Note %1: key must be 0 to 127 or a name like C4.").arg(index));
        if (step < 0) return failure(QStringLiteral("Note %1: step must be 0 or more.").arg(index));
        if (length <= 0) return failure(QStringLiteral("Note %1: length must be more than 0.").arg(index));
        if (velocity < 0 || velocity > 1) return failure(QStringLiteral("Note %1: velocity must be 0 to 1.").arg(index));
        if (!(cents >= -100.0 && cents <= 100.0))
            return failure(QStringLiteral("Note %1: cents must be -100 to 100.").arg(index));
        if (!(offset >= -max_offset && offset <= max_offset) || offset != std::floor(offset))
            return failure(QStringLiteral("Note %1: offset must be a whole number of ticks, -%2 to %2.")
                               .arg(index).arg(max_offset));
        const std::string lyric = note.value("lyric").toString().trimmed().toStdString();
        if (lyric.size() > 256) return failure(QStringLiteral("Note %1: a lyric is one syllable, at most 256 bytes.").arg(index));
        wanted.push_back({step, length, *key, static_cast<float>(velocity), cents, lyric,
                          static_cast<blokkily::Tick>(offset)});
    }
    // The part is opened, as a click on its lane would, so the editors show
    // what is written.
    song_.selectSection(section);
    song_.selectTrack(track);
    const auto& open = song_.editPattern();
    const double step_ticks = static_cast<double>(std::max<blokkily::Tick>(1, open.ticks_per_beat() / 4));
    const double steps = static_cast<double>(open.length()) / step_ticks;
    for (std::size_t index = 0; index < wanted.size(); ++index)
        if (wanted[index].step >= steps)
            return failure(QStringLiteral("Note %1: step %2 is past the pattern's %3 steps.")
                               .arg(index).arg(wanted[index].step).arg(steps));

    song_.checkpoint();
    auto& pattern = song_.editPattern();
    if (arguments.value("replace").toBool()) {
        std::vector<blokkily::EventId> ids;
        for (const auto& trigger : pattern.events()) ids.push_back(trigger.id);
        for (const auto id : ids) (void)pattern.remove(id);
    }
    int written = 0;
    for (const auto& note : wanted) {
        blokkily::Trigger trigger;
        trigger.start = static_cast<blokkily::Tick>(std::llround(note.step * step_ticks));
        trigger.duration = std::max<blokkily::Tick>(
            1, std::min<blokkily::Tick>(static_cast<blokkily::Tick>(std::llround(note.length * step_ticks)),
                                        pattern.length() - trigger.start));
        trigger.musical_data = blokkily::Note{static_cast<std::int16_t>(note.key), note.velocity, 0.0F, note.cents};
        trigger.lyric = note.lyric;
        trigger.micro_offset = note.offset;
        if (pattern.add(std::move(trigger)) != 0) ++written;
    }
    song_.notifyStructureChanged();
    // Direct model write, past the emitting commands: attribute it by hand so
    // the guide's feed tells the producer their agent did this.
    if (written > 0)
        controller_.eventBus().publish(
            {blokkily::guide::EventSource::mcp, blokkily::guide::EventKind::note_added,
             std::to_string(written) + " notes",
             "track=" + std::to_string(track) + " section=" + std::to_string(section)});
    return jsonResult(QJsonObject{{"written", written},
                                  {"refused", static_cast<int>(wanted.size()) - written},
                                  {"track", track},
                                  {"pattern", section},
                                  {"notes", static_cast<int>(pattern.events().size())}});
}

// Opening the section and acting on it happen in one call: while the song
// plays, the window opens whichever section the playhead is in, so an open
// section chosen by one call may be gone by the next.
QJsonObject McpTools::setSection(const QJsonObject& arguments) {
    const int section = arguments.value("section").toInt(-1);
    if (section < 0 || section >= song_.sections().size())
        return failure(QStringLiteral("There is no pattern %1.").arg(section));
    const bool steps = arguments.contains("steps"), swing = arguments.contains("swing"),
               name = arguments.contains("name");
    if (!steps && !swing && !name) return failure(QStringLiteral("Give steps, swing or name."));
    // Every value is checked before anything changes.
    if (steps && arguments.value("steps").toInt(0) < 1)
        return failure(QStringLiteral("steps must be 1 or more."));
    const int amount = arguments.value("swing").toInt(-1);
    if (swing && (amount < 0 || amount > 100)) return failure(QStringLiteral("swing is 0 to 100."));
    if (name && arguments.value("name").toString().trimmed().isEmpty())
        return failure(QStringLiteral("name must not be empty."));
    song_.selectSection(section);
    if (steps && !song_.setPatternSteps(arguments.value("steps").toInt()))
        return failure(QStringLiteral("Pattern %1 could not be made %2 steps long.").arg(section).arg(arguments.value("steps").toInt()));
    if (swing) (void)song_.setPatternSwing(amount);
    if (name) song_.renamePattern(section, arguments.value("name").toString().trimmed());
    const auto& open = song_.editPattern();
    return jsonResult(QJsonObject{
        {"section", section},
        {"name", song_.sections().at(section).toMap().value("name").toString()},
        {"steps", static_cast<int>(open.length() / std::max<blokkily::Tick>(1, open.ticks_per_beat() / 4))},
        {"swing", open.swing()}});
}

QJsonObject McpTools::setLyrics(const QJsonObject& arguments) {
    const int track = arguments.value("track").toInt(song_.selectedTrack());
    const int section = arguments.value("section").toInt(song_.currentSection());
    if (track < 0 || track >= song_.trackCount()) return failure(QStringLiteral("There is no track %1.").arg(track));
    if (section < 0 || section >= song_.sections().size())
        return failure(QStringLiteral("There is no pattern %1.").arg(section));
    if (!arguments.value("text").isString()) return failure(QStringLiteral("set_lyrics needs text."));
    const QString text = arguments.value("text").toString();
    song_.selectSection(section);
    song_.selectTrack(track);
    const int placed = pattern_.setLyrics(text);
    const auto syllables = static_cast<int>(blokkily::notation::split_lyrics(text.toStdString()).size());
    return jsonResult(QJsonObject{{"track", track},
                                  {"pattern", section},
                                  {"placed", placed},
                                  {"syllables", syllables},
                                  {"lyrics", pattern_.lyricsText()}});
}

QJsonObject McpTools::placeSection(const QJsonObject& arguments) {
    const int section = arguments.value("section").toInt(-1);
    const int bar = arguments.value("bar").toInt(-1);
    if (section < 0 || section >= song_.sections().size())
        return failure(QStringLiteral("There is no pattern %1.").arg(section));
    if (bar < 0) return failure(QStringLiteral("bar must be 0 or more."));
    const bool one = arguments.contains("track");
    const int track = arguments.value("track").toInt(-1);
    if (one && (track < 0 || track >= song_.trackCount()))
        return failure(QStringLiteral("There is no track %1.").arg(track));
    // Placing a section where it already is changes nothing and is no
    // error: the arrangement is already what was asked for.
    const auto& song = song_.song();
    const auto start = song.meter.bar_start(bar);
    std::vector<int> wanted_tracks;
    for (int index = 0; index < song_.trackCount(); ++index)
        if ((!one || index == track) && song_.partOf(section, index) >= 0) wanted_tracks.push_back(index);
    const auto there = [&](int index) {
        const auto part = static_cast<std::size_t>(song_.partOf(section, index));
        return std::any_of(song.clips.begin(), song.clips.end(), [&](const blokkily::Clip& clip) {
            return static_cast<int>(clip.track) == index && clip.pattern == part && clip.start == start;
        });
    };
    if (!wanted_tracks.empty() && std::all_of(wanted_tracks.begin(), wanted_tracks.end(), there))
        return jsonResult(QJsonObject{{"section", section}, {"bar", bar}, {"placed", 0},
                                      {"alreadyPlaced", true}, {"bars", song_.bars()}});
    const auto before = song.clips.size();
    song_.selectSection(section);
    if (one) song_.placeClip(track, bar);
    else song_.placeSection(bar);
    const auto placed = static_cast<int>(song_.song().clips.size() - before);
    if (placed <= 0) {
        // Name what is in the way: a clip anywhere in the section's span
        // blocks it, not only one starting at `bar`.
        const auto part = song_.partOf(section, one ? track : wanted_tracks.empty() ? 0 : wanted_tracks.front());
        const auto span = part >= 0 ? song.patterns.at(static_cast<std::size_t>(part)).pattern.length() : 0;
        QStringList blocking;
        for (const auto& clip : song.clips) {
            if (one && static_cast<int>(clip.track) != track) continue;
            const auto clip_span = clip.span(song.patterns.at(clip.pattern).pattern.length());
            if (clip.start < start + span && start < clip.start + clip_span)
                blocking << QStringLiteral("track %1 has %2 from bar %3")
                                .arg(clip.track)
                                .arg(QString::fromStdString(song.patterns.at(clip.pattern).name))
                                .arg(song.meter.bar_at(clip.start));
            if (blocking.size() == 3) break;
        }
        return failure(QStringLiteral("Nothing was placed: the bars from %1 are taken (%2).")
                           .arg(bar)
                           .arg(blocking.isEmpty() ? QStringLiteral("no track has a part of it")
                                                   : blocking.join(QStringLiteral("; "))));
    }
    return jsonResult(QJsonObject{{"section", section}, {"bar", bar}, {"placed", placed},
                                  {"bars", song_.bars()}});
}

QJsonObject McpTools::runTransport(const QJsonObject& arguments) {
    const QString action = arguments.value("action").toString();
    if (action == "play") {
        if (!transport_.playing()) controller_.togglePlayback();
    } else if (action == "stop") {
        if (transport_.playing()) controller_.togglePlayback();
    } else if (action == "rewind") {
        controller_.rewindPlayback();
    } else if (action == "record") {
        controller_.toggleRecord();
    } else if (action == "seek") {
        if (!arguments.contains("bar")) return failure(QStringLiteral("seek needs a bar."));
        controller_.seekToBar(arguments.value("bar").toInt());
    } else {
        return failure(QStringLiteral("Unknown action: %1").arg(action));
    }
    return jsonResult(transportState());
}

QJsonObject McpTools::listPlugins() const {
    QJsonArray plugins;
    const auto entries = controller_.plugins();
    for (int index = 0; index < entries.size(); ++index) {
        auto entry = pick(entries.at(index).toMap(), {"name", "vendor", "format", "kind"});
        entry.insert("index", index);
        plugins.append(entry);
    }
    return jsonResult(QJsonObject{{"plugins", plugins}, {"scanning", controller_.scanning()}});
}

QJsonObject McpTools::setInstrument(const QJsonObject& arguments) {
    const int track = arguments.value("track").toInt(song_.selectedTrack());
    const int plugin = arguments.value("plugin").toInt(-1);
    if (track < 0 || track >= song_.trackCount())
        return failure(QStringLiteral("There is no track %1.").arg(track));
    const auto entries = controller_.plugins();
    if (plugin < 0 || plugin >= entries.size())
        return failure(QStringLiteral("There is no plugin %1; list_plugins lists them.").arg(plugin));
    if (entries.at(plugin).toMap().value("kind").toString() == "effect")
        return failure(QStringLiteral("Plugin %1 is an effect, not an instrument.").arg(plugin));
    song_.selectTrack(track);
    if (!controller_.selectInstrument(plugin))
        return failure(QStringLiteral("The instrument could not be loaded: %1").arg(controller_.status()));
    return jsonResult(pick(song_.tracks().at(track).toMap(), {"index", "name", "instrument"}));
}

QJsonObject McpTools::analyzeRender(const QJsonObject& arguments) {
    const bool track = arguments.contains("track");
    const bool bus = arguments.contains("return");
    const bool file = arguments.contains("file");
    const bool all = arguments.value("all").toBool();
    if (int(track) + int(bus) + int(file) + int(all) > 1)
        return failure(QStringLiteral("Analyze one of track, return, all or file, or none for the master."));
    const int from = arguments.value("fromBar").toInt(0);
    const int to = arguments.value("toBar").toInt(-1);
    if (file && (arguments.contains("fromBar") || arguments.contains("toBar")))
        return failure(QStringLiteral("fromBar and toBar choose bars of the song, not of a file."));
    if (all) {
        const auto analyses = controller_.analyzeEveryBus(from, to);
        if (analyses.size() == 1 && analyses.front().toMap().contains("error"))
            return failure(analyses.front().toMap().value("error").toString());
        return jsonResult(QJsonObject{{"buses", QJsonArray::fromVariantList(analyses)}});
    }
    const auto analysis =
        file ? controller_.analyzeAudioFile(arguments.value("file").toString())
        : track ? controller_.analyzeRender(QStringLiteral("track"), arguments.value("track").toInt(-1), from, to)
        : bus ? controller_.analyzeRender(QStringLiteral("return"), arguments.value("return").toInt(-1), from, to)
              : controller_.analyzeRender(QStringLiteral("master"), 0, from, to);
    if (analysis.contains("error")) return failure(analysis.value("error").toString());
    return jsonResult(QJsonObject::fromVariantMap(analysis));
}

QJsonObject McpTools::listParameters(const QJsonObject& arguments) const {
    const auto bus = busOf(arguments, song_.selectedTrack(), song_.trackCount(),
                           static_cast<int>(song_.song().returns.size()));
    if (!bus.error.isEmpty()) return failure(bus.error);
    const int slot = arguments.value("slot").toInt(bus.kind == "track" ? -1 : 0);
    const auto rows = controller_.insertParameters(bus.kind, bus.bus, slot);
    if (rows.isEmpty())
        return failure(slot < 0 ? QStringLiteral("%1's instrument has no parameters to set (or none is loaded).").arg(busName(bus))
                                : QStringLiteral("%1 has no effect in slot %2 with parameters.").arg(busName(bus)).arg(slot));
    QJsonArray parameters;
    for (const auto& row : rows) parameters.append(QJsonObject::fromVariantMap(row.toMap()));
    QJsonObject result{{"slot", slot}, {"parameters", parameters}};
    nameBus(result, bus);
    return jsonResult(result);
}

QJsonObject McpTools::setParameter(const QJsonObject& arguments) {
    const auto bus = busOf(arguments, song_.selectedTrack(), song_.trackCount(),
                           static_cast<int>(song_.song().returns.size()));
    if (!bus.error.isEmpty()) return failure(bus.error);
    const int slot = arguments.value("slot").toInt(bus.kind == "track" ? -1 : 0);
    // The parameter by its id, or by its name as list_parameters shows it.
    const auto wanted = arguments.value("parameter");
    QVariantMap found;
    for (const auto& row : controller_.insertParameters(bus.kind, bus.bus, slot)) {
        const auto entry = row.toMap();
        if ((wanted.isDouble() && entry.value("id").toInt() == wanted.toInt()) ||
            (wanted.isString() && entry.value("name").toString().compare(wanted.toString(), Qt::CaseInsensitive) == 0))
            found = entry;
    }
    if (found.isEmpty())
        return failure(QStringLiteral("No parameter %1 there; list_parameters lists them.")
                           .arg(wanted.isString() ? wanted.toString() : QString::number(wanted.toInt())));
    const double value = arguments.value("value").toDouble();
    const double low = found.value("minimum").toDouble(), high = found.value("maximum").toDouble();
    if (value < low || value > high)
        return failure(QStringLiteral("%1 takes %2 to %3.").arg(found.value("name").toString()).arg(low).arg(high));
    const int id = found.value("id").toInt();
    const blokkily::ProcessorAddress where{bus.address_kind, static_cast<std::uint32_t>(bus.bus), slot};
    // The track whose lane holds the parameter: a track's own, or for a
    // return or the master, whichever track already has a lane for it, else
    // the selected track, where the window's controls would put one.
    const auto lane_on = [&](int owner) {
        const auto& lanes = song_.song().tracks.at(static_cast<std::size_t>(owner)).automation;
        for (std::size_t index = 0; index < lanes.size(); ++index)
            if (lanes[index].target.kind == blokkily::AutomationTarget::Kind::parameter &&
                lanes[index].target.processor == where && lanes[index].target.parameter_index == id)
                return static_cast<int>(index);
        return -1;
    };
    int owner = bus.kind == "track" ? bus.bus : song_.selectedTrack();
    if (bus.kind != "track")
        for (int track = 0; track < song_.trackCount(); ++track)
            if (lane_on(track) >= 0) owner = track;
    if (lane_on(owner) < 0) {
        if (!controller_.automateInsert(bus.kind, bus.bus, slot, id))
            return failure(QStringLiteral("%1 could not be given a lane.").arg(found.value("name").toString()));
    }
    // The lane the parameter has: one value for the whole song, unless it is
    // already automated, which is the producer's to edit.
    const int lane = lane_on(owner);
    if (lane < 0) return failure(QStringLiteral("The lane could not be found."));
    const auto& lanes = song_.song().tracks.at(static_cast<std::size_t>(owner)).automation;
    if (lanes[static_cast<std::size_t>(lane)].points.size() > 1)
        return failure(QStringLiteral("%1 is automated across the song; edit its lane instead.").arg(found.value("name").toString()));
    if (song_.moveAutomationPoint(owner, lane, 0, 0.0, value) < 0)
        return failure(QStringLiteral("%1 could not be set.").arg(found.value("name").toString()));
    QJsonObject result{{"slot", slot}, {"parameter", id}, {"name", found.value("name").toString()},
                       {"value", value}, {"laneTrack", owner}};
    nameBus(result, bus);
    return jsonResult(result);
}

QJsonObject McpTools::setSoundFontPreset(const QJsonObject& arguments) {
    const int track = arguments.value("track").toInt(song_.selectedTrack());
    if (track < 0 || track >= song_.trackCount())
        return failure(QStringLiteral("There is no track %1.").arg(track));
    const auto& slot = song_.song().tracks.at(static_cast<std::size_t>(track)).instrument;
    if (slot.format != "SoundFont")
        return failure(QStringLiteral("Track %1 does not play a SoundFont; set_instrument gives it one.").arg(track));
    const int bank = arguments.value("bank").toInt(-1);
    const int program = arguments.value("program").toInt(-1);
    if (bank < 0 || bank > 128 || program < 0 || program > 127)
        return failure(QStringLiteral("bank is 0 to 128 and program 0 to 127."));
    // A SoundFont track's state is its file, bank and program, one per line.
    const QByteArray state = QByteArray::fromStdString(slot.path) + '\n' + QByteArray::number(bank) + '\n' +
                             QByteArray::number(program);
    if (!controller_.setInstrumentState(track, state))
        return failure(QStringLiteral("That SoundFont has no preset at bank %1, program %2.").arg(bank).arg(program));
    return jsonResult(QJsonObject{{"track", track}, {"bank", bank}, {"program", program},
                                  {"instrument", song_.tracks().at(track).toMap().value("instrument").toString()}});
}

QJsonObject McpTools::screenshot() const {
    if (window_ == nullptr) return failure(QStringLiteral("There is no window to show."));
    const QImage image = window_->grabWindow();
    if (image.isNull()) return failure(QStringLiteral("The window could not be drawn."));
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return {{"content", QJsonArray{QJsonObject{{"type", "image"},
                                               {"data", QString::fromLatin1(png.toBase64())},
                                               {"mimeType", "image/png"}}}}};
}

QJsonObject McpTools::describe(const QJsonObject& arguments) const {
    const QString only = arguments.value("object").toString();
    QJsonObject described;
    for (const char* name : {"songModel", "patternModel", "appController", "transport"}) {
        if (!only.isEmpty() && only != QLatin1String(name)) continue;
        const QMetaObject* meta = object(QLatin1String(name))->metaObject();
        QJsonArray methods;
        QStringList seen;
        for (int index = QObject::staticMetaObject.methodCount(); index < meta->methodCount(); ++index) {
            const auto method = meta->method(index);
            if (!listed(method)) continue;
            const QString signature = signatureOf(method);
            if (seen.contains(signature)) continue;
            seen << signature;
            methods.append(signature);
        }
        QJsonArray properties;
        for (int index = QObject::staticMetaObject.propertyCount(); index < meta->propertyCount(); ++index) {
            const auto property = meta->property(index);
            properties.append(QStringLiteral("%1 %2%3")
                                  .arg(QString::fromLatin1(property.typeName()),
                                       QString::fromLatin1(property.name()),
                                       property.isWritable() ? QStringLiteral(" (writable)") : QString()));
        }
        described.insert(QLatin1String(name), QJsonObject{{"methods", methods}, {"properties", properties}});
    }
    if (described.isEmpty()) return failure(QStringLiteral("Unknown object: %1").arg(only));
    return jsonResult(described);
}

QJsonObject McpTools::invoke(const QJsonObject& arguments) {
    const QString name = arguments.value("object").toString();
    QObject* target = object(name);
    if (target == nullptr) return failure(QStringLiteral("Unknown object: %1").arg(name));
    const QByteArray method_name = arguments.value("method").toString().toLatin1();
    const auto args = arguments.value("args").toArray();
    if (args.size() > 10) return failure(QStringLiteral("At most 10 arguments."));
    const QMetaObject* meta = target->metaObject();
    QStringList tried;
    for (int index = QObject::staticMetaObject.methodCount(); index < meta->methodCount(); ++index) {
        const auto method = meta->method(index);
        if (!listed(method) || method.name() != method_name) continue;
        tried << signatureOf(method);
        if (method.parameterCount() != args.size()) continue;
        // Each argument is converted to the type the method takes; a method
        // that cannot take them is passed over for an overload that can.
        std::vector<QVariant> values;
        bool converts = true;
        for (int at = 0; at < args.size() && converts; ++at) {
            QVariant value = args.at(at).toVariant();
            converts = value.convert(method.parameterMetaType(at));
            values.push_back(std::move(value));
        }
        if (!converts) continue;
        QGenericArgument passed[10];
        for (std::size_t at = 0; at < values.size(); ++at)
            passed[at] = QGenericArgument(values[at].typeName(), values[at].constData());
        QVariant returned;
        QGenericReturnArgument receive;
        if (method.returnMetaType().id() != QMetaType::Void) {
            returned = QVariant(method.returnMetaType());
            receive = QGenericReturnArgument(method.typeName(), returned.data());
        }
        if (!method.invoke(target, Qt::DirectConnection, receive, passed[0], passed[1], passed[2],
                           passed[3], passed[4], passed[5], passed[6], passed[7], passed[8],
                           passed[9]))
            return failure(QStringLiteral("%1.%2 could not be called.").arg(name, QString::fromLatin1(method_name)));
        if (!returned.isValid()) return textResult(QStringLiteral("ok"));
        const QJsonValue value = QJsonValue::fromVariant(returned);
        if (value.isObject() || value.isArray()) return jsonResult(value);
        return textResult(QString::fromUtf8(QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact)).mid(1).chopped(1));
    }
    if (tried.isEmpty())
        return failure(QStringLiteral("%1 has no method %2; describe lists them.").arg(name, QString::fromLatin1(method_name)));
    return failure(QStringLiteral("No %1.%2 takes those arguments. It is: %3")
                       .arg(name, QString::fromLatin1(method_name), tried.join(QStringLiteral("; "))));
}

QJsonObject McpTools::getProperty(const QJsonObject& arguments) const {
    const QString name = arguments.value("object").toString();
    QObject* target = object(name);
    if (target == nullptr) return failure(QStringLiteral("Unknown object: %1").arg(name));
    const QByteArray property = arguments.value("property").toString().toLatin1();
    if (target->metaObject()->indexOfProperty(property.constData()) < 0)
        return failure(QStringLiteral("%1 has no property %2; describe lists them.").arg(name, QString::fromLatin1(property)));
    const QJsonValue value = QJsonValue::fromVariant(target->property(property.constData()));
    if (value.isObject() || value.isArray()) return jsonResult(value);
    return textResult(QString::fromUtf8(QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact)).mid(1).chopped(1));
}

namespace {

// Staff notation (feature/staff-notation): shared argument handling.
struct PartRef {
    int track = -1;
    int section = -1;
    QString error;
};

PartRef resolvePart(SongModel& song, int track, int section) {
    PartRef ref{track, section, {}};
    if (track < 0 || track >= song.trackCount())
        ref.error = QStringLiteral("There is no track %1.").arg(track);
    else if (section < 0 || section >= song.sections().size())
        ref.error = QStringLiteral("There is no pattern %1.").arg(section);
    else if (song.partOf(section, track) < 0)
        ref.error =
            QStringLiteral("Track %1 has no part of pattern %2.").arg(track).arg(section);
    return ref;
}

std::optional<blokkily::notation::ClefKind> clefKindOf(const QString& name) {
    for (int kind = 0; kind <= static_cast<int>(blokkily::notation::ClefKind::Percussion);
         ++kind) {
        const auto clef_kind = static_cast<blokkily::notation::ClefKind>(kind);
        if (QString::fromUtf8(blokkily::notation::Clef(clef_kind).name())
                .compare(name, Qt::CaseInsensitive) == 0)
            return clef_kind;
    }
    return std::nullopt;
}

QString clefNames() {
    QStringList names;
    for (int kind = 0; kind <= static_cast<int>(blokkily::notation::ClefKind::Percussion);
         ++kind)
        names << QString::fromUtf8(
            blokkily::notation::Clef(static_cast<blokkily::notation::ClefKind>(kind)).name());
    return names.join(QStringLiteral(", "));
}

}  // namespace

McpTools::ScoreDressing McpTools::scoreDressing(int track, int section,
                                               const QJsonObject& arguments) {
    // The part's own settings, overridden by the call's. Throws
    // std::invalid_argument on an override that is not one.
    ScoreDressing dressing;
    const QVariantMap stored = song_.notationSettings(track, section);
    const QString clef_name = arguments.value("clef").toString(stored.value("clef").toString());
    const auto kind = clefKindOf(clef_name);
    if (!kind)
        throw std::invalid_argument("unknown clef \"" + clef_name.toStdString() + "\"; one of: " +
                                    clefNames().toStdString());
    dressing.clef = blokkily::notation::Clef(*kind);
    int fifths = stored.value("keyFifths").toInt();
    if (arguments.contains("key")) {
        const auto key = arguments.value("key");
        if (!key.isDouble() || key.toDouble() != std::floor(key.toDouble()))
            throw std::invalid_argument("key is a whole number of fifths, -7 to 7");
        fifths = key.toInt();
    }
    dressing.key = blokkily::notation::KeySignature(fifths); // range-checked
    if (arguments.contains("time")) {
        const QString time = arguments.value("time").toString().trimmed();
        if (time.compare("unmetered", Qt::CaseInsensitive) == 0) {
            dressing.time = blokkily::notation::TimeSignature::unmetered();
        } else {
            static const QRegularExpression meter(QStringLiteral("^(\\d{1,2})/(\\d{1,2})$"));
            const auto match = meter.match(time);
            if (!match.hasMatch()) throw std::invalid_argument("time is \"beats/unit\", such as 3/4, or \"unmetered\"");
            dressing.time = blokkily::notation::TimeSignature(match.captured(1).toInt(), match.captured(2).toInt());
        }
    } else if (stored.value("unmetered").toBool()) {
        dressing.time = blokkily::notation::TimeSignature::unmetered();
    } else {
        dressing.time = blokkily::notation::TimeSignature(stored.value("timeBeats").toInt(),
                                                         stored.value("timeUnit").toInt());
    }
    return dressing;
}

blokkily::notation::Staff McpTools::scoreStaff(int track, int section, const ScoreDressing& dressing) const {
    const int index = song_.partOf(section, track);
    const auto& pattern = song_.song().patterns.at(static_cast<std::size_t>(index)).pattern;
    return blokkily::notation::to_staff(pattern, dressing.key, dressing.clef, dressing.time);
}

QJsonObject McpTools::getScore(const QJsonObject& arguments) {
    const int track = arguments.value("track").toInt(song_.selectedTrack());
    const int section = arguments.value("section").toInt(song_.currentSection());
    const auto part = resolvePart(song_, track, section);
    if (!part.error.isEmpty()) return failure(part.error);
    try {
        const auto dressing = scoreDressing(track, section, arguments);
        const auto staff = scoreStaff(track, section, dressing);
        const auto& pattern =
            song_.song().patterns.at(static_cast<std::size_t>(song_.partOf(section, track))).pattern;
        const blokkily::Tick ticks_per_beat = pattern.ticks_per_beat();
        const double step = static_cast<double>(std::max<blokkily::Tick>(1, ticks_per_beat / 4));
        // The notes and rests as they are written: where each falls, its
        // value, and what it prints. The same reading the score view draws.
        QJsonArray notes;
        QJsonArray rests;
        for (const auto& placed : blokkily::notation::place(staff, ticks_per_beat)) {
            const auto& object = staff.measures[placed.measure].objects[placed.index];
            const auto& value = std::visit(
                [](const auto& o) -> const blokkily::notation::NoteDuration& { return o.duration; }, object);
            QJsonObject entry{{"measure", static_cast<int>(placed.measure) + 1},
                              {"step", static_cast<double>(placed.onset) / step},
                              {"length", static_cast<double>(placed.length) / step},
                              {"value", QString::fromUtf8(blokkily::notation::value_name(value.value))},
                              {"dots", value.dots},
                              {"voice", placed.voice}};
            if (value.tuplet()) entry.insert("tuplet", QStringLiteral("%1:%2").arg(value.tuplet_actual).arg(value.tuplet_normal));
            const auto* note = std::get_if<blokkily::notation::NotationNote>(&object);
            if (note == nullptr) {
                rests.append(entry);
                continue;
            }
            entry.insert("midi", blokkily::notation::to_midi(note->spelled) + staff.transposition());
            entry.insert("spelled", QString::fromUtf8(blokkily::notation::step_name(note->spelled.step)) +
                                        QString::fromUtf8(blokkily::notation::accidental_name(note->spelled.accidental)) +
                                        QString::number(note->spelled.octave));
            entry.insert("staffPosition", dressing.clef.pitched()
                                              ? blokkily::notation::staff_position(note->spelled, dressing.clef)
                                              : 4);
            entry.insert("accidentalShown", placed.accidental_shown);
            entry.insert("chord", note->chord);
            entry.insert("tieStart", note->tie_start);
            entry.insert("tieEnd", note->tie_end);
            entry.insert("velocity", note->velocity);
            if (note->cents != 0.0) entry.insert("cents", note->cents);
            if (!note->lyric.empty()) entry.insert("lyric", QString::fromStdString(note->lyric));
            notes.append(entry);
        }
        blokkily::notation::Score score;
        score.title = song_.sections().at(section).toMap().value("name").toString().toStdString();
        score.parts.push_back({song_.tracks().at(track).toMap().value("name").toString().toStdString(), {staff}});
        const std::string xml = blokkily::notation::write(score);
        return jsonResult(QJsonObject{
            {"track", track},
            {"pattern", section},
            {"name", song_.sections().at(section).toMap().value("name").toString()},
            {"clef", QString::fromUtf8(dressing.clef.name())},
            {"key", QJsonObject{{"fifths", dressing.key.fifths()},
                                {"label", QString::fromStdString(dressing.key.label())}}},
            {"time", QString::fromStdString(dressing.time.label())},
            {"steps", static_cast<double>(pattern.length()) / step},
            {"measures", static_cast<int>(staff.measures.size())},
            {"notes", notes},
            {"rests", rests},
            {"musicxml", QString::fromUtf8(xml.c_str())},
        });
    } catch (const std::exception& e) {
        return failure(QStringLiteral("The score could not be built: %1").arg(e.what()));
    }
}

namespace {

// A JSON value that is a whole number, or nothing.
std::optional<int> wholeNumber(const QJsonValue& value) {
    if (!value.isDouble() || value.toDouble() != std::floor(value.toDouble())) return std::nullopt;
    return value.toInt();
}

// What the scheduler plays from a pattern, note by note.
using Played = std::tuple<blokkily::Tick, int, blokkily::Tick, long long, int>;
std::vector<Played> played(const blokkily::Pattern& pattern) {
    std::vector<Played> notes;
    for (const auto& note : blokkily::Scheduler{}.render_loop(pattern, 0, 0))
        notes.emplace_back(note.start, note.key, note.duration, std::llround(note.cents * 1000.0),
                           static_cast<int>(std::lround(note.velocity * 1000.0F)));
    std::sort(notes.begin(), notes.end());
    return notes;
}

} // namespace

QJsonObject McpTools::setClef(const QJsonObject& arguments) {
    const int track = arguments.value("track").toInt(song_.selectedTrack());
    const int section = arguments.value("section").toInt(song_.currentSection());
    const auto part = resolvePart(song_, track, section);
    if (!part.error.isEmpty()) return failure(part.error);
    const auto kind = clefKindOf(arguments.value("clef").toString());
    if (!kind)
        return failure(QStringLiteral("Unknown clef \"%1\". One of: %2.").arg(arguments.value("clef").toString(), clefNames()));
    const QString name = QString::fromUtf8(blokkily::notation::Clef(*kind).name());
    if (!song_.setNotationSettings(track, section, {{"clef", name}}))
        return failure(QStringLiteral("The clef could not be set."));
    return jsonResult(QJsonObject{{"track", track}, {"pattern", section}, {"clef", name}});
}

QJsonObject McpTools::setKeySignature(const QJsonObject& arguments) {
    const int section = arguments.value("section").toInt(song_.currentSection());
    if (section < 0 || section >= song_.sections().size())
        return failure(QStringLiteral("There is no pattern %1.").arg(section));
    const auto fifths = wholeNumber(arguments.value("fifths"));
    if (!fifths || *fifths < -7 || *fifths > 7)
        return failure(QStringLiteral("fifths is a whole number from -7 to 7 (0 is C major, 1 one sharp)."));
    // The key is the section's: any track with a part of it sets it for all.
    int any = -1;
    for (int track = 0; track < song_.trackCount() && any < 0; ++track)
        if (song_.partOf(section, track) >= 0) any = track;
    if (any < 0 || !song_.setNotationSettings(any, section, {{"keyFifths", *fifths}}))
        return failure(QStringLiteral("Pattern %1 has no parts to set a key on.").arg(section));
    return jsonResult(QJsonObject{{"pattern", section},
                                  {"fifths", *fifths},
                                  {"label", QString::fromStdString(blokkily::notation::KeySignature(*fifths).label())}});
}

// The song's own meter, which places its bars and plays: every bar from `bar`
// on is numerator/denominator until the next change. Clips and tempo points
// keep their bar numbers.
QJsonObject McpTools::setMeter(const QJsonObject& arguments) {
    const auto bar = wholeNumber(arguments.value("bar"));
    const auto numerator = wholeNumber(arguments.value("numerator"));
    const auto denominator = wholeNumber(arguments.value("denominator"));
    if (!bar || *bar < 0) return failure(QStringLiteral("bar must be 0 or more."));
    if (!numerator || *numerator < 1 || *numerator > 64)
        return failure(QStringLiteral("numerator must be 1 to 64."));
    if (!denominator || (*denominator != 1 && *denominator != 2 && *denominator != 4 &&
                         *denominator != 8 && *denominator != 16 && *denominator != 32))
        return failure(QStringLiteral("denominator must be 1, 2, 4, 8, 16 or 32."));
    if (!song_.setMeter(*bar, *numerator, *denominator))
        return failure(QStringLiteral("The meter could not be set at bar %1.").arg(*bar));
    QJsonArray meters;
    for (const auto& change : song_.song().meter.changes)
        meters.append(QJsonObject{{"bar", change.bar},
                                  {"numerator", change.numerator},
                                  {"denominator", change.denominator}});
    return jsonResult(QJsonObject{{"meters", meters}, {"transport", transportState()}});
}

QJsonObject McpTools::setTimeSignature(const QJsonObject& arguments) {
    const int section = arguments.value("section").toInt(song_.currentSection());
    if (section < 0 || section >= song_.sections().size())
        return failure(QStringLiteral("There is no pattern %1.").arg(section));
    QVariantMap settings;
    if (arguments.value("song").toBool()) {
        // Back to the song's meter where the pattern plays.
        settings = {{"meterSet", false}, {"unmetered", false}};
    } else {
        if (arguments.contains("beats") || arguments.contains("unit")) {
            const auto beats = wholeNumber(arguments.value("beats"));
            const auto unit = wholeNumber(arguments.value("unit"));
            if (!beats || !unit)
                return failure(QStringLiteral("Give beats and unit together, such as beats 3 and unit 4."));
            settings = {{"timeBeats", *beats}, {"timeUnit", *unit}, {"unmetered", false}};
        }
        // Chant on or off; the meter it had is kept for when it comes off.
        if (arguments.contains("unmetered")) settings["unmetered"] = arguments.value("unmetered").toBool();
        if (settings.isEmpty())
            return failure(QStringLiteral("Give beats and unit, unmetered, or song: true for the song's meter."));
    }
    int any = -1;
    for (int track = 0; track < song_.trackCount() && any < 0; ++track)
        if (song_.partOf(section, track) >= 0) any = track;
    if (any < 0 || !song_.setNotationSettings(any, section, settings))
        return failure(QStringLiteral("beats is 1 to 64 and unit one of 1, 2, 4, 8, 16, 32."));
    const QVariantMap now = song_.notationSettings(any, section);
    const auto time = now.value("unmetered").toBool()
                          ? blokkily::notation::TimeSignature::unmetered()
                          : blokkily::notation::TimeSignature(now.value("timeBeats").toInt(), now.value("timeUnit").toInt());
    return jsonResult(QJsonObject{{"pattern", section},
                                  {"time", QString::fromStdString(time.label())},
                                  {"followsSong", !now.value("meterSet").toBool()}});
}

QJsonObject McpTools::exportMusicXml(const QJsonObject& arguments) {
    const int section = arguments.value("section").toInt(song_.currentSection());
    if (section < 0 || section >= song_.sections().size())
        return failure(QStringLiteral("There is no pattern %1.").arg(section));
    const QString path = arguments.value("path").toString();
    const QFileInfo target(path);
    if (path.isEmpty() || !target.isAbsolute())
        return failure(QStringLiteral("export_musicxml needs an absolute path."));
    const QString suffix = target.suffix().toLower();
    if (suffix != "musicxml" && suffix != "xml")
        return failure(QStringLiteral("The path must end in .musicxml or .xml."));
    if (target.exists() && !arguments.value("overwrite").toBool())
        return failure(QStringLiteral("%1 already exists; pass overwrite: true to replace it.").arg(path));
    try {
        blokkily::notation::Score score;
        score.title = song_.sections().at(section).toMap().value("name").toString().toStdString();
        std::vector<int> tracks;
        if (arguments.contains("track")) {
            const int track = arguments.value("track").toInt();
            const auto part = resolvePart(song_, track, section);
            if (!part.error.isEmpty()) return failure(part.error);
            tracks.push_back(track);
        } else {
            for (int track = 0; track < song_.trackCount(); ++track)
                if (song_.partOf(section, track) >= 0) tracks.push_back(track);
        }
        if (tracks.empty()) return failure(QStringLiteral("Pattern %1 has no parts on any track.").arg(section));
        for (const int track : tracks)
            score.parts.push_back({song_.tracks().at(track).toMap().value("name").toString().toStdString(),
                                   {scoreStaff(track, section, scoreDressing(track, section, {}))}});
        const std::string xml = blokkily::notation::write(score);
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) ||
            file.write(xml.c_str(), static_cast<qint64>(xml.size())) != static_cast<qint64>(xml.size()) ||
            !file.commit())
            return failure(QStringLiteral("Could not write %1: %2").arg(path, file.errorString()));

        // The export rule: the file is read back, and what it plays is
        // compared with what each exported part plays.
        QFile written(path);
        if (!written.open(QIODevice::ReadOnly)) return failure(QStringLiteral("Could not read %1 back.").arg(path));
        const auto back = blokkily::notation::parse(written.readAll().toStdString());
        if (back.parts.size() != tracks.size())
            return failure(QStringLiteral("%1 reads back with %2 parts, not %3.")
                               .arg(path).arg(back.parts.size()).arg(tracks.size()));
        QJsonArray parts;
        bool all_match = true;
        int notes = 0;
        for (std::size_t p = 0; p < tracks.size(); ++p) {
            const auto& pattern =
                song_.song().patterns.at(static_cast<std::size_t>(song_.partOf(section, tracks[p]))).pattern;
            std::vector<Played> heard;
            for (const auto& staff : back.parts[p].staves) {
                const auto staff_notes = played(blokkily::notation::to_pattern(staff, pattern.ticks_per_beat()));
                heard.insert(heard.end(), staff_notes.begin(), staff_notes.end());
            }
            std::sort(heard.begin(), heard.end());
            const auto original = played(pattern);
            const bool matches = heard == original;
            all_match = all_match && matches;
            notes += static_cast<int>(original.size());
            parts.append(QJsonObject{{"track", tracks[p]},
                                     {"notes", static_cast<int>(original.size())},
                                     {"readBackNotes", static_cast<int>(heard.size())},
                                     {"matches", matches}});
        }
        return jsonResult(QJsonObject{{"path", path},
                                      {"bytes", static_cast<int>(xml.size())},
                                      {"notes", notes},
                                      {"parts", parts},
                                      // False when the pattern holds what notation cannot say,
                                      // such as timing off the 128th grid.
                                      {"matches", all_match}});
    } catch (const std::exception& e) {
        return failure(QStringLiteral("The export failed: %1").arg(e.what()));
    }
}

QJsonObject McpTools::importMusicXml(const QJsonObject& arguments) {
    const int track = arguments.value("track").toInt(song_.selectedTrack());
    const int section = arguments.value("section").toInt(song_.currentSection());
    if (section < 0 || section >= song_.sections().size())
        return failure(QStringLiteral("There is no pattern %1.").arg(section));
    if (arguments.contains("xml") == arguments.contains("path"))
        return failure(QStringLiteral("import_musicxml needs one of xml or path."));
    std::string xml;
    if (arguments.contains("xml")) {
        xml = arguments.value("xml").toString().toStdString();
    } else {
        QFile file(arguments.value("path").toString());
        if (!file.open(QIODevice::ReadOnly))
            return failure(QStringLiteral("Could not read %1.").arg(arguments.value("path").toString()));
        xml = file.readAll().toStdString();
    }
    try {
        const auto score = blokkily::notation::parse(xml);
        if (score.parts.empty()) return failure(QStringLiteral("The score has no parts."));
        // Everything is worked out before anything changes: a part that has
        // no track to land on, or notes that run past the pattern, refuse
        // the whole import.
        struct Landing {
            int track;
            std::vector<blokkily::Trigger> notes;
            const blokkily::notation::Staff* first;
        };
        std::vector<Landing> landings;
        int skipped = 0;
        int dropped = 0;
        for (std::size_t p = 0; p < score.parts.size(); ++p) {
            const int target = track + static_cast<int>(p);
            if (target < 0 || target >= song_.trackCount() || song_.partOf(section, target) < 0 ||
                score.parts[p].staves.empty()) {
                ++skipped;
                continue;
            }
            const auto& pattern =
                song_.song().patterns.at(static_cast<std::size_t>(song_.partOf(section, target))).pattern;
            Landing landing{target, {}, &score.parts[p].staves.front()};
            for (const auto& staff : score.parts[p].staves) {
                int left_out = 0;
                const auto imported = blokkily::notation::to_pattern(staff, pattern.ticks_per_beat(), &left_out);
                dropped += left_out;
                for (const auto& trigger : imported.events()) {
                    if (trigger.start >= pattern.length()) {
                        const double step = static_cast<double>(std::max<blokkily::Tick>(1, pattern.ticks_per_beat() / 4));
                        return failure(QStringLiteral("The score runs to step %1, past pattern %2's %3 steps; "
                                                      "lengthen it with set_section first.")
                                           .arg(static_cast<double>(imported.length()) / step)
                                           .arg(section)
                                           .arg(static_cast<double>(pattern.length()) / step));
                    }
                    auto note = trigger;
                    note.id = 0;
                    note.duration = std::min(note.duration, pattern.length() - note.start);
                    landing.notes.push_back(note);
                }
            }
            landings.push_back(std::move(landing));
        }
        if (landings.empty())
            return failure(QStringLiteral("No part of the score has a track to land on from track %1.").arg(track));

        // One step of undo for the notes and the clef, key and meter they
        // were written in.
        song_.checkpoint();
        int written = 0;
        for (const auto& landing : landings) {
            song_.selectSection(section);
            song_.selectTrack(landing.track);
            auto& pattern = song_.editPattern();
            if (arguments.value("replace").toBool()) {
                std::vector<blokkily::EventId> ids;
                for (const auto& trigger : pattern.events()) ids.push_back(trigger.id);
                for (const auto id : ids) (void)pattern.remove(id);
            }
            for (const auto& note : landing.notes)
                if (pattern.add(note) != 0) ++written;
            const auto& staff = *landing.first;
            QVariantMap settings{{"clef", QString::fromUtf8(staff.clef.name())}, {"keyFifths", staff.key.fifths()}};
            if (staff.time.metered())
                settings.insert({{"timeBeats", staff.time.beats()}, {"timeUnit", staff.time.beat_unit()}, {"unmetered", false}});
            else
                settings.insert("unmetered", true);
            (void)song_.setNotationSettings(landing.track, section, settings, false);
        }
        song_.notifyStructureChanged();
        if (written > 0)
            controller_.eventBus().publish({blokkily::guide::EventSource::mcp, blokkily::guide::EventKind::note_added,
                                            std::to_string(written) + " notes from MusicXML",
                                            "section=" + std::to_string(section)});
        return jsonResult(QJsonObject{{"written", written},
                                      {"dropped", dropped},
                                      {"skippedParts", skipped},
                                      {"section", section},
                                      {"title", QString::fromStdString(score.title)}});
    } catch (const std::exception& e) {
        return failure(QStringLiteral("The import failed: %1").arg(e.what()));
    }
}

QJsonObject McpTools::guideEvents(const QJsonObject& arguments) {
    if (guide_ == nullptr)
        return failure(QStringLiteral("The guide is not running in this window."));
    const int count = std::clamp(arguments.value("count").toInt(10), 1, 50);
    const QVariantList events = guide_->narratedEvents(count);
    QJsonArray out;
    for (const auto& event : events) out.append(QJsonObject::fromVariantMap(event.toMap()));
    return jsonResult(QJsonObject{{"events", out}});
}

QJsonObject McpTools::guideLesson(const QJsonObject& arguments) {
    if (guide_ == nullptr)
        return failure(QStringLiteral("The guide is not running in this window."));
    const QString action = arguments.value("action").toString();
    if (action == "list") {
        QJsonArray lessons;
        for (const auto& id : guide_->lessonIds())
            lessons.append(
                QJsonObject{{"id", id}, {"title", guide_->lessonTitleFor(id)}});
        return jsonResult(QJsonObject{{"lessons", lessons}});
    }
    if (action == "start") {
        const QString id = arguments.value("id").toString();
        if (id.isEmpty()) return failure(QStringLiteral("start needs an id; list shows them."));
        if (!guide_->lessonIds().contains(id))
            return failure(QStringLiteral("There is no lesson \"%1\"; list shows them.").arg(id));
        // Starting a lesson shows the guide, and the guide stays off until
        // the producer turns it on: an agent may not turn it on for them.
        if (!guide_->guideEnabled())
            return failure(QStringLiteral("The guide is off, so no lesson was started. The producer "
                                          "turns it on with GUIDE."));
        guide_->startLesson(id);
    } else if (action == "next") {
        guide_->lessonNext();
    } else if (action == "back") {
        guide_->lessonBack();
    } else if (action == "stop") {
        guide_->stopLesson();
    } else {
        return failure(QStringLiteral("Unknown action \"%1\"; one of list, start, next, back, stop.")
                           .arg(action));
    }
    return jsonResult(QJsonObject{{"active", guide_->lessonActive()},
                                  {"title", guide_->lessonTitle()},
                                  {"step", guide_->lessonStepIndex()},
                                  {"steps", guide_->lessonStepCount()},
                                  {"stepTitle", guide_->lessonStepTitle()},
                                  {"stepText", guide_->lessonStepText()}});
}

QJsonObject McpTools::guideAsk(const QJsonObject& arguments) {
    if (guide_ == nullptr)
        return failure(QStringLiteral("The guide is not running in this window."));
    const QString question = arguments.value("question").toString().trimmed();
    if (question.isEmpty()) return failure(QStringLiteral("guide_ask needs a question."));
    const QString ruled = guide_->ruleAnswer(question);
    if (!ruled.isEmpty()) return jsonResult(QJsonObject{{"answer", ruled}, {"source", "guide"}});
    if (!guide_->guideEnabled())
        return failure(QStringLiteral("The guide's rules have no answer, and the guide is off, so nothing "
                                      "was asked. Turn it on with GUIDE to ask a model."));
    // No rule covers it: hand it to the guide the way the question box does.
    // It answers in the bubble — from its own LLM when configured, else the
    // prompt bar's shared backend.
    guide_->askQuestion(question);
    return jsonResult(
        QJsonObject{{"source", "deferred"},
                    {"answer", "The guide's rules had no answer, so the question went to "
                               "its bubble, where it asks a model (its own LLM config, else "
                               "the prompt bar's backend)."}});
}
