// Every backend that speaks the OpenAI chat-completions dialect — MiniMax,
// ChatGPT and OpenRouter — is this one class with a different description:
// a label for the picker and error text, the environment variables it reads,
// and the defaults it falls back on. The HTTP and JSON plumbing lives here
// once so the vendors cannot drift apart.
//
// The request is Authorization: Bearer <key>, POST <url>/v1/chat/completions,
// with the conversation as chat turns and response_format json_object so
// the reply stays in the wire dialect trigger_json.cpp reads. The answer is
// choices[0].message.content. A backend without a key stays in the picker
// and says which variable to set, rather than vanishing.

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

struct Vendor {
    QString label;       // what the picker and the error text call it
    const char* keyVar;
    const char* urlVar;
    QString defaultUrl;  // the API root; /v1/chat/completions follows it
    const char* modelVar;
    QString defaultModel;
};

QString envOr(const char* name, const QString& fallback) {
    const auto value = qEnvironmentVariable(name);
    return value.isEmpty() ? fallback : value;
}

class OpenAiCompatibleBackend final : public LlmBackend {
public:
    OpenAiCompatibleBackend(Vendor vendor, QObject* parent)
        : LlmBackend(parent), vendor_(std::move(vendor)),
          base_(envOr(vendor_.urlVar, vendor_.defaultUrl)),
          model_(envOr(vendor_.modelVar, vendor_.defaultModel)),
          key_(qEnvironmentVariable(vendor_.keyVar)) {
        while (base_.endsWith('/')) base_.chop(1);
    }

    void complete(const Request& request, Completion completion) override {
        if (key_.isEmpty()) {
            Reply answer;
            answer.error = QStringLiteral("No %1 API key: set %2 and ask again, or "
                                          "switch to the local Ollama backend.")
                               .arg(vendor_.label, QLatin1String(vendor_.keyVar));
            completion(answer);
            return;
        }

        // The earlier turns as chat history, then the session and the new
        // line as the latest user turn — each said exactly once.
        QJsonArray messages{QJsonObject{{"role", "system"}, {"content", request.systemPrompt}}};
        for (const auto& turn : request.history) {
            messages.append(QJsonObject{{"role", "user"}, {"content", turn.prompt}});
            messages.append(QJsonObject{
                {"role", "assistant"},
                {"content", QStringLiteral("(applied: %1)").arg(turn.summary)}});
        }
        messages.append(QJsonObject{
            {"role", "user"},
            {"content", QStringLiteral("Current session context (JSON):\n%1\n\n%2")
                            .arg(request.context, request.prompt)}});
        const QJsonObject body{
            {"model", model_},
            {"messages", messages},
            {"temperature", 0.4},
            {"response_format", QJsonObject{{"type", "json_object"}}},
        };

        QNetworkRequest netRequest(QUrl(base_ + QStringLiteral("/v1/chat/completions")));
        netRequest.setHeader(QNetworkRequest::ContentTypeHeader,
                             QStringLiteral("application/json"));
        netRequest.setRawHeader("Authorization", "Bearer " + key_.toUtf8());
        auto* reply = network()->post(netRequest, QJsonDocument(body).toJson());
        connect(reply, &QNetworkReply::finished, this,
                [reply, label = vendor_.label, completion = std::move(completion)] {
                    reply->deleteLater();
                    const auto bytes = reply->readAll();
                    Reply answer;
                    // Providers explain a refusal (bad key, no credit, unknown
                    // model) in the body of a 4xx answer; that beats Qt's
                    // generic transport reason.
                    if (const auto said = providerError(bytes); !said.isEmpty()) {
                        answer.error = QStringLiteral("%1 error: %2").arg(label, said);
                    } else if (reply->error() != QNetworkReply::NoError) {
                        answer.error = QStringLiteral("%1 did not answer: %2")
                                           .arg(label, reply->errorString());
                    } else {
                        const auto choices =
                            QJsonDocument::fromJson(bytes).object().value("choices").toArray();
                        answer.text = choices.first()
                                          .toObject()
                                          .value("message")
                                          .toObject()
                                          .value("content")
                                          .toString();
                        answer.ok = !answer.text.isEmpty();
                        if (!answer.ok)
                            answer.error = QStringLiteral("%1 answered with no content").arg(label);
                    }
                    completion(answer);
                });
    }

    [[nodiscard]] QString displayName() const override {
        return QStringLiteral("%1 (%2)").arg(vendor_.label, model_);
    }

private:
    Vendor vendor_;
    QString base_;
    QString model_;
    QString key_;
};

} // namespace

std::unique_ptr<LlmBackend> makeMinimaxBackend(QObject* parent) {
    return std::make_unique<OpenAiCompatibleBackend>(
        Vendor{QStringLiteral("MiniMax"), "BLOKKILY_MINIMAX_KEY", "BLOKKILY_MINIMAX_URL",
               QStringLiteral("https://api.minimax.io"), "BLOKKILY_MINIMAX_MODEL",
               QStringLiteral("MiniMax-M3")},
        parent);
}

std::unique_ptr<LlmBackend> makeOpenAiBackend(QObject* parent) {
    return std::make_unique<OpenAiCompatibleBackend>(
        Vendor{QStringLiteral("ChatGPT"), "BLOKKILY_OPENAI_KEY", "BLOKKILY_OPENAI_URL",
               QStringLiteral("https://api.openai.com"), "BLOKKILY_OPENAI_MODEL",
               QStringLiteral("gpt-4o-mini")},
        parent);
}

std::unique_ptr<LlmBackend> makeOpenRouterBackend(QObject* parent) {
    return std::make_unique<OpenAiCompatibleBackend>(
        Vendor{QStringLiteral("OpenRouter"), "BLOKKILY_OPENROUTER_KEY",
               "BLOKKILY_OPENROUTER_URL", QStringLiteral("https://openrouter.ai/api"),
               "BLOKKILY_OPENROUTER_MODEL", QStringLiteral("openai/gpt-4o-mini")},
        parent);
}

} // namespace blokkily::llm
