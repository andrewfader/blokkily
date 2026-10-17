#include "mcp_protocol.hpp"

#include <QJsonDocument>

#include <cmath>
#include <QJsonParseError>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QLockFile>

#ifndef BLOKKILY_VERSION
#define BLOKKILY_VERSION "0"
#endif

namespace blokkily::mcp {
namespace {

QJsonObject schema(QJsonObject properties, QJsonArray required = {}) {
    QJsonObject object{{"type", "object"}, {"properties", properties}};
    if (!required.isEmpty()) object.insert("required", required);
    return object;
}

QJsonObject integer(const QString& description) {
    return {{"type", "integer"}, {"description", description}};
}

QJsonObject tool(const QString& name, const QString& description, QJsonObject input) {
    return {{"name", name}, {"description", description}, {"inputSchema", input}};
}

const QJsonArray objects{"songModel", "patternModel", "appController", "transport"};

}  // namespace

QString socketPath() {
    const QByteArray chosen = qgetenv("BLOKKILY_MCP_SOCKET");
    if (!chosen.isEmpty()) return QString::fromLocal8Bit(chosen);
#ifdef Q_OS_WIN
    return QStringLiteral("blokkily-mcp");
#else
    QString directory = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (directory.isEmpty()) directory = QDir::tempPath();
    return QDir(directory).filePath(QStringLiteral("blokkily-mcp.sock"));
#endif
}

namespace {

// Beside a Unix socket; for a Windows pipe in the temporary directory, by the
// pipe's own name. A pipe is named either bare ("blokkily-mcp") or in full
// ("\\.\pipe\blokkily-mcp", what QLocalServer::fullServerName() gives), and
// both spellings must find the same lock: no file can live in the pipe
// namespace itself.
QString lockPath(const QString& socket) {
    QString name = socket;
    for (const auto prefix : {QStringLiteral("\\\\.\\pipe\\"), QStringLiteral("//./pipe/")})
        if (name.startsWith(prefix, Qt::CaseInsensitive)) name = name.mid(prefix.size());
    if (name == socket && QFileInfo(socket).isAbsolute()) return socket + QStringLiteral(".lock");
    return QDir(QDir::tempPath()).filePath(name + QStringLiteral(".lock"));
}

}  // namespace

SocketLock::SocketLock(const QString& socket)
    : lock_(std::make_unique<QLockFile>(lockPath(socket))) {
    // Only a dead owner makes a lock stale, never its age: a window may run
    // for days.
    lock_->setStaleLockTime(0);
    held_ = lock_->tryLock(0);
}

SocketLock::~SocketLock() = default;

bool SocketLock::taken(const QString& socket) {
    QLockFile probe(lockPath(socket));
    probe.setStaleLockTime(0);
    if (probe.tryLock(0)) {
        probe.unlock();
        return false;
    }
    // Held by a live process. Windows reports another process's open lock as
    // a permission error, not LockFailedError, so any refusal counts.
    return true;
}

QJsonArray toolDefinitions() {
    const QJsonObject track = integer("Track index, from 0. Defaults to the selected track.");
    const QJsonObject section =
        integer("Pattern (section) index, from 0. Defaults to the open pattern.");
    const QJsonObject slot = integer("-1 for the instrument (the default), 0.. for an effect.");
    const QJsonObject on_return =
        integer("A return bus's effect instead of a track's: the return's index, from 0.");
    const QJsonObject on_master{{"type", "boolean"},
                                {"description", "The master bus's effect instead of a track's."}};
    QJsonObject note{
        {"type", "object"},
        {"properties",
         QJsonObject{
             {"step", QJsonObject{{"type", "number"},
                                  {"description",
                                   "Where the note starts, in sixteenth-note steps from the "
                                   "pattern's start (0 is the first step; 0.5 is half a step "
                                   "later)."}}},
             {"length", QJsonObject{{"type", "number"},
                                    {"description", "How long it lasts, in steps. Default 1."}}},
             {"key", QJsonObject{{"description",
                                  "MIDI key (60 is middle C) or a note name such as \"C4\", "
                                  "\"F#3\" or \"Bb2\"."}}},
             {"velocity", QJsonObject{{"type", "number"},
                                      {"description", "0 to 1. Default 0.8."}}},
             {"cents", QJsonObject{{"type", "number"},
                                   {"description",
                                    "Retunes the note from its key, -100 to 100 cents, for a "
                                    "pitch between keys (50 is a quarter tone up). Default 0."}}},
             {"offset", QJsonObject{{"type", "integer"},
                                    {"description",
                                     "Pushes the note off its step by this many ticks (120 to a "
                                     "step), -59 to 59, for feel: a late snare, a loose double. "
                                     "Default 0."}}},
             {"lyric", QJsonObject{{"type", "string"},
                                   {"description",
                                    "The syllable sung on the note, in any script: \"Al-\" goes "
                                    "on into the next syllable, \"_\" holds the last one."}}},
         }},
        {"required", QJsonArray{"step", "key"}},
    };
    return {
        tool("get_session",
             "The whole session at a glance: tracks with their instruments, mixer settings, "
             "effects and sends, the returns and the master's effects, the patterns with their "
             "length and swing, where clips sit in the arrangement, the song's length in bars "
             "(lengthBars: where the last clip ends) and its meter changes, tempo, the "
             "transport, what is selected, and whether there are unsaved changes.",
             schema({})),
        tool("get_pattern",
             "The notes of one track's part of one pattern: start step, length in steps, key "
             "and velocity of each, its cents when it is retuned from its key, its offset "
             "in ticks when it is pushed off its step, and the "
             "parameter locks a note holds.",
             schema({{"track", track}, {"section", section}})),
        tool("write_notes",
             "Writes notes into one track's part of one pattern, as one step of undo. Opens "
             "that pattern and track in the window. With replace, the part is emptied first.",
             schema({{"track", track},
                     {"section", section},
                     {"replace", QJsonObject{{"type", "boolean"},
                                             {"description",
                                              "Empty the part before writing. Default false."}}},
                     {"notes", QJsonObject{{"type", "array"}, {"items", note}}}},
                    {"notes"})),
        tool("set_section",
             "Sets a pattern's (section's) length in steps, its swing, or its name, for every "
             "track's part of it. Safe while the song plays: the pattern is named, not taken "
             "from what is open.",
             schema({{"section", integer("Pattern (section) index, from 0.")},
                     {"steps", integer("Length in sixteenth-note steps (16 is a bar of 4/4).")},
                     {"swing", integer("0 to 100.")},
                     {"name", QJsonObject{{"type", "string"}}}},
                    {"section"})),
        tool("set_lyrics",
             "Lays a line of lyrics on one track's part of one pattern, one syllable per note "
             "in time order (notes that start together share one), as one step of undo: "
             "\"A-ma-zing grace\" is A-, ma-, zing, grace; \"_\" holds the last syllable over a "
             "note. Any script, Hebrew included. Notes past the last syllable are left without one.",
             schema({{"track", track},
                     {"section", section},
                     {"text", QJsonObject{{"type", "string"}}}},
                    {"text"})),
        tool("place_section",
             "Places a pattern (section) in the arrangement at a bar: a clip of each track's "
             "part, or of one track's with track. Bars already holding a clip are left as "
             "they are. Placing it where it already is changes nothing and answers "
             "alreadyPlaced; a refusal names the clips in the way.",
             schema({{"section", integer("Pattern (section) index, from 0.")},
                     {"bar", integer("The bar, from 0.")},
                     {"track", integer("Only this track's part.")}},
                    {"section", "bar"})),
        tool("transport",
             "Plays, stops, rewinds, arms recording, or moves the playhead to a bar.",
             schema({{"action", QJsonObject{{"type", "string"},
                                            {"enum", QJsonArray{"play", "stop", "rewind",
                                                                "record", "seek"}}}},
                     {"bar", integer("For seek: the bar, from 0.")}},
                    {"action"})),
        tool("list_plugins",
             "The instruments and effects the plugin browser lists, each with the index "
             "set_instrument takes.",
             schema({})),
        tool("set_instrument",
             "Gives a track the instrument the plugin browser lists at `plugin`.",
             schema({{"track", track},
                     {"plugin", integer("Index from list_plugins.")}},
                    {"plugin"})),
        tool("list_parameters",
             "The parameters of a track's instrument (slot -1) or of one of its effects (slot "
             "0, 1, ...), or of an effect on a return or the master: id, name, minimum, "
             "maximum, default and current value of each, for set_parameter.",
             schema({{"track", track}, {"slot", slot}, {"return", on_return}, {"master", on_master}})),
        tool("set_parameter",
             "Sets one parameter of a track's instrument or effect, or of an effect on a return "
             "or the master, for the whole song, as a lane holding that value (heard from the "
             "next play). A return's or the master's lane is kept on the selected track. A "
             "synth's preset is usually a parameter called Preset.",
             schema({{"track", track},
                     {"slot", slot},
                     {"return", on_return},
                     {"master", on_master},
                     {"parameter", QJsonObject{{"description", "Its id, or its name."}}},
                     {"value", QJsonObject{{"type", "number"}}}},
                    {"parameter", "value"})),
        tool("set_soundfont_preset",
             "Chooses which preset of its SoundFont a SoundFont track plays: a General MIDI "
             "bank holds its instruments in bank 0 by program (0 piano, 33 fingered bass, ...) "
             "and its drum kits in bank 128.",
             schema({{"track", track},
                     {"bank", integer("0 to 128.")},
                     {"program", integer("0 to 127.")}},
                    {"bank", "program"})),
        tool("screenshot", "A PNG of the Blokkily window as it is drawn now.", schema({})),
        tool("analyze_render",
             "Renders the song through the same engine as an export and measures what it hears: "
             "integrated loudness (LUFS, BS.1770 gated), loudest 400 ms and 3 s, loudness range, "
             "sample and true peak, RMS, crest factor, clipped samples, left/right correlation, "
             "spectral centroid, and each band's level and share of the energy (sub 20-60 Hz, "
             "bass 60-250, lowMid 250-500, mid 500-2k, highMid 2-4k, presence 4-6k, air 6-20k). "
             "With no arguments it measures the master mix; with track or return, that bus alone "
             "after its inserts, fader and pan; with file, a WAVE file already written, such as "
             "a reference mix; with all, the master, every track and every return from one "
             "render (a list under buses, each with its source and name). fromBar and toBar "
             "measure only those bars, starting from silence. Silent levels read null. "
             "Changes nothing in the song.",
             schema({{"track", integer("Measure this track's bus alone.")},
                     {"return", integer("Measure this return bus alone.")},
                     {"all", QJsonObject{{"type", "boolean"},
                                         {"description", "Measure the master and every bus from one render."}}},
                     {"fromBar", integer("The first bar to render, from 0. Default 0.")},
                     {"toBar", integer("The bar to stop before. Default: the song's end.")},
                     {"file", QJsonObject{{"type", "string"},
                                          {"description", "Measure this WAVE file instead of rendering."}}}})),
        tool("describe",
             "Every method and property the window's controls use, for `call` and "
             "`get_property`. Covers what the purpose-built tools do not: the mixer, effects, "
             "automation, clips, tempo, tuning, saving and exporting, undo and redo.",
             schema({{"object", QJsonObject{{"type", "string"}, {"enum", objects}}}})),
        tool("get_score",
             "One track's part of one pattern as staff notation, as the SCORE view draws "
             "it: every note and rest with its bar, step, written value, dots, tuplet and "
             "voice, and for a note its spelling (C4, F#3), whether it is a chord tone or "
             "tied, its staff position and whether its accidental prints by the bar's "
             "rule - plus the part as a MusicXML document. Clef, key and meter are the "
             "song's (set_clef, set_key_signature, set_time_signature) unless given here.",
             schema({{"track", track},
                     {"section", section},
                     {"clef",
                      QJsonObject{{"type", "string"},
                                  {"description",
                                   "Treble, French violin, Soprano, Mezzo-soprano, Alto, "
                                   "Tenor, Baritone (C), Baritone (F), Bass, Subbass, "
                                   "Percussion."}}},
                     {"key",
                      QJsonObject{{"type", "integer"},
                                  {"description",
                                   "Key signature in fifths, -7 to 7. 0 is C major."}}},
                     {"time",
                      QJsonObject{{"type", "string"},
                                  {"description",
                                   "\"4/4\", \"3/4\", \"6/8\", ... or \"unmetered\"."}}}})),
        tool("set_clef",
             "The clef one track's part of one pattern is written in. Saved with the song "
             "and undone with it.",
             schema({{"track", track},
                     {"section", section},
                     {"clef",
                      QJsonObject{{"type", "string"},
                                  {"description",
                                   "Treble, French violin, Soprano, Mezzo-soprano, Alto, "
                                   "Tenor, Baritone (C), Baritone (F), Bass, Subbass, "
                                   "Percussion."}}}},
                    {"clef"})),
        tool("set_key_signature",
             "The key signature a pattern is spelled in, in fifths (-7 to 7; 0 is C major, "
             "1 one sharp), for every track's part of it. Saved and undone with the song.",
             schema({{"section", section},
                     {"fifths", integer("The key signature in fifths, -7 to 7.")}},
                    {"fifths"})),
        tool("set_time_signature",
             "The meter a pattern is written in: beats and unit, unmetered (chant: no "
             "time signature and no barlines; false turns chant off and keeps the meter), "
             "or song: true to follow the song's meter where the pattern plays (the "
             "default). Saved and undone with the song.",
             schema({{"section", section},
                     {"beats", integer("Beats in the bar, 1 to 64; give unit with it.")},
                     {"unit", integer("Beat unit: 1, 2, 4, 8, 16 or 32.")},
                     {"unmetered",
                      QJsonObject{{"type", "boolean"},
                                  {"description", "Chant on or off."}}},
                     {"song",
                      QJsonObject{{"type", "boolean"},
                                  {"description", "Follow the song's meter again."}}}},
                    {})),
        tool("set_meter",
             "The song's time signature from a bar on, until the next change: it sets how "
             "long that bar and the ones after it are, where clips are placed and how the "
             "song plays (7/8 is 14 steps a bar). Clips and tempo points keep their bar "
             "numbers. One step of undo. set_time_signature only changes how a pattern is "
             "written.",
             schema({{"bar", integer("The bar it takes effect at, from 0.")},
                     {"numerator", integer("Beats in the bar, 1 to 64.")},
                     {"denominator", integer("Beat unit: 1, 2, 4, 8, 16 or 32.")}},
                    {"bar", "numerator", "denominator"})),
        tool("export_musicxml",
             "Writes a pattern as a MusicXML 4.0 partwise file: one part per track that "
             "has a part of it (or one track's with track). The file is read back and "
             "what it plays is compared with each part: matches is false when a part "
             "holds what notation cannot say, such as timing off the 128th-note grid.",
             schema({{"section", section},
                     {"track", track},
                     {"path",
                      QJsonObject{{"type", "string"},
                                  {"description", "An absolute path ending in .musicxml or .xml."}}},
                     {"overwrite",
                      QJsonObject{{"type", "boolean"},
                                  {"description", "Replace a file already there. Default false."}}}},
                    {"path"})),
        tool("import_musicxml",
             "Reads a MusicXML partwise score into one pattern as one step of undo: part "
             "i lands on track i from `track`, every staff of it, at its sounding pitch, "
             "and the part takes the score's clef, key and meter. With replace, each part "
             "is emptied first. Nothing changes when no part has a track or the score runs "
             "past the pattern (lengthen it with set_section); notes outside MIDI 0-127 "
             "are left out and counted as dropped.",
             schema({{"track", track},
                     {"section", section},
                     {"replace",
                      QJsonObject{{"type", "boolean"},
                                  {"description", "Empty each part before writing. Default false."}}},
                     {"xml",
                      QJsonObject{{"type", "string"},
                                  {"description", "The MusicXML document, as text (or path)."}}},
                     {"path",
                      QJsonObject{{"type", "string"},
                                  {"description", "Read the document from this file (or xml)."}}}},
                    {})),
        tool("guide_events",
             "What the floating guide has been narrating: the last bus events — user "
             "edits, agent tool calls, guide actions — each with who did it and the "
             "guide's why-explanation.",
             schema({{"count",
                      QJsonObject{{"type", "integer"},
                                  {"description", "How many recent events. Default 10, at most 50."}}}})),
        tool("guide_lesson",
             "The guide's interactive lessons: list them, start one, step through it, "
             "or stop. Each step says what to do and where; the guide spotlights the "
             "control in the window.",
             schema({{"action",
                      QJsonObject{{"type", "string"},
                                  {"enum", QJsonArray{"list", "start", "next", "back", "stop"}}}},
                     {"id", QJsonObject{{"type", "string"},
                                        {"description", "For start: the lesson id from list."}}}},
                    {"action"})),
        tool("guide_ask",
             "Ask the guide a music-theory or composition question. Its built-in rules "
             "answer at once; what they do not cover goes to the guide's bubble, where "
             "it asks a model (its own LLM config, else the prompt bar's backend).",
             schema({{"question", QJsonObject{{"type", "string"}}}},
                    {"question"})),
        tool("call",
             "Calls one of the window's methods (listed by describe) with positional "
             "arguments, through the same path a click takes, and returns what it returns.",
             schema({{"object", QJsonObject{{"type", "string"}, {"enum", objects}}},
                     {"method", QJsonObject{{"type", "string"}}},
                     {"args", QJsonObject{{"type", "array"}}}},
                    {"object", "method"})),
        tool("get_property", "Reads one property (listed by describe).",
             schema({{"object", QJsonObject{{"type", "string"}, {"enum", objects}}},
                     {"property", QJsonObject{{"type", "string"}}}},
                    {"object", "property"})),
    };
}

QString instructions() {
    return QStringLiteral(
        "Blokkily is a music workstation; these tools drive the running window. A session is "
        "a song: tracks (each owns an instrument and a mixer strip), patterns (sections; each "
        "track has its own part of each), and clips that place patterns on the arrangement's "
        "bars. Every index is from 0. Steps are sixteenth notes. Start with get_session. "
        "write_notes writes notes, each retuned from its key by cents and sung to a lyric if "
        "asked; set_lyrics lays a line of lyrics on a part; set_section "
        "sizes a pattern and place_section arranges it; transport plays them. set_instrument, set_soundfont_preset and set_parameter choose sounds; "
        "set_parameter also reaches an effect on a return or the master. Anything else the interface can do "
        "is a method: describe lists them, call runs one (e.g. songModel.setTrackGain(track, "
        "dB), songModel.addPattern(), songModel.placeClip(track, bar), appController.setTempo"
        "(bpm), appController.setStepLock(track, pattern, step, slot, parameter, value, "
        "modulation) to lock a parameter on one note (slot -1 the instrument, 0.. an insert; "
        "setInsertStepLock(track, pattern, step, kind, bus, slot, ...) for a return's or the "
        "master's insert), "
        "songModel.undo(), appController.saveProjectFile(path), "
        "appController.exportAudioFile(path), exportAudioRange(path, fromBar, toBar) and "
        "exportTrackFilesRange(directory, fromBar, toBar) for some bars of it, "
        "appController.importMusicXmlFile(path) and "
        "exportMusicXmlFile(path) for a whole score). Every edit can be undone. screenshot shows the "
        "window. analyze_render measures how the mix sounds: loudness, peaks and where the "
        "energy sits, of the master, one bus, or every bus at once, over the song or some "
        "bars of it. set_meter changes the song's time signature from a bar on. get_score reads a part as staff notation (spelled notes plus "
        "MusicXML); set_clef, set_key_signature and set_time_signature dress the "
        "score; export_musicxml writes a MusicXML file and import_musicxml reads one "
        "back as one undo step. guide_events narrates what happened lately, "
        "guide_lesson runs the guide's interactive lessons, guide_ask asks it a "
        "theory question.");
}

QJsonObject textResult(const QString& text, bool error) {
    QJsonObject result{
        {"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", text}}}}};
    if (error) result.insert("isError", true);
    return result;
}

QJsonObject jsonResult(const QJsonValue& value) {
    QJsonDocument document;
    if (value.isObject()) document.setObject(value.toObject());
    else if (value.isArray()) document.setArray(value.toArray());
    else return textResult(value.toVariant().toString());
    QJsonObject result = textResult(QString::fromUtf8(document.toJson(QJsonDocument::Compact)));
    if (value.isObject()) result.insert("structuredContent", value);
    return result;
}

QJsonObject errorResponse(const QJsonValue& id, int code, const QString& message) {
    return {{"jsonrpc", "2.0"},
            {"id", id},
            {"error", QJsonObject{{"code", code}, {"message", message}}}};
}

namespace {

QString typeOf(const QJsonValue& value) {
    switch (value.type()) {
    case QJsonValue::Bool: return QStringLiteral("a boolean");
    case QJsonValue::Double: return QStringLiteral("a number");
    case QJsonValue::String: return QStringLiteral("a string");
    case QJsonValue::Array: return QStringLiteral("an array");
    case QJsonValue::Object: return QStringLiteral("an object");
    default: return QStringLiteral("null");
    }
}

}  // namespace

QString argumentProblem(const QJsonObject& schema, const QJsonObject& arguments, const QString& where) {
    const auto properties = schema.value("properties").toObject();
    for (auto it = arguments.begin(); it != arguments.end(); ++it) {
        const auto property = properties.value(it.key()).toObject();
        const QString type = property.value("type").toString();
        const QJsonValue value = it.value();
        const QString name = where.isEmpty() ? it.key() : QStringLiteral("%1.%2").arg(where, it.key());
        bool fits = true;
        if (type == "integer")
            fits = value.isDouble() && std::isfinite(value.toDouble()) &&
                   value.toDouble() == std::floor(value.toDouble());
        else if (type == "number") fits = value.isDouble();
        else if (type == "boolean") fits = value.isBool();
        else if (type == "string") fits = value.isString();
        else if (type == "array") fits = value.isArray();
        else if (type == "object") fits = value.isObject();
        if (!fits)
            return QStringLiteral("%1 must be %2, not %3.")
                .arg(name,
                     type == "integer" ? QStringLiteral("a whole number")
                     : type == "number" ? QStringLiteral("a number")
                     : type == "boolean" ? QStringLiteral("true or false")
                     : QStringLiteral("a ") + type,
                     typeOf(value));
        if (const auto allowed = property.value("enum").toArray(); !allowed.isEmpty() && !allowed.contains(value))
            return QStringLiteral("%1 is not one of the choices.").arg(name);
        // An array's items are checked against the items' schema, one by one.
        const auto items = property.value("items").toObject();
        if (type == "array" && items.value("type").toString() == "object") {
            const auto entries = value.toArray();
            for (int index = 0; index < entries.size(); ++index) {
                const QString item = QStringLiteral("%1[%2]").arg(name).arg(index);
                if (!entries.at(index).isObject())
                    return QStringLiteral("%1 must be an object, not %2.").arg(item, typeOf(entries.at(index)));
                const auto problem = argumentProblem(items, entries.at(index).toObject(), item);
                if (!problem.isEmpty()) return problem;
            }
        }
    }
    return {};
}

std::optional<QByteArray> Session::handleLine(const QByteArray& line) {
    if (line.trimmed().isEmpty()) return std::nullopt;
    QJsonParseError error{};
    const auto document = QJsonDocument::fromJson(line, &error);
    std::optional<QJsonObject> reply;
    if (error.error != QJsonParseError::NoError || !document.isObject())
        reply = errorResponse(QJsonValue::Null, -32700, QStringLiteral("Parse error"));
    else
        reply = handle(document.object());
    if (!reply) return std::nullopt;
    return QJsonDocument(*reply).toJson(QJsonDocument::Compact);
}

std::optional<QJsonObject> Session::handle(const QJsonObject& message) {
    const QString method = message.value("method").toString();
    // A notification (initialized, cancelled) is acknowledged by silence.
    if (!message.contains("id")) return std::nullopt;
    const QJsonValue id = message.value("id");
    const auto answer = [&id](QJsonObject result) {
        return QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
    };
    if (method == "initialize") {
        const QString asked = message.value("params").toObject().value("protocolVersion").toString();
        QString version = QString::fromLatin1(protocol_versions[0]);
        for (const char* known : protocol_versions)
            if (asked == QLatin1String(known)) version = asked;
        return answer({{"protocolVersion", version},
                       {"capabilities", QJsonObject{{"tools", QJsonObject{{"listChanged", false}}}}},
                       {"serverInfo",
                        QJsonObject{{"name", "blokkily"}, {"version", BLOKKILY_VERSION}}},
                       {"instructions", instructions()}});
    }
    if (method == "ping") return answer({});
    if (method == "tools/list") return answer({{"tools", toolDefinitions()}});
    if (method == "tools/call") {
        const auto params = message.value("params").toObject();
        const QString name = params.value("name").toString();
        std::optional<QJsonObject> input;
        for (const auto& entry : toolDefinitions())
            if (entry.toObject().value("name").toString() == name)
                input = entry.toObject().value("inputSchema").toObject();
        if (!input) return errorResponse(id, -32602, QStringLiteral("Unknown tool: %1").arg(name));
        // An argument of the wrong type is refused before the tool runs, so a
        // list given as a track can never fall back to the selected track.
        const auto arguments = params.value("arguments").toObject();
        if (const auto problem = argumentProblem(*input, arguments); !problem.isEmpty())
            return answer(textResult(problem, true));
        return answer(run_tool_(name, arguments));
    }
    return errorResponse(id, -32601, QStringLiteral("Method not found: %1").arg(method));
}

}  // namespace blokkily::mcp
