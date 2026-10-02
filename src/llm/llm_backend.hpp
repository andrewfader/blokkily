#pragma once

// The LLM backend seam. A backend answers one request — a system prompt, the
// session's context, the conversation so far and the producer's latest line —
// with one reply. Everything network-shaped lives behind this interface, so
// the verification gate can stand a scripted backend in for a live one and
// exercise the whole path without touching a network (AGENTS.md: tests must
// not depend on a network connection).

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <vector>

class QNetworkAccessManager;

namespace blokkily::llm {

struct Turn {
    QString prompt;    // what the producer said
    QString summary;   // what the application did with the reply
};

struct Request {
    QString systemPrompt;
    QString context;               // the session, as a JSON object in text
    std::vector<Turn> history;     // oldest first
    QString prompt;
};

struct Reply {
    bool ok = false;
    QString text;    // the model's raw answer when ok
    QString error;   // a readable reason when not
};

using Completion = std::function<void(Reply)>;

class LlmBackend : public QObject {
    Q_OBJECT
public:
    explicit LlmBackend(QObject* parent = nullptr) : QObject(parent) {}
    ~LlmBackend() override;
    // Asks the model. The completion runs on this object's thread, exactly
    // once, however the answer went.
    virtual void complete(const Request& request, Completion completion) = 0;
    // What the prompt bar calls this backend.
    [[nodiscard]] virtual QString displayName() const = 0;

    // Test seam: the unit tests hand in a network access manager that
    // captures the request and answers with canned bytes, so every live
    // backend's request, reply and error path runs without a server. The
    // backend takes ownership.
    void setNetworkForTesting(QNetworkAccessManager* network);

protected:
    // The backend's network access, created on first use: the live network,
    // or the cassette BLOKKILY_LLM_CASSETTE names. The scripted backend never
    // asks for one.
    [[nodiscard]] QNetworkAccessManager* network();

private:
    QNetworkAccessManager* network_ = nullptr;
};

// The provider's own explanation in an HTTP answer's body, when it gave one:
// {"error": {"message": "..."}} (OpenAI-compatible vendors and Gemini) or
// {"error": "..."} (Ollama). Providers send it with a 4xx/5xx status, so a
// backend reads it before falling back on the transport's reason.
[[nodiscard]] QString providerError(const QByteArray& body);

[[nodiscard]] std::unique_ptr<LlmBackend> makeOllamaBackend(QObject* parent = nullptr);
[[nodiscard]] std::unique_ptr<LlmBackend> makeGeminiBackend(QObject* parent = nullptr);
[[nodiscard]] std::unique_ptr<LlmBackend> makeMinimaxBackend(QObject* parent = nullptr);
[[nodiscard]] std::unique_ptr<LlmBackend> makeOpenAiBackend(QObject* parent = nullptr);
[[nodiscard]] std::unique_ptr<LlmBackend> makeOpenRouterBackend(QObject* parent = nullptr);

// The names the backend picker offers, in the order it offers them.
[[nodiscard]] QStringList backendNames();
// Builds the backend `name` selects. "ollama", "gemini", "minimax",
// "chatgpt" and "openrouter" are live; any other name is an empty
// optional. Configuration comes from the environment:
//   BLOKKILY_OLLAMA_URL        (default http://localhost:11434)
//   BLOKKILY_OLLAMA_MODEL      (default llama3.1)
//   BLOKKILY_GEMINI_KEY        API key; the Gemini backend is offered
//                              without one but answers every request with
//                              an error
//   BLOKKILY_GEMINI_MODEL      (default gemini-2.5-flash)
//   BLOKKILY_MINIMAX_KEY       API key for the OpenAI-compatible chat
//                              endpoint at BLOKKILY_MINIMAX_URL; the
//                              backend is offered without one but answers
//                              every request with an error
//   BLOKKILY_MINIMAX_URL       (default https://api.minimax.io)
//   BLOKKILY_MINIMAX_MODEL     (default MiniMax-M3)
//   BLOKKILY_OPENAI_KEY        API key for OpenAI's chat-completions
//                              endpoint; the ChatGPT backend is offered
//                              without one but answers every request with
//                              an error
//   BLOKKILY_OPENAI_URL        (default https://api.openai.com)
//   BLOKKILY_OPENAI_MODEL      (default gpt-4o-mini)
//   BLOKKILY_OPENROUTER_KEY    API key for OpenRouter (routes to any
//                              vendor); same shape as ChatGPT
//   BLOKKILY_OPENROUTER_URL    (default https://openrouter.ai/api)
//   BLOKKILY_OPENROUTER_MODEL  (default openrouter/free: OpenRouter
//                              routes to whichever free model is up,
//                              so an unfunded key still answers)
[[nodiscard]] std::unique_ptr<LlmBackend> makeBackend(const QString& name,
                                                      QObject* parent = nullptr);

} // namespace blokkily::llm
