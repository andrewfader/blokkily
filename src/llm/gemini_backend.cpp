// The cloud backend: Google's Gemini API over HTTPS, chosen when a producer
// wants a stronger model and is comfortable sending the session's context to
// it. The key comes from BLOKKILY_GEMINI_KEY; without one the backend stays
// selectable and says why it cannot answer, rather than vanishing from the
// picker and leaving the producer wondering where it went.

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

QJsonObject textPart(const QString& text) {
    return QJsonObject{{"text", text}};
}

class GeminiBackend final : public LlmBackend {
public:
    explicit GeminiBackend(QObject* parent = nullptr) : LlmBackend(parent) {
        key_ = qEnvironmentVariable("BLOKKILY_GEMINI_KEY");
        const auto model = qEnvironmentVariable("BLOKKILY_GEMINI_MODEL");
        model_ = model.isEmpty() ? QStringLiteral("gemini-2.5-flash") : model;
    }

    void complete(const Request& request, Completion completion) override {
        if (key_.isEmpty()) {
            Reply answer;
            answer.error = QStringLiteral(
                "No Gemini API key: set BLOKKILY_GEMINI_KEY and ask again, or switch "
                "to the local Ollama backend.");
            completion(answer);
            return;
        }
        QJsonArray contents;
        for (const auto& turn : request.history) {
            contents.append(QJsonObject{{"role", "user"},
                                        {"parts", QJsonArray{textPart(turn.prompt)}}});
            contents.append(QJsonObject{
                {"role", "model"},
                {"parts", QJsonArray{textPart(QStringLiteral("(applied: %1)").arg(turn.summary))}}});
        }
        contents.append(QJsonObject{
            {"role", "user"},
            {"parts", QJsonArray{textPart(QStringLiteral("Current session context (JSON):\n%1\n\n%2")
                                              .arg(request.context, request.prompt))}}});
        QJsonObject body{
            {"system_instruction",
             QJsonObject{{"parts", QJsonArray{textPart(request.systemPrompt)}}}},
            {"contents", contents},
            {"generationConfig",
             QJsonObject{{"response_mime_type", "application/json"}}},
        };
        // The key travels in a header, not the query string, so it never
        // lands in a URL that a proxy or a log might keep.
        QNetworkRequest netRequest(QUrl(QStringLiteral(
            "https://generativelanguage.googleapis.com/v1beta/models/%1:generateContent")
                                            .arg(model_)));
        netRequest.setHeader(QNetworkRequest::ContentTypeHeader,
                             QStringLiteral("application/json"));
        netRequest.setRawHeader("x-goog-api-key", key_.toUtf8());
        auto* reply = network()->post(netRequest, QJsonDocument(body).toJson());
        connect(reply, &QNetworkReply::finished, this, [reply, completion = std::move(completion)] {
            reply->deleteLater();
            const auto bytes = reply->readAll();
            Reply answer;
            if (const auto said = providerError(bytes); !said.isEmpty()) {
                answer.error = QStringLiteral("Gemini error: %1").arg(said);
            } else if (reply->error() != QNetworkReply::NoError) {
                answer.error = QStringLiteral("Gemini did not answer: %1")
                                   .arg(reply->errorString());
            } else {
                const auto document = QJsonDocument::fromJson(bytes);
                const auto candidates = document.object().value("candidates").toArray();
                if (!candidates.isEmpty()) {
                    const auto parts = candidates.first()
                                           .toObject()
                                           .value("content")
                                           .toObject()
                                           .value("parts")
                                           .toArray();
                    for (const auto& part : parts)
                        answer.text += part.toObject().value("text").toString();
                }
                answer.ok = !answer.text.isEmpty();
                if (!answer.ok)
                    answer.error = QStringLiteral("Gemini answered with no content");
            }
            completion(answer);
        });
    }

    [[nodiscard]] QString displayName() const override {
        return QStringLiteral("Gemini (%1)").arg(model_);
    }

private:
    QString key_;
    QString model_;
};

} // namespace

std::unique_ptr<LlmBackend> makeGeminiBackend(QObject* parent) {
    return std::make_unique<GeminiBackend>(parent);
}

} // namespace blokkily::llm
