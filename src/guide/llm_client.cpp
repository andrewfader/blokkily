#include "blokkily/guide/llm_client.hpp"

#include <sstream>

namespace blokkily::guide {

void LlmClient::configure(const std::string& endpoint, const std::string& api_key,
                          const std::string& model) {
    endpoint_ = endpoint;
    api_key_ = api_key;
    if (!model.empty()) model_ = model;
}

void LlmClient::clear() {
    endpoint_.clear();
    api_key_.clear();
}

std::string LlmClient::escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (const char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                // Every other control character is escaped too, or the
                // request is not JSON.
                if (static_cast<unsigned char>(c) < 0x20) {
                    static const char* hex = "0123456789abcdef";
                    out += "\\u00";
                    out += hex[(c >> 4) & 0xF];
                    out += hex[c & 0xF];
                } else {
                    out += c;
                }
        }
    }
    return out;
}

namespace {

// Four hex digits at `at`, or -1.
long hex4(const std::string& text, std::size_t at) {
    if (at + 4 > text.size()) return -1;
    long value = 0;
    for (std::size_t i = at; i < at + 4; ++i) {
        const char c = text[i];
        value <<= 4;
        if (c >= '0' && c <= '9') value |= c - '0';
        else if (c >= 'a' && c <= 'f') value |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') value |= c - 'A' + 10;
        else return -1;
    }
    return value;
}

void append_utf8(std::string& out, unsigned long cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

} // namespace

std::string LlmClient::build_request(const std::string& question,
                                     const std::string& session_context) const {
    std::ostringstream body;
    body << "{\"model\":\"" << escape(model_) << "\","
         << "\"messages\":["
         << "{\"role\":\"system\",\"content\":\""
         << escape("You are the in-app guide of Blokkily, a pattern-first music "
                   "workstation. Answer music theory and composition questions "
                   "plainly and briefly (a few sentences), grounded in the "
                   "producer's session described below. Never invent notes "
                   "that are not in the session.")
         << "\"},";
    if (!session_context.empty())
        body << "{\"role\":\"system\",\"content\":\""
             << escape("Session: " + session_context) << "\"},";
    body << "{\"role\":\"user\",\"content\":\"" << escape(question) << "\"}"
         << "],\"temperature\":0.7,\"max_tokens\":300}";
    return body.str();
}

// Minimal extraction of choices[0].message.content without a JSON library:
// finds "content" after the first "message" object and reads its string.
std::string LlmClient::parse_answer(const std::string& response_json) {
    const auto message_at = response_json.find("\"message\"");
    if (message_at == std::string::npos) return "";
    const auto content_at = response_json.find("\"content\"", message_at);
    if (content_at == std::string::npos) return "";
    const auto colon = response_json.find(':', content_at);
    if (colon == std::string::npos) return "";
    auto p = colon + 1;
    while (p < response_json.size() && response_json[p] != '"') {
        if (response_json[p] != ' ' && response_json[p] != '\t') return "";
        ++p;
    }
    if (p >= response_json.size()) return "";
    ++p;
    std::string out;
    while (p < response_json.size()) {
        const char c = response_json[p++];
        if (c == '"') return out;
        if (c == '\\' && p < response_json.size()) {
            const char e = response_json[p++];
            switch (e) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    long cp = hex4(response_json, p);
                    if (cp < 0) return "";
                    p += 4;
                    // A surrogate pair is one character.
                    if (cp >= 0xD800 && cp <= 0xDBFF && p + 6 <= response_json.size() &&
                        response_json[p] == '\\' && response_json[p + 1] == 'u') {
                        const long low = hex4(response_json, p + 2);
                        if (low >= 0xDC00 && low <= 0xDFFF) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                            p += 6;
                        }
                    }
                    append_utf8(out, static_cast<unsigned long>(cp));
                    break;
                }
                default: out += e;
            }
        } else {
            out += c;
        }
    }
    return "";
}

void LlmClient::ask(const std::string& question, const std::string& session_context,
                    std::function<void(const std::string& answer)> answer,
                    std::function<void(const std::string& error)> error) {
    if (!enabled()) {
        error("LLM help is off — configure an endpoint and key in the guide's settings to enable it.");
        return;
    }
    if (!transport_) {
        error("No network transport is wired up.");
        return;
    }
    const std::string body = build_request(question, session_context);
    transport_(body,
               [answer = std::move(answer), error = std::move(error)](const std::string& response,
                                                                      const std::string& err) {
                   if (!err.empty()) {
                       error(err);
                       return;
                   }
                   const std::string parsed = parse_answer(response);
                   if (parsed.empty())
                       error("The model answered, but I couldn't read the reply.");
                   else
                       answer(parsed);
               });
}

} // namespace blokkily::guide
