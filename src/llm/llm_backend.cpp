// The LlmBackend base: the network seam every live backend shares, the
// provider-error reader, and the registry — the names the picker offers and
// the factory that turns a name into a concrete backend.

#include "llm_backend.hpp"

#include "http_cassette.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>

namespace blokkily::llm {

LlmBackend::~LlmBackend() = default;

void LlmBackend::setNetworkForTesting(QNetworkAccessManager* network) {
    if (network_ == network) return;
    delete network_;
    network_ = network;
    network_->setParent(this);
}

QNetworkAccessManager* LlmBackend::network() {
    // BLOKKILY_LLM_CASSETTE puts a recording or replaying network in place
    // of the live one (http_cassette.hpp).
    if (network_ == nullptr) network_ = cassetteFromEnvironment(this);
    if (network_ == nullptr) network_ = new QNetworkAccessManager(this);
    return network_;
}

QString providerError(const QByteArray& body) {
    const auto error = QJsonDocument::fromJson(body).object().value("error");
    if (error.isString()) return error.toString();
    return error.toObject().value("message").toString();
}

QStringList backendNames() {
    return {QStringLiteral("ollama"), QStringLiteral("gemini"),
            QStringLiteral("minimax"), QStringLiteral("chatgpt"),
            QStringLiteral("openrouter")};
}

std::unique_ptr<LlmBackend> makeBackend(const QString& name, QObject* parent) {
    if (name == QLatin1String("ollama")) return makeOllamaBackend(parent);
    if (name == QLatin1String("gemini")) return makeGeminiBackend(parent);
    if (name == QLatin1String("minimax")) return makeMinimaxBackend(parent);
    if (name == QLatin1String("chatgpt")) return makeOpenAiBackend(parent);
    if (name == QLatin1String("openrouter")) return makeOpenRouterBackend(parent);
    return nullptr;
}

} // namespace blokkily::llm
