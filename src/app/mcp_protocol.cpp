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
         }},
        {"required", QJsonArray{"step", "key"}},
    };
    return {
        tool("get_session",
             "The whole session at a glance: tracks with their instruments and mixer settings, "
             "the patterns, where clips sit in the arrangement, tempo, meter, the transport, "
             "what is selected, and whether there are unsaved changes.",
             schema({})),
        tool("get_pattern",
             "The notes of one track's part of one pattern: start step, length in steps, key "
             "and velocity of each.",
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
             "0, 1, ...): id, name, minimum, maximum and default of each, for set_parameter.",
             schema({{"track", track},
                     {"slot", integer("-1 for the instrument (the default), 0.. for an effect.")}})),
        tool("set_parameter",
             "Sets one parameter of a track's instrument or effect for the whole song, as a "
             "lane holding that value (heard from the next play). A synth's preset is usually "
             "a parameter called Preset.",
             schema({{"track", track},
                     {"slot", integer("-1 for the instrument (the default), 0.. for an effect.")},
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
        tool("describe",
             "Every method and property the window's controls use, for `call` and "
             "`get_property`. Covers what the purpose-built tools do not: the mixer, effects, "
             "automation, clips, tempo, tuning, saving and exporting, undo and redo.",
             schema({{"object", QJsonObject{{"type", "string"}, {"enum", objects}}}})),
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
        "write_notes writes notes; transport plays them. set_instrument, set_soundfont_preset "
        "and set_parameter choose sounds. Anything else the interface can do "
        "is a method: describe lists them, call runs one (e.g. songModel.setTrackGain(track, "
        "dB), songModel.addPattern(), songModel.placeClip(track, bar), appController.setTempo"
        "(bpm), songModel.undo(), appController.saveProjectFile(path), "
        "appController.exportAudioFile(path)). Every edit can be undone. screenshot shows the "
        "window.");
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
