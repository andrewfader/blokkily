// The assistant's wire format and scripted backend, checked against canned
// replies — no server, no key, no network (AGENTS.md). Applying, undo and
// scale snapping are verified end to end by bdd_llm_assistant against the
// live models, the way every GUI-model behavior in this suite is.
//
// Run with a case name; each case is its own CTest test (llm_<case>).
// features/llm_assistant.feature names the scenario each case executes.

#include "scripted_backend.hpp"
#include "minimax_backend.hpp"
#include "openai_backend.hpp"
#include "openrouter_backend.hpp"
#include "system_prompt.hpp"
#include "trigger_json.hpp"

#include "blokkily/model/pattern.hpp"

#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonDocument>
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

// The MiniMax backend lives in the network. The unit tests stand up a
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

// Scenario: the MiniMax backend builds the request shape the OpenAI chat
// dialect expects when the key is present.
void minimax_backend_builds_request_case() {
    QCoreApplication app(dummy_argc, dummy_argv);
    qputenv("BLOKKILY_MINIMAX_KEY", "test-key");
    qunsetenv("BLOKKILY_MINIMAX_URL");
    qunsetenv("BLOKKILY_MINIMAX_MODEL");
    auto* backend = makeMinimaxBackendForTesting(&app);
    std::unique_ptr<MinimaxBackend> owner(backend);
    auto* nam = new CapturingNetworkAccessManager(&app);
    nam->capture.response_body =
        R"({"choices":[{"message":{"content":"{\"mode\":\"replace\",\"triggers\":[]}"}}]})";
    setMinimaxBackendNetworkForTesting(backend, nam);

    Reply reply;
    runBackend(backend, sampleRequest(), reply);
    require(reply.ok, "a happy-path reply must succeed: " + reply.error.toStdString());

    // The URL hits the documented default endpoint + path.
    require(nam->capture.request.url().toString() ==
                "https://api.minimax.io/v1/chat/completions",
            "request must hit the default chat completions endpoint");
    // Bearer key in the Authorization header.
    require(nam->capture.request.rawHeader("Authorization") ==
                QByteArray("Bearer test-key"),
            "Authorization must be a Bearer of the key");

    const auto body = QJsonDocument::fromJson(nam->capture.body).object();
    require(body.value("model").toString() == "MiniMax-M3",
            "default model id must be sent");
    require(body.value("response_format").toObject().value("type").toString() ==
                "json_object",
            "response_format must force JSON so the wire dialect survives");
    const auto messages = body.value("messages").toArray();
    require(!messages.isEmpty(), "messages must not be empty");
    require(messages.first().toObject().value("role").toString() == "system",
            "first message must be the system prompt");
    require(messages.last().toObject().value("content").toString()
                .contains("four-on-the-floor hats"),
            "the producer's prompt must be in the last user turn");
}

// Scenario: the model's `content` field is what reaches parseResponse.
void minimax_backend_parses_reply_case() {
    QCoreApplication app(dummy_argc, dummy_argv);
    qputenv("BLOKKILY_MINIMAX_KEY", "test-key");
    auto* backend = makeMinimaxBackendForTesting(&app);
    std::unique_ptr<MinimaxBackend> owner(backend);
    auto* nam = new CapturingNetworkAccessManager(&app);
    // The mock reply's content is the wire-dialect JSON the parser expects,
    // nested exactly the way the OpenAI chat-completions endpoint delivers
    // it. Built as one raw string so the embedded quotes stay literal.
    nam->capture.response_body =
        "{\"choices\":[{\"message\":{\"content\":\"{\\\"mode\\\":\\\"replace\\\","
        "\\\"triggers\\\":[{\\\"step\\\":0,\\\"length_steps\\\":1,"
        "\\\"note\\\":{\\\"key\\\":60,\\\"velocity\\\":0.8}}]}\"}}]}";
    setMinimaxBackendNetworkForTesting(backend, nam);

    Reply reply;
    runBackend(backend, sampleRequest(), reply);
    require(reply.ok, "reply must succeed: " + reply.error.toStdString());

    const auto parsed = parseResponse(reply.text, tps, steps, key_limit);
    require(parsed.ok, "the reply must parse: " + parsed.error.toStdString());
    require(parsed.response.triggers.size() == 1, "one trigger expected");
    require(as_note(parsed.response.triggers[0]).key == 60,
            "the model-chosen key must reach the parser");
}

// Scenario: no key means a readable error, never a crash and never silence.
void minimax_backend_missing_key_errors_case() {
    QCoreApplication app(dummy_argc, dummy_argv);
    qunsetenv("BLOKKILY_MINIMAX_KEY");
    auto backend = makeMinimaxBackend();
    require(backend->displayName().contains("MiniMax"),
            "the picker must label this backend as MiniMax");
    Reply reply;
    runBackend(backend.get(), sampleRequest(), reply);
    require(!reply.ok, "no key must mean no success");
    require(reply.error.contains("BLOKKILY_MINIMAX_KEY"),
            "the error must name the env var to set");
}

// Scenario: a transport-level failure surfaces with the network's reason.
void minimax_backend_network_error_is_readable_case() {
    QCoreApplication app(dummy_argc, dummy_argv);
    qputenv("BLOKKILY_MINIMAX_KEY", "test-key");
    auto* backend = makeMinimaxBackendForTesting(&app);
    std::unique_ptr<MinimaxBackend> owner(backend);
    auto* nam = new CapturingNetworkAccessManager(&app);
    nam->capture.error = QNetworkReply::ConnectionRefusedError;
    setMinimaxBackendNetworkForTesting(backend, nam);

    Reply reply;
    runBackend(backend, sampleRequest(), reply);
    require(!reply.ok, "a refused connection must not succeed");
    require(reply.error.contains("MiniMax did not answer"),
            "the error must say MiniMax was the one that didn't answer");
}

// Scenario: the provider's JSON `error` field is shown to the producer.
void minimax_backend_server_error_is_readable_case() {
    QCoreApplication app(dummy_argc, dummy_argv);
    qputenv("BLOKKILY_MINIMAX_KEY", "test-key");
    auto* backend = makeMinimaxBackendForTesting(&app);
    std::unique_ptr<MinimaxBackend> owner(backend);
    auto* nam = new CapturingNetworkAccessManager(&app);
    nam->capture.response_body =
        R"({"error":{"message":"insufficient credits","type":"billing_error"}})";
    setMinimaxBackendNetworkForTesting(backend, nam);

    Reply reply;
    runBackend(backend, sampleRequest(), reply);
    require(!reply.ok, "an error envelope must not succeed");
    require(reply.error.contains("insufficient credits"),
            "the provider's message must reach the producer: " +
                reply.error.toStdString());
}

// Scenario: BLOKKILY_MINIMAX_URL / _MODEL override the defaults.
void minimax_backend_uses_custom_url_and_model_case() {
    QCoreApplication app(dummy_argc, dummy_argv);
    qputenv("BLOKKILY_MINIMAX_KEY", "test-key");
    qputenv("BLOKKILY_MINIMAX_URL", "https://example.test/minimax");
    qputenv("BLOKKILY_MINIMAX_MODEL", "minimax-test-1");
    auto* backend = makeMinimaxBackendForTesting(&app);
    std::unique_ptr<MinimaxBackend> owner(backend);
    auto* nam = new CapturingNetworkAccessManager(&app);
    nam->capture.response_body =
        R"({"choices":[{"message":{"content":"{\"mode\":\"replace\",\"triggers\":[]}"}}]})";
    setMinimaxBackendNetworkForTesting(backend, nam);

    Reply reply;
    runBackend(backend, sampleRequest(), reply);
    require(reply.ok, "reply must succeed: " + reply.error.toStdString());
    require(nam->capture.request.url().toString() ==
                "https://example.test/minimax/v1/chat/completions",
            "custom URL must be honoured");
    const auto body = QJsonDocument::fromJson(nam->capture.body).object();
    require(body.value("model").toString() == "minimax-test-1",
            "custom model must be honoured");
    qunsetenv("BLOKKILY_MINIMAX_URL");
    qunsetenv("BLOKKILY_MINIMAX_MODEL");
}

// Scenario: the ChatGPT backend builds the request shape OpenAI's chat
// endpoint expects when the key is present.
void openai_backend_builds_request_case() {
    QCoreApplication app(dummy_argc, dummy_argv);
    qputenv("BLOKKILY_OPENAI_KEY", "test-key");
    qunsetenv("BLOKKILY_OPENAI_URL");
    qunsetenv("BLOKKILY_OPENAI_MODEL");
    auto* backend = makeOpenAiBackendForTesting(&app);
    std::unique_ptr<OpenAiBackend> owner(backend);
    auto* nam = new CapturingNetworkAccessManager(&app);
    nam->capture.response_body =
        R"({"choices":[{"message":{"content":"{\"mode\":\"replace\",\"triggers\":[]}"}}]})";
    setOpenAiBackendNetworkForTesting(backend, nam);

    Reply reply;
    runBackend(backend, sampleRequest(), reply);
    require(reply.ok, "a happy-path reply must succeed: " + reply.error.toStdString());

    require(nam->capture.request.url().toString() ==
                "https://api.openai.com/v1/chat/completions",
            "request must hit the default chat completions endpoint");
    require(nam->capture.request.rawHeader("Authorization") ==
                QByteArray("Bearer test-key"),
            "Authorization must be a Bearer of the key");

    const auto body = QJsonDocument::fromJson(nam->capture.body).object();
    require(body.value("model").toString() == "gpt-4o-mini",
            "default model id must be sent");
    require(body.value("response_format").toObject().value("type").toString() ==
                "json_object",
            "response_format must force JSON so the wire dialect survives");
    const auto messages = body.value("messages").toArray();
    require(!messages.isEmpty(), "messages must not be empty");
    require(messages.first().toObject().value("role").toString() == "system",
            "first message must be the system prompt");
}

// Scenario: no ChatGPT key means a readable error, never a crash and
// never silence.
void openai_backend_missing_key_errors_case() {
    QCoreApplication app(dummy_argc, dummy_argv);
    qunsetenv("BLOKKILY_OPENAI_KEY");
    auto backend = makeOpenAiBackend();
    require(backend->displayName().contains("ChatGPT"),
            "the picker must label this backend as ChatGPT");
    Reply reply;
    runBackend(backend.get(), sampleRequest(), reply);
    require(!reply.ok, "no key must mean no success");
    require(reply.error.contains("BLOKKILY_OPENAI_KEY"),
            "the error must name the env var to set");
}

// Scenario: a ChatGPT transport-level failure surfaces with the network's
// reason.
void openai_backend_network_error_is_readable_case() {
    QCoreApplication app(dummy_argc, dummy_argv);
    qputenv("BLOKKILY_OPENAI_KEY", "test-key");
    auto* backend = makeOpenAiBackendForTesting(&app);
    std::unique_ptr<OpenAiBackend> owner(backend);
    auto* nam = new CapturingNetworkAccessManager(&app);
    nam->capture.error = QNetworkReply::ConnectionRefusedError;
    setOpenAiBackendNetworkForTesting(backend, nam);

    Reply reply;
    runBackend(backend, sampleRequest(), reply);
    require(!reply.ok, "a refused connection must not succeed");
    require(reply.error.contains("ChatGPT did not answer"),
            "the error must say ChatGPT was the one that didn't answer");
}

// Scenario: the OpenAI JSON `error` field is shown to the producer.
void openai_backend_server_error_is_readable_case() {
    QCoreApplication app(dummy_argc, dummy_argv);
    qputenv("BLOKKILY_OPENAI_KEY", "test-key");
    auto* backend = makeOpenAiBackendForTesting(&app);
    std::unique_ptr<OpenAiBackend> owner(backend);
    auto* nam = new CapturingNetworkAccessManager(&app);
    nam->capture.response_body =
        R"({"error":{"message":"Incorrect API key provided","type":"invalid_request_error"}})";
    setOpenAiBackendNetworkForTesting(backend, nam);

    Reply reply;
    runBackend(backend, sampleRequest(), reply);
    require(!reply.ok, "an error envelope must not succeed");
    require(reply.error.contains("Incorrect API key provided"),
            "the provider's message must reach the producer: " +
                reply.error.toStdString());
}

// Scenario: the OpenRouter backend builds the request shape the OpenRouter
// chat endpoint expects when the key is present.
void openrouter_backend_builds_request_case() {
    QCoreApplication app(dummy_argc, dummy_argv);
    qputenv("BLOKKILY_OPENROUTER_KEY", "test-key");
    qunsetenv("BLOKKILY_OPENROUTER_URL");
    qunsetenv("BLOKKILY_OPENROUTER_MODEL");
    auto* backend = makeOpenRouterBackendForTesting(&app);
    std::unique_ptr<OpenRouterBackend> owner(backend);
    auto* nam = new CapturingNetworkAccessManager(&app);
    nam->capture.response_body =
        R"({"choices":[{"message":{"content":"{\"mode\":\"replace\",\"triggers\":[]}"}}]})";
    setOpenRouterBackendNetworkForTesting(backend, nam);

    Reply reply;
    runBackend(backend, sampleRequest(), reply);
    require(reply.ok, "a happy-path reply must succeed: " + reply.error.toStdString());

    require(nam->capture.request.url().toString() ==
                "https://openrouter.ai/api/v1/chat/completions",
            "request must hit the default OpenRouter endpoint");
    require(nam->capture.request.rawHeader("Authorization") ==
                QByteArray("Bearer test-key"),
            "Authorization must be a Bearer of the key");

    const auto body = QJsonDocument::fromJson(nam->capture.body).object();
    require(body.value("model").toString() == "openai/gpt-4o-mini",
            "default model id must be sent");
    require(body.value("response_format").toObject().value("type").toString() ==
                "json_object",
            "response_format must force JSON so the wire dialect survives");
}

// Scenario: no OpenRouter key means a readable error, never a crash and
// never silence.
void openrouter_backend_missing_key_errors_case() {
    QCoreApplication app(dummy_argc, dummy_argv);
    qunsetenv("BLOKKILY_OPENROUTER_KEY");
    auto backend = makeOpenRouterBackend();
    require(backend->displayName().contains("OpenRouter"),
            "the picker must label this backend as OpenRouter");
    Reply reply;
    runBackend(backend.get(), sampleRequest(), reply);
    require(!reply.ok, "no key must mean no success");
    require(reply.error.contains("BLOKKILY_OPENROUTER_KEY"),
            "the error must name the env var to set");
}

// Scenario: an OpenRouter transport-level failure surfaces with the
// network's reason.
void openrouter_backend_network_error_is_readable_case() {
    QCoreApplication app(dummy_argc, dummy_argv);
    qputenv("BLOKKILY_OPENROUTER_KEY", "test-key");
    auto* backend = makeOpenRouterBackendForTesting(&app);
    std::unique_ptr<OpenRouterBackend> owner(backend);
    auto* nam = new CapturingNetworkAccessManager(&app);
    nam->capture.error = QNetworkReply::ConnectionRefusedError;
    setOpenRouterBackendNetworkForTesting(backend, nam);

    Reply reply;
    runBackend(backend, sampleRequest(), reply);
    require(!reply.ok, "a refused connection must not succeed");
    require(reply.error.contains("OpenRouter did not answer"),
            "the error must say OpenRouter was the one that didn't answer");
}

// Scenario: the OpenRouter JSON `error` field is shown to the producer.
void openrouter_backend_server_error_is_readable_case() {
    QCoreApplication app(dummy_argc, dummy_argv);
    qputenv("BLOKKILY_OPENROUTER_KEY", "test-key");
    auto* backend = makeOpenRouterBackendForTesting(&app);
    std::unique_ptr<OpenRouterBackend> owner(backend);
    auto* nam = new CapturingNetworkAccessManager(&app);
    nam->capture.response_body =
        R"({"error":{"message":"No cookie auth credentials found","code":401}})";
    setOpenRouterBackendNetworkForTesting(backend, nam);

    Reply reply;
    runBackend(backend, sampleRequest(), reply);
    require(!reply.ok, "an error envelope must not succeed");
    require(reply.error.contains("No cookie auth credentials found"),
            "the provider's message must reach the producer: " +
                reply.error.toStdString());
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
        else if (name == "minimax_backend_builds_request") minimax_backend_builds_request_case();
        else if (name == "minimax_backend_parses_reply") minimax_backend_parses_reply_case();
        else if (name == "minimax_backend_missing_key_errors") minimax_backend_missing_key_errors_case();
        else if (name == "minimax_backend_network_error_is_readable") minimax_backend_network_error_is_readable_case();
        else if (name == "minimax_backend_server_error_is_readable") minimax_backend_server_error_is_readable_case();
        else if (name == "minimax_backend_uses_custom_url_and_model") minimax_backend_uses_custom_url_and_model_case();
        else if (name == "openai_backend_builds_request") openai_backend_builds_request_case();
        else if (name == "openai_backend_missing_key_errors") openai_backend_missing_key_errors_case();
        else if (name == "openai_backend_network_error_is_readable") openai_backend_network_error_is_readable_case();
        else if (name == "openai_backend_server_error_is_readable") openai_backend_server_error_is_readable_case();
        else if (name == "openrouter_backend_builds_request") openrouter_backend_builds_request_case();
        else if (name == "openrouter_backend_missing_key_errors") openrouter_backend_missing_key_errors_case();
        else if (name == "openrouter_backend_network_error_is_readable") openrouter_backend_network_error_is_readable_case();
        else if (name == "openrouter_backend_server_error_is_readable") openrouter_backend_server_error_is_readable_case();
        else throw std::runtime_error("unknown case " + name);
        std::cout << name << " passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
}
