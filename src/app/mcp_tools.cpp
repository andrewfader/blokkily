#include "mcp_tools.hpp"

#include "../guide/guide_controller.hpp"
#include "app_controller.hpp"
#include "blokkily/guide/event_bus.hpp"
#include "blokkily/model/song.hpp"
#include "blokkily/notation/clef.hpp"
#include "blokkily/notation/duration.hpp"
#include "blokkily/notation/layout.hpp"
#include "blokkily/notation/musicxml.hpp"
#include "blokkily/notation/pattern_bridge.hpp"
#include "blokkily/notation/pitch.hpp"
#include "blokkily/notation/signature.hpp"
#include "blokkily/notation/spelling.hpp"
#include "mcp_protocol.hpp"
#include "pattern_model.hpp"
#include "song_model.hpp"
#include "transport.hpp"

#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QQuickWindow>
#include <QRegularExpression>

#include <cmath>
#include <algorithm>
#include <optional>
#include <stdexcept>
#include <variant>
#include <vector>

namespace {

using blokkily::mcp::jsonResult;
using blokkily::mcp::textResult;

QJsonObject failure(const QString& text) { return textResult(text, true); }

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
    // The guide's narration feed attributes what follows: the window's own
    // emitting commands stamp source=mcp through setNextEventSource, and the
    // stamp is cleared afterwards so a later click is never misattributed.
    // Both models are stamped: tools reach the pattern model as well as the
    // controller (e.g. through the generic call/invoke tool).
    controller_.setNextEventSource(blokkily::guide::EventSource::mcp);
    pattern_.setNextEventSource(blokkily::guide::EventSource::mcp);
    QJsonObject result = dispatch(name, arguments);
    controller_.setNextEventSource(blokkily::guide::EventSource::user);
    pattern_.setNextEventSource(blokkily::guide::EventSource::user);
    return result;
}

QJsonObject McpTools::dispatch(const QString& name, const QJsonObject& arguments) {
    if (name == "get_session") return getSession();
    if (name == "get_pattern") return getPattern(arguments);
    if (name == "write_notes") return writeNotes(arguments);
    if (name == "set_section") return setSection(arguments);
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
        if (const auto* note = std::get_if<blokkily::Note>(&trigger.musical_data)) {
            entry.insert("key", note->key);
            entry.insert("velocity", note->velocity);
            if (note->cents != 0.0) entry.insert("cents", note->cents);
        } else if (const auto* chord = std::get_if<blokkily::Chord>(&trigger.musical_data)) {
            QJsonArray keys;
            for (const auto interval : chord->intervals) keys.append(chord->root + interval);
            entry.insert("keys", keys);
        }
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
        if (!key) return failure(QStringLiteral("Note %1: key must be 0 to 127 or a name like C4.").arg(index));
        if (step < 0) return failure(QStringLiteral("Note %1: step must be 0 or more.").arg(index));
        if (length <= 0) return failure(QStringLiteral("Note %1: length must be more than 0.").arg(index));
        if (velocity < 0 || velocity > 1) return failure(QStringLiteral("Note %1: velocity must be 0 to 1.").arg(index));
        if (!(cents >= -100.0 && cents <= 100.0))
            return failure(QStringLiteral("Note %1: cents must be -100 to 100.").arg(index));
        wanted.push_back({step, length, *key, static_cast<float>(velocity), cents});
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
    const auto before = song_.song().clips.size();
    song_.selectSection(section);
    if (one) song_.placeClip(track, bar);
    else song_.placeSection(bar);
    const auto placed = static_cast<int>(song_.song().clips.size() - before);
    if (placed <= 0)
        return failure(one ? QStringLiteral("Track %1 already has a clip at bar %2.").arg(track).arg(bar)
                           : QStringLiteral("Every track already has a clip at bar %1.").arg(bar));
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
    if (int(track) + int(bus) + int(file) > 1)
        return failure(QStringLiteral("Analyze one of track, return or file, or none for the master."));
    const auto analysis =
        file ? controller_.analyzeAudioFile(arguments.value("file").toString())
        : track ? controller_.analyzeRender(QStringLiteral("track"), arguments.value("track").toInt(-1))
        : bus ? controller_.analyzeRender(QStringLiteral("return"), arguments.value("return").toInt(-1))
              : controller_.analyzeRender(QStringLiteral("master"), 0);
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
    // Throws std::invalid_argument on a bad override; the callers turn that
    // into a refused tool call.
    ScoreDressing dressing;
    const QVariantMap stored = song_.notationSettings(track, section);
    const QString clef_name =
        arguments.value("clef").toString(stored.value("clef").toString());
    const auto kind = clefKindOf(clef_name);
    if (!kind)
        throw std::invalid_argument("unknown clef \"" + clef_name.toStdString() +
                                    "\"; one of: " + clefNames().toStdString());
    dressing.clef = blokkily::notation::Clef(*kind);
    const int fifths = arguments.contains("key") ? arguments.value("key").toInt()
                                                 : stored.value("keyFifths").toInt();
    dressing.key = blokkily::notation::KeySignature(fifths);  // range-checked
    const QString time_arg = arguments.value("time").toString();
    if (!time_arg.isEmpty()) {
        if (time_arg.compare("unmetered", Qt::CaseInsensitive) == 0) {
            dressing.time = blokkily::notation::TimeSignature::unmetered();
        } else {
            static const QRegularExpression meter(QStringLiteral("^(\\d+)/(\\d+)$"));
            const auto match = meter.match(time_arg.trimmed());
            if (!match.hasMatch())
                throw std::invalid_argument("time is \"beats/unit\" or \"unmetered\"");
            dressing.time = blokkily::notation::TimeSignature(match.captured(1).toInt(),
                                                             match.captured(2).toInt());
        }
    } else if (stored.value("unmetered").toBool()) {
        dressing.time = blokkily::notation::TimeSignature::unmetered();
    } else {
        dressing.time = blokkily::notation::TimeSignature(stored.value("timeBeats").toInt(),
                                                         stored.value("timeUnit").toInt());
    }
    return dressing;
}

blokkily::notation::Part McpTools::scorePart(int track, int section) {
    const int index = song_.partOf(section, track);
    const auto& pattern = song_.song().patterns.at(static_cast<std::size_t>(index)).pattern;
    const auto dressing = scoreDressing(track, section, {});
    auto staff = blokkily::notation::to_staff(
        pattern, dressing.key, dressing.clef,
        std::max<blokkily::Tick>(1, pattern.ticks_per_beat()));
    staff.time = dressing.time;
    return {song_.tracks().at(track).toMap().value("name").toString().toStdString(),
            {std::move(staff)}};
}

QJsonObject McpTools::getScore(const QJsonObject& arguments) {
    const int track = arguments.value("track").toInt(song_.selectedTrack());
    const int section = arguments.value("section").toInt(song_.currentSection());
    const auto part = resolvePart(song_, track, section);
    if (!part.error.isEmpty()) return failure(part.error);
    try {
        const auto dressing = scoreDressing(track, section, arguments);
        const int index = song_.partOf(section, track);
        const auto& pattern =
            song_.song().patterns.at(static_cast<std::size_t>(index)).pattern;
        const blokkily::Tick ticks_per_beat =
            std::max<blokkily::Tick>(1, pattern.ticks_per_beat());
        const double step =
            static_cast<double>(std::max<blokkily::Tick>(1, ticks_per_beat / 4));
        QJsonArray notes;
        for (const auto& trigger : pattern.events()) {
            struct Voice {
                int key;
                float velocity;
                int number;
            };
            std::vector<Voice> voices;
            if (const auto* note = std::get_if<blokkily::Note>(&trigger.musical_data)) {
                voices.push_back({note->key, note->velocity, 1});
            } else if (const auto* chord =
                           std::get_if<blokkily::Chord>(&trigger.musical_data)) {
                for (std::size_t voice = 0; voice < chord->intervals.size(); ++voice)
                    voices.push_back(
                        {static_cast<int>(chord->root + chord->intervals[voice]),
                         blokkily::voice_velocity(*chord, voice),
                         static_cast<int>(voice + 1)});
            }
            for (const auto& voice : voices) {
                const auto spelled = blokkily::notation::spell(voice.key, dressing.key);
                const auto chain = blokkily::notation::from_ticks(trigger.duration,
                                                                  ticks_per_beat);
                const auto& first = chain.front();
                notes.append(QJsonObject{
                    {"step", static_cast<double>(trigger.start) / step},
                    {"length", static_cast<double>(trigger.duration) / step},
                    {"midi", voice.key},
                    {"spelled",
                     QString::fromUtf8(blokkily::notation::step_name(spelled.step)) +
                         QString::fromUtf8(
                             blokkily::notation::accidental_name(spelled.accidental)) +
                         QString::number(spelled.octave)},
                    {"value",
                     QString::fromUtf8(blokkily::notation::value_name(first.value))},
                    {"dots", first.dots},
                    {"tied", chain.size() > 1},
                    {"staffPosition",
                     blokkily::notation::staff_position(spelled, dressing.clef)},
                    {"accidentalShown",
                     blokkily::notation::needs_accidental(spelled, dressing.key)},
                    {"voice", voice.number},
                    {"velocity", voice.velocity}});
            }
        }
        // The same part as a MusicXML document, through the pattern bridge.
        auto staff =
            blokkily::notation::to_staff(pattern, dressing.key, dressing.clef, ticks_per_beat);
        staff.time = dressing.time;
        blokkily::notation::Score score;
        score.title =
            song_.sections().at(section).toMap().value("name").toString().toStdString();
        score.parts.push_back(
            {song_.tracks().at(track).toMap().value("name").toString().toStdString(),
             {std::move(staff)}});
        const std::string xml = blokkily::notation::write(score);
        return jsonResult(QJsonObject{
            {"track", track},
            {"pattern", section},
            {"name", song_.sections().at(section).toMap().value("name").toString()},
            {"clef", QString::fromUtf8(dressing.clef.name())},
            {"key",
             QJsonObject{{"fifths", dressing.key.fifths()},
                         {"label", QString::fromStdString(dressing.key.label())}}},
            {"time", QString::fromStdString(dressing.time.label())},
            {"steps", static_cast<double>(pattern.length()) / step},
            {"notes", notes},
            {"musicxml", QString::fromUtf8(xml.c_str())},
        });
    } catch (const std::exception& e) {
        return failure(QStringLiteral("The score could not be built: %1").arg(e.what()));
    }
}

QJsonObject McpTools::setClef(const QJsonObject& arguments) {
    const int track = arguments.value("track").toInt(song_.selectedTrack());
    const int section = arguments.value("section").toInt(song_.currentSection());
    const auto part = resolvePart(song_, track, section);
    if (!part.error.isEmpty()) return failure(part.error);
    const auto kind = clefKindOf(arguments.value("clef").toString());
    if (!kind)
        return failure(QStringLiteral("Unknown clef \"%1\". One of: %2.")
                           .arg(arguments.value("clef").toString(), clefNames()));
    QVariantMap settings = song_.notationSettings(track, section);
    settings["clef"] = QString::fromUtf8(blokkily::notation::Clef(*kind).name());
    song_.setNotationSettings(track, section, settings);
    return jsonResult(QJsonObject{{"track", track},
                                  {"pattern", section},
                                  {"clef", settings.value("clef").toString()}});
}

QJsonObject McpTools::setKeySignature(const QJsonObject& arguments) {
    const int section = arguments.value("section").toInt(song_.currentSection());
    if (section < 0 || section >= song_.sections().size())
        return failure(QStringLiteral("There is no pattern %1.").arg(section));
    const int fifths = arguments.value("fifths").toInt(999);
    if (fifths < -7 || fifths > 7)
        return failure(QStringLiteral("fifths is -7 to 7 (0 is C major, 1 one sharp)."));
    // Every track's part of the section reads the section's key.
    for (int track = 0; track < song_.trackCount(); ++track) {
        if (song_.partOf(section, track) < 0) continue;
        QVariantMap settings = song_.notationSettings(track, section);
        settings["keyFifths"] = fifths;
        song_.setNotationSettings(track, section, settings);
    }
    const blokkily::notation::KeySignature key(fifths);
    return jsonResult(QJsonObject{{"pattern", section},
                                  {"fifths", fifths},
                                  {"label", QString::fromStdString(key.label())}});
}

QJsonObject McpTools::setTimeSignature(const QJsonObject& arguments) {
    const int section = arguments.value("section").toInt(song_.currentSection());
    if (section < 0 || section >= song_.sections().size())
        return failure(QStringLiteral("There is no pattern %1.").arg(section));
    const bool unmetered = arguments.value("unmetered").toBool();
    const int beats = arguments.value("beats").toInt(4);
    const int unit = arguments.value("unit").toInt(4);
    if (!unmetered &&
        (beats < 1 || (unit != 1 && unit != 2 && unit != 4 && unit != 8 && unit != 16 &&
                       unit != 32)))
        return failure(QStringLiteral("beats is 1 or more and unit one of 1, 2, 4, 8, 16, 32."));
    for (int track = 0; track < song_.trackCount(); ++track) {
        if (song_.partOf(section, track) < 0) continue;
        QVariantMap settings = song_.notationSettings(track, section);
        settings["unmetered"] = unmetered;
        settings["timeBeats"] = beats;
        settings["timeUnit"] = unit;
        song_.setNotationSettings(track, section, settings);
    }
    const auto time =
        unmetered ? blokkily::notation::TimeSignature::unmetered()
                  : blokkily::notation::TimeSignature(beats, unit);
    return jsonResult(QJsonObject{{"pattern", section},
                                  {"time", QString::fromStdString(time.label())}});
}

QJsonObject McpTools::exportMusicXml(const QJsonObject& arguments) {
    const int section = arguments.value("section").toInt(song_.currentSection());
    if (section < 0 || section >= song_.sections().size())
        return failure(QStringLiteral("There is no pattern %1.").arg(section));
    const QString path = arguments.value("path").toString();
    if (path.isEmpty()) return failure(QStringLiteral("export_musicxml needs a path."));
    try {
        blokkily::notation::Score score;
        score.title =
            song_.sections().at(section).toMap().value("name").toString().toStdString();
        int note_count = 0;
        if (arguments.contains("track")) {
            const int track = arguments.value("track").toInt();
            const auto part = resolvePart(song_, track, section);
            if (!part.error.isEmpty()) return failure(part.error);
            score.parts.push_back(scorePart(track, section));
            note_count = static_cast<int>(
                song_.song()
                    .patterns.at(static_cast<std::size_t>(song_.partOf(section, track)))
                    .pattern.events()
                    .size());
        } else {
            for (int track = 0; track < song_.trackCount(); ++track) {
                if (song_.partOf(section, track) < 0) continue;
                score.parts.push_back(scorePart(track, section));
                note_count += static_cast<int>(
                    song_.song()
                        .patterns.at(static_cast<std::size_t>(song_.partOf(section, track)))
                        .pattern.events()
                        .size());
            }
        }
        if (score.parts.empty())
            return failure(QStringLiteral("Pattern %1 has no parts on any track.").arg(section));
        const std::string xml = blokkily::notation::write(score);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
            return failure(QStringLiteral("Could not write %1.").arg(path));
        file.write(xml.c_str(), static_cast<qint64>(xml.size()));
        file.close();
        // Read back what was written, per the export rule.
        const auto back = blokkily::notation::parse(xml);
        int back_notes = 0;
        for (const auto& part : back.parts)
            for (const auto& staff : part.staves)
                for (const auto& measure : staff.measures)
                    for (const auto& object : measure.objects)
                        if (blokkily::notation::is_note(object)) ++back_notes;
        return jsonResult(QJsonObject{{"path", path},
                                      {"bytes", static_cast<int>(xml.size())},
                                      {"parts", static_cast<int>(score.parts.size())},
                                      {"notes", note_count},
                                      {"readBackNotes", back_notes}});
    } catch (const std::exception& e) {
        return failure(QStringLiteral("The export failed: %1").arg(e.what()));
    }
}

QJsonObject McpTools::importMusicXml(const QJsonObject& arguments) {
    const int track = arguments.value("track").toInt(song_.selectedTrack());
    const int section = arguments.value("section").toInt(song_.currentSection());
    if (section < 0 || section >= song_.sections().size())
        return failure(QStringLiteral("There is no pattern %1.").arg(section));
    std::string xml;
    if (arguments.contains("xml")) {
        xml = arguments.value("xml").toString().toStdString();
    } else if (arguments.contains("path")) {
        QFile file(arguments.value("path").toString());
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return failure(QStringLiteral("Could not read %1.")
                               .arg(arguments.value("path").toString()));
        xml = file.readAll().toStdString();
    } else {
        return failure(QStringLiteral("import_musicxml needs xml or path."));
    }
    try {
        const auto score = blokkily::notation::parse(xml);
        if (score.parts.empty())
            return failure(QStringLiteral("The score has no parts."));
        const bool replace = arguments.value("replace").toBool();
        // One checkpoint for the whole import: a single step of undo, however
        // many parts land.
        song_.selectSection(section);
        song_.checkpoint();
        int written = 0, refused = 0, skipped = 0;
        for (std::size_t part_index = 0; part_index < score.parts.size(); ++part_index) {
            const int target = track + static_cast<int>(part_index);
            if (target < 0 || target >= song_.trackCount() ||
                song_.partOf(section, target) < 0) {
                ++skipped;
                continue;
            }
            song_.selectTrack(target);
            auto& pattern = song_.editPattern();
            if (replace) {
                std::vector<blokkily::EventId> ids;
                for (const auto& trigger : pattern.events()) ids.push_back(trigger.id);
                for (const auto id : ids) (void)pattern.remove(id);
            }
            const blokkily::Tick ticks_per_beat =
                std::max<blokkily::Tick>(1, pattern.ticks_per_beat());
            for (const auto& staff : score.parts[part_index].staves) {
                const auto imported = blokkily::notation::to_pattern(staff, ticks_per_beat);
                for (const auto& trigger : imported.events()) {
                    if (trigger.start < 0 || trigger.start >= pattern.length() ||
                        trigger.duration <= 0) {
                        ++refused;
                        continue;
                    }
                    if (pattern.add(trigger) != 0)
                        ++written;
                    else
                        ++refused;
                }
            }
        }
        song_.notifyStructureChanged();
        if (written > 0)
            controller_.eventBus().publish(
                {blokkily::guide::EventSource::mcp, blokkily::guide::EventKind::note_added,
                 std::to_string(written) + " notes from MusicXML",
                 "section=" + std::to_string(section)});
        return jsonResult(QJsonObject{{"written", written},
                                      {"refused", refused},
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
