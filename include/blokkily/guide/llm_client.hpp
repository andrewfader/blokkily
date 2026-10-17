#pragma once

// Optional LLM help for the guide: free-form questions ("why does this chord
// sound sad?") and richer narration. Speaks the OpenAI-compatible
// /chat/completions dialect — the same dialect the upstream llm backends use,
// so this can delegate to that registry on rebase.
//
// Disabled by default: enabled() is false until the producer configures an
// endpoint and an API key in the app's settings (the app's own config, never
// the repo, never chat). The HTTP transport is injected, so tests use a fake
// and never touch the network. Short timeouts are the transport's job;
// any transport failure falls back to the rule-based narrator.

#include <functional>
#include <string>

namespace blokkily::guide {

class LlmClient {
public:
    // request_json is the POST body; the callback gets (response_json, error).
    // Either response_json or error is set, never both.
    using Transport =
        std::function<void(const std::string& request_json,
                           std::function<void(const std::string& response_json,
                                              const std::string& error)>)>;

    void configure(const std::string& endpoint, const std::string& api_key,
                   const std::string& model = "gpt-4o-mini");
    void clear(); // back to disabled
    [[nodiscard]] bool enabled() const { return !endpoint_.empty() && !api_key_.empty(); }

    void set_transport(Transport transport) { transport_ = std::move(transport); }

    // Asks the model; answer arrives on answer(), error on error().
    // The callback always fires exactly once per ask() when enabled.
    void ask(const std::string& question, const std::string& session_context,
             std::function<void(const std::string& answer)> answer,
             std::function<void(const std::string& error)> error);

    // Builds the chat/completions body (also what the tests assert on).
    [[nodiscard]] std::string build_request(const std::string& question,
                                            const std::string& session_context) const;
    // Extracts choices[0].message.content; "" when absent.
    [[nodiscard]] static std::string parse_answer(const std::string& response_json);

private:
    std::string endpoint_;
    std::string api_key_;
    std::string model_ = "gpt-4o-mini";
    Transport transport_;

    static std::string escape(const std::string& s);
};

} // namespace blokkily::guide
