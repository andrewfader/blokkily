// The LlmBackend abstract QObject base. Provides the vtable and Q_OBJECT
// linkage that every concrete backend relies on; the test binary otherwise
// cannot link (undefined vtable / staticMetaObject for LlmBackend).
// Also the registry: the names the picker offers, and the factory that
// turns a name into a concrete backend.

#include "llm_backend.hpp"

namespace blokkily::llm {

LlmBackend::~LlmBackend() = default;

// Forward declarations so this translation unit does not have to include
// the network-bearing backend sources just to expose their factories.
std::unique_ptr<LlmBackend> makeOllamaBackend(QObject* parent);
std::unique_ptr<LlmBackend> makeGeminiBackend(QObject* parent);
std::unique_ptr<LlmBackend> makeMinimaxBackend(QObject* parent);

QStringList backendNames() {
    return {QStringLiteral("ollama"), QStringLiteral("gemini"),
            QStringLiteral("minimax")};
}

std::unique_ptr<LlmBackend> makeBackend(const QString& name, QObject* parent) {
    if (name == QLatin1String("ollama")) return makeOllamaBackend(parent);
    if (name == QLatin1String("gemini")) return makeGeminiBackend(parent);
    if (name == QLatin1String("minimax")) return makeMinimaxBackend(parent);
    return nullptr;
}

} // namespace blokkily::llm
