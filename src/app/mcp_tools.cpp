#include "mcp_tools.hpp"

#include "app_controller.hpp"
#include "blokkily/model/song.hpp"
#include "mcp_protocol.hpp"
#include "pattern_model.hpp"
#include "song_model.hpp"
#include "transport.hpp"

#include <QBuffer>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QQuickWindow>
#include <QRegularExpression>

#include <cmath>
#include <optional>
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
                   Transport& transport, QQuickWindow* window)
    : song_(song), pattern_(pattern), controller_(controller), transport_(transport),
      window_(window) {}

QJsonObject McpTools::call(const QString& name, const QJsonObject& arguments) {
    if (name == "get_session") return getSession();
    if (name == "get_pattern") return getPattern(arguments);
    if (name == "write_notes") return writeNotes(arguments);
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

QJsonObject McpTools::getSession() const {
    QJsonArray tracks;
    for (const auto& row : song_.tracks())
        tracks.append(pick(row.toMap(), {"index", "name", "instrument", "hasInstrument", "gainDb",
                                         "pan", "mute", "solo", "armed", "selected", "inserts"}));
    QJsonArray patterns;
    for (const auto& row : song_.sections())
        patterns.append(pick(row.toMap(), {"index", "name", "events", "current"}));
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
    };
    std::vector<Wanted> wanted;
    const auto notes = arguments.value("notes").toArray();
    for (int index = 0; index < notes.size(); ++index) {
        const auto note = notes.at(index).toObject();
        const auto key = keyOf(note.value("key"));
        const double step = note.value("step").toDouble(-1.0);
        const double length = note.value("length").toDouble(1.0);
        const double velocity = note.value("velocity").toDouble(0.8);
        if (!key) return failure(QStringLiteral("Note %1: key must be 0 to 127 or a name like C4.").arg(index));
        if (step < 0) return failure(QStringLiteral("Note %1: step must be 0 or more.").arg(index));
        if (length <= 0) return failure(QStringLiteral("Note %1: length must be more than 0.").arg(index));
        if (velocity < 0 || velocity > 1) return failure(QStringLiteral("Note %1: velocity must be 0 to 1.").arg(index));
        wanted.push_back({step, length, *key, static_cast<float>(velocity)});
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
        trigger.musical_data = blokkily::Note{static_cast<std::int16_t>(note.key), note.velocity, 0.0F};
        if (pattern.add(std::move(trigger)) != 0) ++written;
    }
    song_.notifyStructureChanged();
    return jsonResult(QJsonObject{{"written", written},
                                  {"refused", static_cast<int>(wanted.size()) - written},
                                  {"track", track},
                                  {"pattern", section},
                                  {"notes", static_cast<int>(pattern.events().size())}});
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
    const int track = arguments.value("track").toInt(song_.selectedTrack());
    const int slot = arguments.value("slot").toInt(-1);
    if (track < 0 || track >= song_.trackCount())
        return failure(QStringLiteral("There is no track %1.").arg(track));
    const auto rows = controller_.insertParameters(QStringLiteral("track"), track, slot);
    if (rows.isEmpty())
        return failure(slot < 0 ? QStringLiteral("Track %1's instrument has no parameters to set (or none is loaded).").arg(track)
                                : QStringLiteral("Track %1 has no effect in slot %2 with parameters.").arg(track).arg(slot));
    QJsonArray parameters;
    for (const auto& row : rows) parameters.append(QJsonObject::fromVariantMap(row.toMap()));
    return jsonResult(QJsonObject{{"track", track}, {"slot", slot}, {"parameters", parameters}});
}

QJsonObject McpTools::setParameter(const QJsonObject& arguments) {
    const int track = arguments.value("track").toInt(song_.selectedTrack());
    const int slot = arguments.value("slot").toInt(-1);
    if (track < 0 || track >= song_.trackCount())
        return failure(QStringLiteral("There is no track %1.").arg(track));
    // The parameter by its id, or by its name as list_parameters shows it.
    const auto wanted = arguments.value("parameter");
    QVariantMap found;
    for (const auto& row : controller_.insertParameters(QStringLiteral("track"), track, slot)) {
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
    if (!controller_.automateInsert(QStringLiteral("track"), track, slot, id))
        return failure(QStringLiteral("%1 could not be given a lane.").arg(found.value("name").toString()));
    // The lane the parameter has: one value for the whole song, unless it is
    // already automated, which is the producer's to edit.
    const auto& lanes = song_.song().tracks.at(static_cast<std::size_t>(track)).automation;
    int lane = -1;
    for (std::size_t index = 0; index < lanes.size(); ++index)
        if (lanes[index].target.kind == blokkily::AutomationTarget::Kind::parameter &&
            lanes[index].target.processor.slot == slot && lanes[index].target.parameter_index == id)
            lane = static_cast<int>(index);
    if (lane < 0) return failure(QStringLiteral("The lane could not be found."));
    if (lanes[static_cast<std::size_t>(lane)].points.size() > 1)
        return failure(QStringLiteral("%1 is automated across the song; edit its lane instead.").arg(found.value("name").toString()));
    if (song_.moveAutomationPoint(track, lane, 0, 0.0, value) < 0)
        return failure(QStringLiteral("%1 could not be set.").arg(found.value("name").toString()));
    return jsonResult(QJsonObject{{"track", track}, {"slot", slot}, {"parameter", id},
                                  {"name", found.value("name").toString()}, {"value", value}});
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
