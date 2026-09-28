// The assistant's wire format and scripted backend, checked against canned
// replies — no server, no key, no network (AGENTS.md). Applying, undo and
// scale snapping are verified end to end by bdd_llm_assistant against the
// live models, the way every GUI-model behavior in this suite is.
//
// Run with a case name; each case is its own CTest test (llm_<case>).
// features/llm_assistant.feature names the scenario each case executes.

#include "scripted_backend.hpp"
#include "system_prompt.hpp"
#include "trigger_json.hpp"

#include "blokkily/model/pattern.hpp"

#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTemporaryFile>
#include <QTimer>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <variant>

using namespace blokkily;
using namespace blokkily::llm;

namespace {

int dummy_argc = 0;
char* dummy_argv[] = {nullptr};

void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

constexpr Tick tps = 120;      // ticks per step, as PatternModel counts them
constexpr int steps = 16;      // a 4/4 bar of sixteenths
constexpr int key_limit = 120; // ten periods of twelve

const Note& as_note(const Trigger& trigger) {
    require(std::holds_alternative<Note>(trigger.musical_data), "expected a note");
    return std::get<Note>(trigger.musical_data);
}

// Scenario: a reply in the wire dialect becomes triggers.
void parse_replace_case() {
    const auto result = parseResponse(
        QStringLiteral(R"({"mode": "replace", "triggers": [
            {"step": 0, "length_steps": 1, "note": {"key": 57, "velocity": 0.8}},
            {"step": 4, "length_steps": 2, "note": {"key": 60, "velocity": 0.7}},
            {"step": 8, "length_steps": 1,
             "chord": {"root": 57, "intervals": [0, 3, 7], "velocity": 0.75}}]})"),
        tps, steps, key_limit);
    require(result.ok, "valid reply must parse: " + result.error.toStdString());
    require(result.response.mode == Mode::replace, "mode must be replace");
    require(result.response.triggers.size() == 3, "three triggers expected");
    const auto& triggers = result.response.triggers;
    require(triggers[0].start == 0 && as_note(triggers[0]).key == 57, "first trigger wrong");
    require(triggers[1].start == 4 * tps && triggers[1].duration == 2 * tps,
            "second trigger placement wrong");
    require(std::holds_alternative<Chord>(triggers[2].musical_data), "third must be a chord");
    require(std::get<Chord>(triggers[2].musical_data).intervals.size() == 3,
            "chord voices wrong");
}

// Scenario: add and modify modes are honored.
void parse_add_and_modify_case() {
    const auto added = parseResponse(
        QStringLiteral(R"({"mode": "add", "triggers": [
            {"step": 2, "note": {"key": 42, "velocity": 0.5}}]})"),
        tps, steps, key_limit);
    require(added.ok && added.response.mode == Mode::add, "mode add must survive");
    const auto modified = parseResponse(
        QStringLiteral(R"({"mode": "modify", "triggers": [
            {"step": 0, "note": {"key": 60, "velocity": 0.9}}]})"),
        tps, steps, key_limit);
    require(modified.ok && modified.response.mode == Mode::modify, "mode modify must survive");
    // A missing mode means replace: "give me a …" answers need not say so.
    const auto implied = parseResponse(
        QStringLiteral(R"({"triggers": [{"step": 0, "note": {"key": 60}}]})"),
        tps, steps, key_limit);
    require(implied.ok && implied.response.mode == Mode::replace,
            "an unnamed mode must mean replace");
}

// Scenario: markdown fences and padding around the JSON are tolerated.
void parse_fenced_and_padded_case() {
    const auto result = parseResponse(
        QStringLiteral("Here is your pattern:\n```json\n{\"mode\": \"replace\", "
                       "\"triggers\": [{\"step\": 3, \"note\": {\"key\": 61}}]}\n```\n"
                       "Hope that grooves!"),
        tps, steps, key_limit);
    require(result.ok, "fenced reply must parse: " + result.error.toStdString());
    require(result.response.triggers.size() == 1, "one trigger expected");
    require(result.response.triggers[0].start == 3 * tps, "fenced trigger misplaced");
}

// Scenario: pure prose is rejected, not crashed on.
void parse_rejects_prose_case() {
    const auto result = parseResponse(
        QStringLiteral("I'd love to help, but let me explain music theory instead…"),
        tps, steps, key_limit);
    require(!result.ok, "prose must not parse");
    require(!result.error.isEmpty(), "a rejection must say why");
    // An empty reply and a broken object fail the same readable way.
    require(!parseResponse(QString(), tps, steps, key_limit).ok, "empty must fail");
    require(!parseResponse(QStringLiteral("{not json"), tps, steps, key_limit).ok,
            "broken JSON must fail");
    require(!parseResponse(QStringLiteral("{\"mode\": \"replace\", \"triggers\": []}"),
                           tps, steps, key_limit)
                 .ok,
            "an empty trigger list must fail");
}

// Scenario: out-of-range fields are repaired and admitted.
void parse_clamps_and_repairs_case() {
    const auto result = parseResponse(
        QStringLiteral(R"({"mode": "replace", "triggers": [
            {"step": 0, "length_steps": 64, "probability": 1.7, "ratchets": 0,
             "note": {"key": 400, "velocity": 2.5}}]})"),
        tps, steps, key_limit);
    require(result.ok, "repairable reply must parse");
    const auto& trigger = result.response.triggers.front();
    require(as_note(trigger).velocity <= 1.0F, "velocity must be clamped");
    require(trigger.probability <= 1.0F, "probability must be clamped");
    require(trigger.ratchets >= 1, "ratchets must be at least one");
    require(as_note(trigger).key < key_limit, "key must be clamped into range");
    require(trigger.start + trigger.duration <= steps * tps,
            "a note running past the end must be tucked inside");
    require(!result.corrections.isEmpty(), "repairs must be admitted");
}

// Scenario: a trigger beyond the pattern's end never sounds.
void parse_drops_out_of_pattern_case() {
    const auto result = parseResponse(
        QStringLiteral(R"({"mode": "replace", "triggers": [
            {"step": 0, "note": {"key": 60}},
            {"step": 40, "note": {"key": 62}}]})"),
        tps, steps, key_limit);
    require(result.ok, "one good trigger must carry the reply");
    require(result.response.triggers.size() == 1, "the out-of-range trigger must drop");
    // And a reply whose every trigger is unusable fails whole.
    const auto all_bad = parseResponse(
        QStringLiteral(R"({"triggers": [{"step": 99, "note": {"key": 62}}]})"),
        tps, steps, key_limit);
    require(!all_bad.ok, "a reply with no usable trigger must fail");
}

// Scenario: what the model is shown is what it can answer.
void roundtrip_triggers_case() {
    Pattern pattern(1920, 480);
    Trigger note;
    note.start = 2 * tps;
    note.duration = tps / 2;
    note.probability = 0.6F;
    note.ratchets = 3;
    note.micro_offset = 12;
    note.musical_data = Note{64, 0.7F, 0.0F};
    (void)pattern.add(note);
    Trigger chord;
    chord.start = 8 * tps;
    chord.duration = 2 * tps;
    chord.musical_data = Chord{57, {0, 3, 7}, 0, tps / 4, {}, 0.75F, {0.8F, 0.6F, 0.7F}, {}};
    (void)pattern.add(chord);

    const auto json = triggersToJson(pattern.events(), tps);
    QJsonObject wrapper{{"mode", "replace"}, {"triggers", json}};
    const auto text = QString::fromUtf8(QJsonDocument(wrapper).toJson());
    const auto result = parseResponse(text, tps, steps, key_limit);
    require(result.ok, "a round trip must parse: " + result.error.toStdString());
    require(result.response.triggers.size() == 2, "both triggers must survive");
    const auto& back = result.response.triggers;
    require(back[0].start == note.start && back[0].duration == note.duration,
            "note placement must survive");
    require(as_note(back[0]).key == 64, "note key must survive");
    require(std::abs(back[0].probability - note.probability) < 0.001F,
            "probability must survive");
    require(back[0].ratchets == 3 && back[0].micro_offset == 12,
            "ratchets and micro-offset must survive");
    require(std::holds_alternative<Chord>(back[1].musical_data), "chord must survive");
    const auto& backChord = std::get<Chord>(back[1].musical_data);
    require(backChord.root == 57 && backChord.intervals.size() == 3 &&
                backChord.intervals[1] == 3,
            "chord shape must survive");
    require(backChord.velocities.size() == 3 &&
                std::abs(backChord.velocities[1] - 0.6F) < 0.001F,
            "per-voice velocities must survive");
    require(backChord.strum == tps / 4, "strum must survive");
}

// Scenario: the gate's backend answers from a script, not a server.
void scripted_backend_answers_case(int argc, char** argv) {
    QTemporaryFile script;
    require(script.open(), "script file must open");
    script.write("{\"match\": \"pentatonic\", \"reply\": \"{\\\"mode\\\": \\\"replace\\\", "
                 "\\\"triggers\\\": []}\"}\n");
    script.flush();
    qputenv("BLOKKILY_LLM_SCRIPT", script.fileName().toUtf8());

    QCoreApplication app(argc, argv);
    ScriptedBackend backend;
    Request request;
    request.prompt = QStringLiteral("a minor pentatonic ascending");
    Reply matched;
    QEventLoop first;
    backend.complete(request, [&](Reply reply) {
        matched = std::move(reply);
        first.quit();
    });
    first.exec();
    require(matched.ok, "a matching prompt must be answered");
    require(matched.text.contains("replace"), "the scripted bytes must come back");
    require(backend.lastRequest().prompt == request.prompt,
            "the backend must remember what it was asked");

    request.prompt = QStringLiteral("something the script never mentions");
    Reply missed;
    QEventLoop second;
    backend.complete(request, [&](Reply reply) {
        missed = std::move(reply);
        second.quit();
    });
    second.exec();
    require(!missed.ok && !missed.error.isEmpty(),
            "an unmatched prompt must fail readably");
}

// Scenario: the standing instructions document the schema the parser reads.
void prompt_documents_schema_case() {
    const auto prompt = systemPrompt(steps);
    for (const char* word :
         {"\"replace\"", "\"add\"", "\"modify\"", "\"step\"", "\"length_steps\"",
          "\"note\"", "\"chord\"", "\"velocity\"", "\"probability\"", "\"ratchets\"",
          "\"micro_offset\"", "\"play_on_loop\"", "\"strum_steps\"", "\"intervals\""})
        require(prompt.contains(QLatin1String(word)),
                std::string("the prompt must document ") + word);
    // The length the prompt announces is the pattern's real length.
    require(prompt.contains("16 steps long"), "the prompt must say the pattern's length");
}

// The live backends live in the network. The unit tests stand up a
// QNetworkAccessManager subclass that intercepts the POST, captures the
// outgoing bytes, and returns whatever response the case wants — no live
// server, no key on disk (AGENTS.md: tests must not depend on a network
// connection).
class CapturingNetworkAccessManager final : public QNetworkAccessManager {
public:
    explicit CapturingNetworkAccessManager(QObject* parent = nullptr)
        : QNetworkAccessManager(parent) {}
    class PendingReply final : public QNetworkReply {
    public:
        PendingReply(QNetworkAccessManager* parent, QNetworkRequest req,
                     QByteArray payload)
            : QNetworkReply(parent), payload_(std::move(payload)) {
            setRequest(req);
            setUrl(req.url());
            setOperation(QNetworkAccessManager::PostOperation);
            open(QIODevice::ReadOnly);
        }
        void abort() override {}
        void close() override {}
        qint64 readData(char* data, qint64 maxlen) override {
            const qint64 give = std::min<qint64>(
                maxlen, static_cast<qint64>(payload_.size()) - consumed_);
            std::memcpy(data, payload_.constData() + consumed_, give);
            consumed_ += give;
            return give;
        }
        qint64 bytesAvailable() const override {
            return payload_.size() - consumed_;
        }
        bool isSequential() const override { return true; }

        // Public facade over Qt's protected hooks, so the owning
        // CapturingNetworkAccessManager can drive the reply without
        // friending the test class into Qt's internals.
        using QNetworkReply::setError;
        using QNetworkReply::setFinished;

    private:
        QByteArray payload_;
        qint64 consumed_ = 0;
    };

    struct Capture {
        QNetworkRequest request;
        QByteArray body;
        QByteArray response_body;
        QNetworkReply::NetworkError error = QNetworkReply::NoError;
    };

    Capture capture;

    QNetworkReply* createRequest(Operation op, const QNetworkRequest& req,
                                 QIODevice* outgoing = nullptr) override {
        if (outgoing) capture.body = outgoing->readAll();
        if (op != PostOperation) {
            // We never expect anything but POST in these tests.
            return QNetworkAccessManager::createRequest(op, req, outgoing);
        }
        capture.request = req;
        auto* reply = new PendingReply(this, req, capture.response_body);
        if (capture.error != QNetworkReply::NoError)
            reply->setError(capture.error, QStringLiteral("synthetic error"));
        // Defer finishing one tick so it lands on the event loop the way a
        // real reply would, letting the test's QEventLoop see it. Qt6's
        // QTimer::singleShot does not overload on a free lambda with a
        // receiver, so we use invokeMethod with a queued connection.
        QMetaObject::invokeMethod(this, [reply] {
            reply->setFinished(true);
            emit reply->finished();
        }, Qt::QueuedConnection);
        return reply;
    }
};

Request sampleRequest() {
    Request r;
    r.systemPrompt = QStringLiteral("system");
    r.context = QStringLiteral("{\"track\":\"drums\"}");
    r.prompt = QStringLiteral("four-on-the-floor hats");
    r.history.push_back({QStringLiteral("an earlier line"), QStringLiteral("4 triggers (replace)")});
    return r;
}

void runBackend(LlmBackend* backend, Request request, Reply& out) {
    QEventLoop loop;
    bool done = false;
    backend->complete(request, [&](Reply reply) {
        out = std::move(reply);
        done = true;
        loop.quit();
    });
    // A synchronous completion (e.g. a missing-key error path that calls
    // the completion inline) will never pump the loop, so quit before
    // exec() and skip the wait.
    if (done) return;
    loop.exec();
}

// Every live backend, described once so each scenario runs against all of
// the backends it applies to. `dialect` picks the wire shape: the
// OpenAI-compatible chat endpoint, Gemini's generateContent, or Ollama's
// generate.
enum class Dialect { chat, gemini, ollama };

struct Vendor {
    const char* name;       // the picker key, and the case-name prefix
    Dialect dialect;
    QString label;          // what the picker and the errors call it
    const char* keyVar;     // nullptr when the backend needs no key
    const char* urlVar;     // nullptr when the endpoint is fixed
    const char* modelVar;
    QString endpoint;       // the default request URL
    QString model;          // the default model id
};

const std::vector<Vendor>& vendors() {
    static const std::vector<Vendor> all{
        {"minimax", Dialect::chat, "MiniMax", "BLOKKILY_MINIMAX_KEY", "BLOKKILY_MINIMAX_URL",
         "BLOKKILY_MINIMAX_MODEL", "https://api.minimax.io/v1/chat/completions", "MiniMax-M3"},
        {"openai", Dialect::chat, "ChatGPT", "BLOKKILY_OPENAI_KEY", "BLOKKILY_OPENAI_URL",
         "BLOKKILY_OPENAI_MODEL", "https://api.openai.com/v1/chat/completions", "gpt-4o-mini"},
        {"openrouter", Dialect::chat, "OpenRouter", "BLOKKILY_OPENROUTER_KEY",
         "BLOKKILY_OPENROUTER_URL", "BLOKKILY_OPENROUTER_MODEL",
         "https://openrouter.ai/api/v1/chat/completions", "openai/gpt-4o-mini"},
        {"gemini", Dialect::gemini, "Gemini", "BLOKKILY_GEMINI_KEY", nullptr,
         "BLOKKILY_GEMINI_MODEL",
         "https://generativelanguage.googleapis.com/v1beta/models/"
         "gemini-2.5-flash:generateContent",
         "gemini-2.5-flash"},
        {"ollama", Dialect::ollama, "Ollama", nullptr, "BLOKKILY_OLLAMA_URL",
         "BLOKKILY_OLLAMA_MODEL", "http://localhost:11434/api/generate", "llama3.1"},
    };
    return all;
}

// The picker names ChatGPT "chatgpt"; the test cases say "openai".
QString pickerName(const Vendor& vendor) {
    return QLatin1String(vendor.name) == QLatin1String("openai") ? QStringLiteral("chatgpt")
                                                                 : QString::fromLatin1(vendor.name);
}

// A fresh backend with defaults, a key when it takes one, and the capturing
// network in place of a live one.
std::unique_ptr<LlmBackend> standUp(const Vendor& vendor, CapturingNetworkAccessManager*& nam,
                                    QObject* parent) {
    if (vendor.keyVar) qputenv(vendor.keyVar, "test-key");
    if (vendor.urlVar) qunsetenv(vendor.urlVar);
    qunsetenv(vendor.modelVar);
    auto backend = makeBackend(pickerName(vendor), parent);
    require(backend != nullptr, std::string("no backend named ") + vendor.name);
    nam = new CapturingNetworkAccessManager;
    backend->setNetworkForTesting(nam);
    return backend;
}

// The provider's successful answer carrying `content` as the model's text.
QByteArray successBody(Dialect dialect, const QString& content) {
    QJsonObject root;
    switch (dialect) {
    case Dialect::chat:
        root["choices"] = QJsonArray{
            QJsonObject{{"message", QJsonObject{{"role", "assistant"}, {"content", content}}}}};
        break;
    case Dialect::gemini:
        root["candidates"] = QJsonArray{QJsonObject{
            {"content", QJsonObject{{"parts", QJsonArray{QJsonObject{{"text", content}}}}}}}};
        break;
    case Dialect::ollama: root["response"] = content; break;
    }
    return QJsonDocument(root).toJson();
}

// The provider's refusal, shaped the way that provider shapes it.
QByteArray errorBody(Dialect dialect, const QString& message) {
    if (dialect == Dialect::ollama) return QJsonDocument(QJsonObject{{"error", message}}).toJson();
    return QJsonDocument(QJsonObject{{"error", QJsonObject{{"message", message}, {"code", 401}}}})
        .toJson();
}

const QString oneNote = QStringLiteral(
    R"({"mode":"replace","triggers":[{"step":0,"length_steps":1,"note":{"key":60,"velocity":0.8}}]})");

// Scenario: each backend builds the request its provider expects — the
// endpoint, the credentials, the model, a JSON-only answer, the standing
// instructions, and the conversation with each turn said exactly once.
void backend_builds_request_case(const Vendor& vendor) {
    QCoreApplication app(dummy_argc, dummy_argv);
    CapturingNetworkAccessManager* nam = nullptr;
    auto backend = standUp(vendor, nam, &app);
    nam->capture.response_body = successBody(vendor.dialect, oneNote);
    Reply reply;
    runBackend(backend.get(), sampleRequest(), reply);
    require(reply.ok, "a happy-path reply must succeed: " + reply.error.toStdString());

    const auto& sent = nam->capture.request;
    const auto body = QJsonDocument::fromJson(nam->capture.body).object();
    const auto everything = QString::fromUtf8(nam->capture.body);
    require(sent.url().toString() == vendor.endpoint,
            "wrong endpoint: " + sent.url().toString().toStdString());
    require(!sent.url().toString().contains("test-key"), "the key must never ride in the URL");
    require(everything.count("an earlier line") == 1,
            "an earlier turn must be sent exactly once");
    require(everything.contains("four-on-the-floor hats"), "the prompt must be sent");
    require(everything.contains("\\\"track\\\":\\\"drums\\\""),
            "the session context must be sent");

    switch (vendor.dialect) {
    case Dialect::chat: {
        require(sent.rawHeader("Authorization") == "Bearer test-key",
                "Authorization must be a Bearer of the key");
        require(body.value("model").toString() == vendor.model, "default model id must be sent");
        require(body.value("response_format").toObject().value("type").toString() ==
                    "json_object",
                "response_format must force JSON so the wire dialect survives");
        const auto messages = body.value("messages").toArray();
        // system, the earlier turn and its outcome, then the new line.
        require(messages.size() == 4, "system + one earlier exchange + the new line");
        require(messages.first().toObject().value("role").toString() == "system" &&
                    messages.first().toObject().value("content").toString() == "system",
                "first message must be the system prompt");
        require(messages.last().toObject().value("content").toString().contains(
                    "four-on-the-floor hats"),
                "the producer's prompt must be in the last user turn");
        break;
    }
    case Dialect::gemini:
        require(sent.rawHeader("x-goog-api-key") == "test-key",
                "the Gemini key must travel in its header");
        require(body.value("generationConfig").toObject().value("response_mime_type").toString() ==
                    "application/json",
                "Gemini must be asked for JSON");
        require(body.value("system_instruction").toObject().value("parts").toArray().first()
                        .toObject().value("text").toString() == "system",
                "the system prompt must be Gemini's system instruction");
        break;
    case Dialect::ollama:
        require(body.value("model").toString() == vendor.model, "default model id must be sent");
        require(body.value("format").toString() == "json", "Ollama must be asked for JSON");
        require(body.value("system").toString() == "system", "the system prompt must be sent");
        require(!body.value("stream").toBool(true), "Ollama must answer in one piece");
        break;
    }
}

// Scenario: the model's text is what reaches the wire-dialect parser.
void backend_parses_reply_case(const Vendor& vendor) {
    QCoreApplication app(dummy_argc, dummy_argv);
    CapturingNetworkAccessManager* nam = nullptr;
    auto backend = standUp(vendor, nam, &app);
    nam->capture.response_body = successBody(vendor.dialect, oneNote);
    Reply reply;
    runBackend(backend.get(), sampleRequest(), reply);
    require(reply.ok, "reply must succeed: " + reply.error.toStdString());
    const auto parsed = parseResponse(reply.text, tps, steps, key_limit);
    require(parsed.ok, "the reply must parse: " + parsed.error.toStdString());
    require(parsed.response.triggers.size() == 1 &&
                as_note(parsed.response.triggers[0]).key == 60,
            "the model-chosen key must reach the parser");
}

// Scenario: no key means a readable error naming the variable to set.
void backend_missing_key_errors_case(const Vendor& vendor) {
    QCoreApplication app(dummy_argc, dummy_argv);
    require(vendor.keyVar != nullptr, "this backend takes no key");
    qunsetenv(vendor.keyVar);
    auto backend = makeBackend(pickerName(vendor), &app);
    require(backend->displayName().contains(vendor.label),
            "the picker must label this backend " + vendor.label.toStdString());
    Reply reply;
    runBackend(backend.get(), sampleRequest(), reply);
    require(!reply.ok, "no key must mean no success");
    require(reply.error.contains(QLatin1String(vendor.keyVar)),
            "the error must name the variable to set");
}

// Scenario: a transport failure surfaces with the backend's name.
void backend_network_error_is_readable_case(const Vendor& vendor) {
    QCoreApplication app(dummy_argc, dummy_argv);
    CapturingNetworkAccessManager* nam = nullptr;
    auto backend = standUp(vendor, nam, &app);
    nam->capture.error = QNetworkReply::ConnectionRefusedError;
    Reply reply;
    runBackend(backend.get(), sampleRequest(), reply);
    require(!reply.ok, "a refused connection must not succeed");
    require(reply.error.contains(vendor.label + " did not answer"),
            "the error must say which backend did not answer: " + reply.error.toStdString());
}

// Scenario: the provider's own explanation reaches the producer. Providers
// send it in the body of an HTTP error (401 bad key, 402 no credit, 404
// unknown model), so the reply is both an HTTP failure and a JSON body.
void backend_server_error_is_readable_case(const Vendor& vendor) {
    QCoreApplication app(dummy_argc, dummy_argv);
    CapturingNetworkAccessManager* nam = nullptr;
    auto backend = standUp(vendor, nam, &app);
    nam->capture.error = QNetworkReply::AuthenticationRequiredError;
    nam->capture.response_body = errorBody(vendor.dialect, "Incorrect API key provided");
    Reply reply;
    runBackend(backend.get(), sampleRequest(), reply);
    require(!reply.ok, "an error answer must not succeed");
    require(reply.error.contains("Incorrect API key provided") &&
                reply.error.contains(vendor.label),
            "the provider's message must reach the producer: " + reply.error.toStdString());
}

// Scenario: the URL and model variables override the defaults.
void backend_uses_custom_url_and_model_case(const Vendor& vendor) {
    QCoreApplication app(dummy_argc, dummy_argv);
    require(vendor.urlVar != nullptr, "this backend's endpoint is fixed");
    if (vendor.keyVar) qputenv(vendor.keyVar, "test-key");
    qputenv(vendor.urlVar, "https://example.test/llm/");
    qputenv(vendor.modelVar, "custom-model-1");
    auto backend = makeBackend(pickerName(vendor), &app);
    auto* nam = new CapturingNetworkAccessManager;
    backend->setNetworkForTesting(nam);
    nam->capture.response_body = successBody(vendor.dialect, oneNote);
    Reply reply;
    runBackend(backend.get(), sampleRequest(), reply);
    require(reply.ok, "reply must succeed: " + reply.error.toStdString());
    // The override replaces the API root; the backend's own path follows it.
    const auto path = vendor.dialect == Dialect::ollama ? QStringLiteral("/api/generate")
                                                        : QStringLiteral("/v1/chat/completions");
    require(nam->capture.request.url().toString() == "https://example.test/llm" + path,
            "the custom URL must be honoured: " +
                nam->capture.request.url().toString().toStdString());
    require(QJsonDocument::fromJson(nam->capture.body).object().value("model").toString() ==
                "custom-model-1",
            "the custom model must be honoured");
    require(backend->displayName().contains("custom-model-1"),
            "the picker must name the model in use");
}

// Runs "<vendor>_backend_<scenario>"; false when the name is not one.
bool runBackendCase(const std::string& name) {
    const auto marker = name.find("_backend_");
    if (marker == std::string::npos) return false;
    const auto prefix = name.substr(0, marker);
    const auto scenario = name.substr(marker + 9);
    for (const auto& vendor : vendors()) {
        if (prefix != vendor.name) continue;
        if (scenario == "builds_request") backend_builds_request_case(vendor);
        else if (scenario == "parses_reply") backend_parses_reply_case(vendor);
        else if (scenario == "missing_key_errors") backend_missing_key_errors_case(vendor);
        else if (scenario == "network_error_is_readable") backend_network_error_is_readable_case(vendor);
        else if (scenario == "server_error_is_readable") backend_server_error_is_readable_case(vendor);
        else if (scenario == "uses_custom_url_and_model") backend_uses_custom_url_and_model_case(vendor);
        else return false;
        return true;
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "pass a case name");
        const std::string name = argv[1];
        if (name == "parse_replace") parse_replace_case();
        else if (name == "parse_add_and_modify") parse_add_and_modify_case();
        else if (name == "parse_fenced_and_padded") parse_fenced_and_padded_case();
        else if (name == "parse_rejects_prose") parse_rejects_prose_case();
        else if (name == "parse_clamps_and_repairs") parse_clamps_and_repairs_case();
        else if (name == "parse_drops_out_of_pattern") parse_drops_out_of_pattern_case();
        else if (name == "roundtrip_triggers") roundtrip_triggers_case();
        else if (name == "scripted_backend_answers") scripted_backend_answers_case(argc, argv);
        else if (name == "prompt_documents_schema") prompt_documents_schema_case();
        else if (!runBackendCase(name)) throw std::runtime_error("unknown case " + name);
        std::cout << name << " passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
}
