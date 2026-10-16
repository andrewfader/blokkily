#include "mcp_protocol.hpp"

#include <QJsonDocument>
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
         }},
        {"required", QJsonArray{"step", "key"}},
    };
    return {
        tool("get_session",
             "The whole session at a glance: tracks with their instruments, mixer settings, "
             "effects and sends, the returns and the master's effects, the patterns with their "
             "length and swing, where clips sit in the arrangement, tempo, meter, the "
             "transport, what is selected, and whether there are unsaved changes.",
             schema({})),
        tool("get_pattern",
             "The notes of one track's part of one pattern: start step, length in steps, key "
             "and velocity of each, its cents when it is retuned from its key, and the "
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
        tool("place_section",
             "Places a pattern (section) in the arrangement at a bar: a clip of each track's "
             "part, or of one track's with track. Bars already holding a clip are left as "
             "they are.",
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
             "maximum and default of each, for set_parameter.",
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
             "a reference mix. Silent levels read null. Changes nothing in the song.",
             schema({{"track", integer("Measure this track's bus alone.")},
                     {"return", integer("Measure this return bus alone.")},
                     {"file", QJsonObject{{"type", "string"},
                                          {"description", "Measure this WAVE file instead of rendering."}}}})),
        tool("describe",
             "Every method and property the window's controls use, for `call` and "
             "`get_property`. Covers what the purpose-built tools do not: the mixer, effects, "
             "automation, clips, tempo, tuning, saving and exporting, undo and redo.",
             schema({{"object", QJsonObject{{"type", "string"}, {"enum", objects}}}})),
        tool("get_score",
             "One track's part of one pattern as staff notation: every note spelled "
             "(C4, F#3), its note value and dots, whether it is tied, its staff position "
             "and whether the accidental is drawn — plus the whole part as a MusicXML "
             "document. Clef, key and meter come from the part's settings (set_clef, "
             "set_key_signature, set_time_signature) unless given here.",
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
             "The clef the score projection reads one track's part of one pattern in.",
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
             "The key signature a pattern's parts are spelled in, in fifths (-7 to 7; "
             "0 is C major, 1 one sharp). Applies to every track's part of the pattern.",
             schema({{"section", section},
                     {"fifths", integer("The key signature in fifths, -7 to 7.")}},
                    {"fifths"})),
        tool("set_time_signature",
             "The meter a pattern's parts are read in. With unmetered, no time signature "
             "is drawn and no barlines fall (chant).",
             schema({{"section", section},
                     {"beats", integer("Beats in the bar.")},
                     {"unit", integer("Beat unit: 1, 2, 4, 8, 16 or 32.")},
                     {"unmetered",
                      QJsonObject{{"type", "boolean"},
                                  {"description", "Chant: no meter. Default false."}}}},
                    {})),
        tool("export_musicxml",
             "Writes a pattern as a MusicXML 4.0 partwise file: one part per track "
             "that plays it (or one track's part with track), read back to prove it.",
             schema({{"section", section},
                     {"track", track},
                     {"path",
                      QJsonObject{{"type", "string"},
                                  {"description", "Where to write the .musicxml file."}}}},
                    {"path"})),
        tool("import_musicxml",
             "Reads a MusicXML partwise score into one pattern as one step of undo: "
             "part i lands on track i from `track`. With replace, each part is emptied "
             "first. Notes that do not fit the pattern are refused and counted.",
             schema({{"track", track},
                     {"section", section},
                     {"replace",
                      QJsonObject{{"type", "boolean"},
                                  {"description", "Empty each part before writing. Default false."}}},
                     {"xml",
                      QJsonObject{{"type", "string"},
                                  {"description", "The MusicXML document, as text."}}},
                     {"path",
                      QJsonObject{{"type", "string"},
                                  {"description", "Read the document from this file instead."}}}},
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
        "write_notes writes notes, each retuned from its key by cents if asked; set_section "
        "sizes a pattern and place_section arranges it; transport plays them. set_instrument, set_soundfont_preset and set_parameter choose sounds; "
        "set_parameter also reaches an effect on a return or the master. Anything else the interface can do "
        "is a method: describe lists them, call runs one (e.g. songModel.setTrackGain(track, "
        "dB), songModel.addPattern(), songModel.placeClip(track, bar), appController.setTempo"
        "(bpm), appController.setStepLock(track, pattern, step, slot, parameter, value, "
        "modulation) to lock a parameter on one note (slot -1 the instrument, 0.. an insert; "
        "setInsertStepLock(track, pattern, step, kind, bus, slot, ...) for a return's or the "
        "master's insert), "
        "songModel.undo(), appController.saveProjectFile(path), "
        "appController.exportAudioFile(path)). Every edit can be undone. screenshot shows the "
        "window. analyze_render measures how the mix sounds: loudness, peaks and where the "
        "energy sits. get_score reads a part as staff notation (spelled notes plus "
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
        bool known = false;
        for (const auto& entry : toolDefinitions())
            if (entry.toObject().value("name").toString() == name) known = true;
        if (!known) return errorResponse(id, -32602, QStringLiteral("Unknown tool: %1").arg(name));
        return answer(run_tool_(name, params.value("arguments").toObject()));
    }
    return errorResponse(id, -32601, QStringLiteral("Method not found: %1").arg(method));
}

}  // namespace blokkily::mcp
