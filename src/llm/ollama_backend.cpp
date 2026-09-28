// The local backend: one HTTP POST to an Ollama server (default
// localhost:11434), no key, no account, nothing leaving the machine. This is
// the default backend because a producer's session should not be sent to a
// cloud unless they ask for that (llm-integration-design.md, "Privacy").

#include "llm_backend.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace blokkily::llm {
namespace {

QString conversationText(const Request& request) {
    QString text;
    for (const auto& turn : request.history) {
        text += QStringLiteral("Producer: %1\nAssistant applied: %2\n\n")
                    .arg(turn.prompt, turn.summary);
    }
    text += QStringLiteral("Current session context (JSON):\n%1\n\nProducer: %2\n")
                .arg(request.context, request.prompt);
    return text;
}

class OllamaBackend final : public LlmBackend {
public:
    explicit OllamaBackend(QObject* parent = nullptr)
        : LlmBackend(parent), network_(new QNetworkAccessManager(this)) {
        const auto url = qEnvironmentVariable("BLOKKILY_OLLAMA_URL");
        base_ = url.isEmpty() ? QStringLiteral("http://localhost:11434") : url;
        const auto model = qEnvironmentVariable("BLOKKILY_OLLAMA_MODEL");
        model_ = model.isEmpty() ? QStringLiteral("llama3.1") : model;
    }

    void complete(const Request& request, Completion completion) override {
        QJsonObject body{
            {"model", model_},
            {"system", request.systemPrompt},
            {"prompt", conversationText(request)},
            {"stream", false},
            {"format", "json"},
        };
        QNetworkRequest netRequest(QUrl(base_ + QStringLiteral("/api/generate")));
        netRequest.setHeader(QNetworkRequest::ContentTypeHeader,
                             QStringLiteral("application/json"));
        auto* reply = network_->post(netRequest, QJsonDocument(body).toJson());
        connect(reply, &QNetworkReply::finished, this, [reply, completion = std::move(completion)] {
            reply->deleteLater();
            Reply answer;
            if (reply->error() != QNetworkReply::NoError) {
                answer.error = QStringLiteral("Ollama did not answer: %1 (is `ollama serve` "
                                              "running?)")
                                   .arg(reply->errorString());
            } else {
                const auto document = QJsonDocument::fromJson(reply->readAll());
                answer.text = document.object().value("response").toString();
                answer.ok = !answer.text.isEmpty();
                if (!answer.ok)
                    answer.error = QStringLiteral("Ollama answered with an empty response");
            }
            completion(answer);
        });
    }

    [[nodiscard]] QString displayName() const override {
        return QStringLiteral("Ollama (%1)").arg(model_);
    }

private:
    QNetworkAccessManager* network_;
    QString base_;
    QString model_;
};

} // namespace

std::unique_ptr<LlmBackend> makeOllamaBackend(QObject* parent) {
    return std::make_unique<OllamaBackend>(parent);
}

} // namespace blokkily::llm
