// Claude, through Anthropic's Messages API over HTTPS. The key comes from
// BLOKKILY_ANTHROPIC_KEY; without one the backend stays selectable and says
// which variable to set, like every other cloud backend.
//
// The request is x-api-key: <key>, anthropic-version, POST
// <url>/v1/messages, with the standing instructions as the top-level system
// prompt and the conversation as alternating user and assistant turns. The
// Messages API has no JSON switch; the system prompt already asks for the
// wire dialect and trigger_json.cpp tolerates the prose a model wraps it in.
// The answer is the concatenation of the reply's text blocks (thinking
// blocks carry no text the producer should see). A refusal arrives as a
// successful answer with stop_reason "refusal", so it is read before the
// content.

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

QString envOr(const char* name, const QString& fallback) {
    const auto value = qEnvironmentVariable(name);
    return value.isEmpty() ? fallback : value;
}

// Claude Opus 5 and later, Fable 5 and later, and Sonnet 5.5 can re-run a
// request their safety classifier declined on another model, server side;
// other models reject the parameter, so it is sent only to these.
bool takesFallbacks(const QString& model) {
    return model.startsWith(QLatin1String("claude-opus-5")) ||
           model.startsWith(QLatin1String("claude-fable-5")) ||
           model.startsWith(QLatin1String("claude-sonnet-5-5"));
}

// The current generation takes an effort level. Low keeps an answer inside
// the guide's thirty-second wait; the prompt bar's requests are short.
bool takesEffort(const QString& model) {
    return takesFallbacks(model) || model.startsWith(QLatin1String("claude-sonnet-5")) ||
           model.startsWith(QLatin1String("claude-haiku-5"));
}

class AnthropicBackend final : public LlmBackend {
public:
    explicit AnthropicBackend(QObject* parent)
        : LlmBackend(parent),
          base_(envOr("BLOKKILY_ANTHROPIC_URL", QStringLiteral("https://api.anthropic.com"))),
          model_(envOr("BLOKKILY_ANTHROPIC_MODEL", QStringLiteral("claude-opus-5-5"))),
          key_(qEnvironmentVariable("BLOKKILY_ANTHROPIC_KEY")) {
        while (base_.endsWith('/')) base_.chop(1);
    }

    void complete(const Request& request, Completion completion) override {
        if (key_.isEmpty()) {
            Reply answer;
            answer.error = QStringLiteral(
                "No Claude API key: set BLOKKILY_ANTHROPIC_KEY and ask again, or switch "
                "to the local Ollama backend.");
            completion(answer);
            return;
        }

        // The earlier turns, then the session and the new line as the latest
        // user turn — each said exactly once.
        QJsonArray messages;
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
        QJsonObject body{
            {"model", model_},
            {"max_tokens", 16000},
            {"system", request.systemPrompt},
            {"messages", messages},
        };
        if (takesEffort(model_)) body["output_config"] = QJsonObject{{"effort", "low"}};
        if (takesFallbacks(model_)) body["fallbacks"] = QStringLiteral("default");

        QNetworkRequest netRequest(QUrl(base_ + QStringLiteral("/v1/messages")));
        netRequest.setHeader(QNetworkRequest::ContentTypeHeader,
                             QStringLiteral("application/json"));
        netRequest.setRawHeader("x-api-key", key_.toUtf8());
        netRequest.setRawHeader("anthropic-version", "2023-06-01");
        if (takesFallbacks(model_))
            netRequest.setRawHeader("anthropic-beta", "server-side-fallback-2026-07-01");
        auto* reply = network()->post(netRequest, QJsonDocument(body).toJson());
        connect(reply, &QNetworkReply::finished, this, [reply, completion = std::move(completion)] {
            reply->deleteLater();
            const auto bytes = reply->readAll();
            Reply answer;
            if (const auto said = providerError(bytes); !said.isEmpty()) {
                answer.error = QStringLiteral("Claude error: %1").arg(said);
            } else if (reply->error() != QNetworkReply::NoError) {
                answer.error = QStringLiteral("Claude did not answer: %1")
                                   .arg(reply->errorString());
            } else {
                const auto root = QJsonDocument::fromJson(bytes).object();
                if (root.value("stop_reason").toString() == QLatin1String("refusal")) {
                    const auto why = root.value("stop_details").toObject();
                    answer.error = QStringLiteral("Claude declined the request");
                    if (const auto category = why.value("category").toString();
                        !category.isEmpty())
                        answer.error += QStringLiteral(" (%1)").arg(category);
                } else {
                    for (const auto& block : root.value("content").toArray()) {
                        const auto object = block.toObject();
                        if (object.value("type").toString() == QLatin1String("text"))
                            answer.text += object.value("text").toString();
                    }
                    answer.ok = !answer.text.isEmpty();
                    if (!answer.ok) answer.error = QStringLiteral("Claude answered with no content");
                }
            }
            completion(answer);
        });
    }

    [[nodiscard]] QString displayName() const override {
        return QStringLiteral("Claude (%1)").arg(model_);
    }

private:
    QString base_;
    QString model_;
    QString key_;
};

} // namespace

std::unique_ptr<LlmBackend> makeAnthropicBackend(QObject* parent) {
    return std::make_unique<AnthropicBackend>(parent);
}

} // namespace blokkily::llm
