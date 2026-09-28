// The cloud backend for providers that speak the OpenAI chat-completions
// dialect — MiniMax / Minimax included. We POST to
// `${BLOKKILY_MINIMAX_URL}/v1/chat/completions` with a Bearer key, ask for
// JSON output through `response_format: {type: "json_object"}`, and pull
// the model's message out of `choices[0].message.content`. The wire dialect
// the assistant already parses (`mode` + `triggers`) is unchanged: same
// input format, same output format, same parsing path.
//
// The key comes from BLOKKILY_MINIMAX_KEY; without one the backend stays
// selectable and reports why it cannot answer (matching the Gemini shape —
// AGENTS.md / docs/llm-assistant.md: backends must not silently disappear
// from the picker).
//
// The class is declared in minimax_backend.hpp so the unit test can stand
// up a backend, swap in a capturing QNetworkAccessManager, and exercise
// the whole request / reply / error path without a live server (AGENTS.md:
// tests must not depend on a network connection).

#include "minimax_backend.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace blokkily::llm {

namespace {
constexpr const char* kDefaultUrl = "https://api.minimax.io";
constexpr const char* kDefaultModel = "MiniMax-M3";

QString chatText(const Request& request) {
    // The Ollama backend flattens the request into one prompt; we keep the
    // OpenAI-shaped messages array instead so chat-tuned MiniMax models see
    // proper turn structure. The system prompt carries the schema; the user
    // turn carries context + producer's line.
    QString text;
    for (const auto& turn : request.history) {
        text += QStringLiteral("Producer: %1\nAssistant applied: %2\n\n")
                    .arg(turn.prompt, turn.summary);
    }
    text += QStringLiteral("Current session context (JSON):\n%1\n\nProducer: %2\n")
                .arg(request.context, request.prompt);
    return text;
}
} // namespace

MinimaxBackend::MinimaxBackend(QObject* parent)
    : LlmBackend(parent), network_(new QNetworkAccessManager(this)) {
    const auto url = qEnvironmentVariable("BLOKKILY_MINIMAX_URL");
    base_ = url.isEmpty() ? QString::fromLatin1(kDefaultUrl) : url;
    // Strip any trailing slash so the chat path concatenates cleanly.
    while (base_.endsWith('/')) base_.chop(1);
    const auto model = qEnvironmentVariable("BLOKKILY_MINIMAX_MODEL");
    model_ = model.isEmpty() ? QString::fromLatin1(kDefaultModel) : model;
    key_ = qEnvironmentVariable("BLOKKILY_MINIMAX_KEY");
}

void MinimaxBackend::complete(const Request& request, Completion completion) {
    if (key_.isEmpty()) {
        Reply answer;
        answer.error = QStringLiteral(
            "No MiniMax API key: set BLOKKILY_MINIMAX_KEY and ask again, or switch "
            "to the local Ollama backend.");
        completion(answer);
        return;
    }

    QJsonArray messages;
    messages.append(QJsonObject{{"role", "system"},
                                {"content", request.systemPrompt}});
    for (const auto& turn : request.history) {
        messages.append(QJsonObject{{"role", "user"},
                                    {"content", turn.prompt}});
        messages.append(QJsonObject{
            {"role", "assistant"},
            {"content", QStringLiteral("(applied: %1)").arg(turn.summary)}});
    }
    messages.append(QJsonObject{{"role", "user"},
                                {"content", chatText(request)}});

    QJsonObject body{
        {"model", model_},
        {"messages", messages},
        {"temperature", 0.4},
        // Force JSON so the existing parser path keeps working unchanged
        // — the wire dialect is JSON, and free-form prose would fail
        // llm_parse_rejects_prose.
        {"response_format", QJsonObject{{"type", "json_object"}}},
    };

    QNetworkRequest netRequest(
        QUrl(base_ + QStringLiteral("/v1/chat/completions")));
    netRequest.setHeader(QNetworkRequest::ContentTypeHeader,
                         QStringLiteral("application/json"));
    netRequest.setRawHeader("Authorization",
                            QByteArray("Bearer ") + key_.toUtf8());
    auto* reply = network_->post(netRequest, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this,
            [reply, completion = std::move(completion)] {
                reply->deleteLater();
                Reply answer;
                if (reply->error() != QNetworkReply::NoError) {
                    answer.error = QStringLiteral("MiniMax did not answer: %1")
                                       .arg(reply->errorString());
                    completion(answer);
                    return;
                }
                const auto document = QJsonDocument::fromJson(reply->readAll());
                const auto root = document.object();
                if (const auto error = root.value("error").toObject();
                    !error.isEmpty()) {
                    answer.error = QStringLiteral("MiniMax error: %1")
                                       .arg(error.value("message").toString());
                    completion(answer);
                    return;
                }
                const auto choices = root.value("choices").toArray();
                if (choices.isEmpty()) {
                    answer.error = QStringLiteral(
                        "MiniMax answered with no choices");
                    completion(answer);
                    return;
                }
                const auto message = choices.first()
                                         .toObject()
                                         .value("message")
                                         .toObject();
                answer.text = message.value("content").toString();
                answer.ok = !answer.text.isEmpty();
                if (!answer.ok)
                    answer.error = QStringLiteral(
                        "MiniMax answered with empty content");
                completion(answer);
            });
}

QString MinimaxBackend::displayName() const {
    return QStringLiteral("MiniMax (%1)").arg(model_);
}

void MinimaxBackend::setNetworkForTesting(QNetworkAccessManager* nam) {
    if (network_ != nam) {
        if (network_) network_->deleteLater();
        network_ = nam;
        network_->setParent(this);
    }
}

std::unique_ptr<LlmBackend> makeMinimaxBackend(QObject* parent) {
    return std::make_unique<MinimaxBackend>(parent);
}

MinimaxBackend* makeMinimaxBackendForTesting(QObject* parent) {
    return new MinimaxBackend(parent);
}

void setMinimaxBackendNetworkForTesting(MinimaxBackend* backend,
                                        QNetworkAccessManager* nam) {
    backend->setNetworkForTesting(nam);
}

} // namespace blokkily::llm
