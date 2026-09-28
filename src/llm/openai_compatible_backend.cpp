// The shared implementation for every backend that talks the OpenAI
// chat-completions dialect: MiniMax, ChatGPT, OpenRouter, and any future
// vendor that adopts the same JSON shape. The subclasses only describe
// who they are and which env vars to read — every line of HTTP and JSON
// plumbing lives here so it stays consistent across providers.
//
// The key, when set, is sent as a Bearer token; the response body is
// parsed for `choices[0].message.content`; the provider's `error.message`
// is shown verbatim when present; transport failures carry the network's
// reason. A backend without a key stays selectable in the picker (matching
// the Gemini shape — AGENTS.md / docs/llm-assistant.md: backends must
// not silently disappear).

#include "openai_compatible_backend.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace blokkily::llm {

namespace {

QString chatText(const Request& request) {
    // Flatten the conversation into one readable block; the system prompt
    // already carries the schema so we keep the user turn as prose.
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

OpenAiCompatibleBackend::OpenAiCompatibleBackend(Config config, QObject* parent)
    : LlmBackend(parent),
      config_(std::move(config)),
      network_(new QNetworkAccessManager(this)) {
    if (!config_.urlEnvVar.isEmpty()) {
        const auto url = qEnvironmentVariable(config_.urlEnvVar.toUtf8().constData());
        base_ = url.isEmpty() ? config_.defaultUrl : url;
    } else {
        base_ = config_.defaultUrl;
    }
    while (base_.endsWith('/')) base_.chop(1);

    if (!config_.modelEnvVar.isEmpty()) {
        const auto model = qEnvironmentVariable(config_.modelEnvVar.toUtf8().constData());
        model_ = model.isEmpty() ? config_.defaultModel : model;
    } else {
        model_ = config_.defaultModel;
    }

    key_ = qEnvironmentVariable(config_.keyEnvVar.toUtf8().constData());
}

void OpenAiCompatibleBackend::complete(const Request& request, Completion completion) {
    if (key_.isEmpty()) {
        Reply answer;
        answer.error = QStringLiteral(
            "No %1 API key: set %2 and ask again, or switch "
            "to the local Ollama backend.")
            .arg(config_.label, config_.keyEnvVar);
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

    QNetworkRequest netRequest(QUrl(base_ + config_.chatPath));
    netRequest.setHeader(QNetworkRequest::ContentTypeHeader,
                         QStringLiteral("application/json"));
    netRequest.setRawHeader("Authorization",
                            QByteArray("Bearer ") + key_.toUtf8());
    auto* reply = network_->post(netRequest, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this,
            [reply, label = config_.label, completion = std::move(completion)] {
                reply->deleteLater();
                Reply answer;
                if (reply->error() != QNetworkReply::NoError) {
                    answer.error = QStringLiteral("%1 did not answer: %2")
                                       .arg(label, reply->errorString());
                    completion(answer);
                    return;
                }
                const auto document = QJsonDocument::fromJson(reply->readAll());
                const auto root = document.object();
                if (const auto error = root.value("error").toObject();
                    !error.isEmpty()) {
                    answer.error = QStringLiteral("%1 error: %2")
                                       .arg(label, error.value("message").toString());
                    completion(answer);
                    return;
                }
                const auto choices = root.value("choices").toArray();
                if (choices.isEmpty()) {
                    answer.error = QStringLiteral(
                        "%1 answered with no choices").arg(label);
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
                        "%1 answered with empty content").arg(label);
                completion(answer);
            });
}

QString OpenAiCompatibleBackend::displayName() const {
    return QStringLiteral("%1 (%2)").arg(config_.label, model_);
}

void OpenAiCompatibleBackend::setNetworkForTesting(QNetworkAccessManager* nam) {
    if (network_ != nam) {
        if (network_) network_->deleteLater();
        network_ = nam;
        network_->setParent(this);
    }
}

OpenAiCompatibleBackend* makeOpenAiCompatibleBackendForTesting(
    OpenAiCompatibleBackend::Config config, QObject* parent) {
    return new OpenAiCompatibleBackend(std::move(config), parent);
}

void setOpenAiCompatibleBackendNetworkForTesting(OpenAiCompatibleBackend* backend,
                                                 QNetworkAccessManager* nam) {
    backend->setNetworkForTesting(nam);
}

} // namespace blokkily::llm
