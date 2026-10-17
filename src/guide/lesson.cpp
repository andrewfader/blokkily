#include "blokkily/guide/lesson.hpp"

#include <cctype>
#include <cmath>
#include <stdexcept>

namespace blokkily::guide {
namespace {

// A minimal JSON reader: objects, arrays, strings (with escapes), numbers,
// true/false/null. Enough for lesson documents; errors throw.
struct JsonValue {
    enum class Type { null, boolean, number, string, array, object } type = Type::null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<JsonValue> array;
    std::vector<std::pair<std::string, JsonValue>> object;

    [[nodiscard]] const JsonValue* find(const std::string& key) const {
        if (type != Type::object) return nullptr;
        for (const auto& [k, v] : object)
            if (k == key) return &v;
        return nullptr;
    }
    [[nodiscard]] std::string get_string(const std::string& key,
                                         const std::string& fallback = "") const {
        const auto* v = find(key);
        return (v != nullptr && v->type == Type::string) ? v->string : fallback;
    }
};

struct Parser {
    const char* p;
    const char* end;
    int depth = 0; // nesting, bounded so a hostile file cannot exhaust the stack

    [[noreturn]] void fail(const std::string& what) {
        throw std::runtime_error("lesson JSON: " + what);
    }
    void skip_ws() {
        while (p != end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
    }
    char peek() {
        if (p == end) fail("unexpected end");
        return *p;
    }
    void expect(char c) {
        if (peek() != c) fail(std::string("expected '") + c + "'");
        ++p;
    }
    JsonValue parse_value() {
        skip_ws();
        const char c = peek();
        if (c == '{' || c == '[') {
            if (++depth > 32) fail("nested too deeply");
            JsonValue v = c == '{' ? parse_object() : parse_array();
            --depth;
            return v;
        }
        if (c == '"') {
            JsonValue v;
            v.type = JsonValue::Type::string;
            v.string = parse_string();
            return v;
        }
        if (c == 't' || c == 'f') return parse_bool();
        if (c == 'n') {
            expect('n');
            expect('u');
            expect('l');
            expect('l');
            return {};
        }
        if (c == '-' || (c >= '0' && c <= '9')) return parse_number();
        fail("unexpected character");
    }
    JsonValue parse_object() {
        JsonValue v;
        v.type = JsonValue::Type::object;
        expect('{');
        skip_ws();
        if (peek() == '}') {
            ++p;
            return v;
        }
        while (true) {
            skip_ws();
            std::string key = parse_string();
            skip_ws();
            expect(':');
            v.object.emplace_back(std::move(key), parse_value());
            skip_ws();
            const char c = peek();
            if (c == ',') {
                ++p;
                continue;
            }
            if (c == '}') {
                ++p;
                return v;
            }
            fail("expected ',' or '}'");
        }
    }
    JsonValue parse_array() {
        JsonValue v;
        v.type = JsonValue::Type::array;
        expect('[');
        skip_ws();
        if (peek() == ']') {
            ++p;
            return v;
        }
        while (true) {
            v.array.push_back(parse_value());
            skip_ws();
            const char c = peek();
            if (c == ',') {
                ++p;
                continue;
            }
            if (c == ']') {
                ++p;
                return v;
            }
            fail("expected ',' or ']'");
        }
    }
    std::string parse_string() {
        expect('"');
        std::string out;
        while (true) {
            if (p == end) fail("unterminated string");
            const char c = *p++;
            if (c == '"') return out;
            if (c == '\\') {
                if (p == end) fail("unterminated escape");
                const char e = *p++;
                switch (e) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'u': append_utf8(out, code_point()); break;
                    default: fail("bad escape");
                }
            } else {
                out += c;
            }
        }
    }
    // Four hex digits after \u.
    unsigned hex4() {
        unsigned value = 0;
        for (int i = 0; i < 4; ++i) {
            if (p == end) fail("unterminated \\u escape");
            const char c = *p++;
            value <<= 4;
            if (c >= '0' && c <= '9') value |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<unsigned>(c - 'A' + 10);
            else fail("bad \\u escape");
        }
        return value;
    }
    // A \u escape, joining a surrogate pair into one code point.
    unsigned code_point() {
        const unsigned high = hex4();
        if (high < 0xD800 || high > 0xDBFF) {
            if (high >= 0xDC00 && high <= 0xDFFF) fail("lone low surrogate");
            return high;
        }
        if (end - p < 6 || p[0] != '\\' || p[1] != 'u') fail("lone high surrogate");
        p += 2;
        const unsigned low = hex4();
        if (low < 0xDC00 || low > 0xDFFF) fail("bad surrogate pair");
        return 0x10000 + ((high - 0xD800) << 10) + (low - 0xDC00);
    }
    static void append_utf8(std::string& out, unsigned cp) {
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
    JsonValue parse_bool() {
        JsonValue v;
        v.type = JsonValue::Type::boolean;
        if (peek() == 't') {
            expect('t');
            expect('r');
            expect('u');
            expect('e');
            v.boolean = true;
        } else {
            expect('f');
            expect('a');
            expect('l');
            expect('s');
            expect('e');
            v.boolean = false;
        }
        return v;
    }
    JsonValue parse_number() {
        JsonValue v;
        v.type = JsonValue::Type::number;
        const char* start = p;
        if (peek() == '-') ++p;
        while (p != end && (std::isdigit(static_cast<unsigned char>(*p)) || *p == '.' ||
                            *p == 'e' || *p == 'E' || *p == '+' || *p == '-'))
            ++p;
        try {
            v.number = std::stod(std::string(start, p));
        } catch (const std::exception&) {
            fail("bad number");
        }
        return v;
    }
};

} // namespace

bool LessonEngine::load(const std::string& json, std::string* error) {
    try {
        Parser parser{json.data(), json.data() + json.size()};
        JsonValue root = parser.parse_value();
        parser.skip_ws();
        if (parser.p != parser.end) throw std::runtime_error("trailing data");
        if (root.type != JsonValue::Type::object)
            throw std::runtime_error("lesson must be an object");

        Lesson lesson;
        lesson.id = root.get_string("id");
        lesson.title = root.get_string("title");
        if (lesson.id.empty()) throw std::runtime_error("lesson needs an id");
        const auto* steps = root.find("steps");
        if (steps == nullptr || steps->type != JsonValue::Type::array || steps->array.empty())
            throw std::runtime_error("lesson needs a non-empty steps array");
        for (const auto& s : steps->array) {
            LessonStep step;
            step.title = s.get_string("title");
            step.text = s.get_string("text");
            if (step.text.empty()) throw std::runtime_error("step needs text");
            step.spotlight = s.get_string("spotlight");
            step.check = s.get_string("check", "none");
            if (const auto* count = s.find("count")) {
                if (count->type != JsonValue::Type::number || count->number < 1 || count->number > 64 ||
                    count->number != static_cast<int>(count->number))
                    throw std::runtime_error("a step's count is a whole number from 1 to 64");
                step.count = static_cast<int>(count->number);
            }
            if (const auto* action = s.find("action")) {
                step.has_action = true;
                step.action.id = action->get_string("id");
                if (const auto* args = action->find("args")) {
                    if (args->type == JsonValue::Type::object) {
                        for (const auto& [k, v] : args->object) {
                            if (v.type == JsonValue::Type::string)
                                step.action.args[k] = v.string;
                            else if (v.type == JsonValue::Type::number) {
                                if (!(std::abs(v.number) < 1e9)) throw std::runtime_error("an action argument is out of range");
                                step.action.args[k] = std::to_string(static_cast<long long>(v.number));
                            }
                        }
                    }
                }
            }
            lesson.steps.push_back(std::move(step));
        }
        lesson_ = std::move(lesson);
        step_ = 0;
        loaded_ = true;
        return true;
    } catch (const std::exception& e) {
        if (error != nullptr) *error = e.what();
        loaded_ = false;
        return false;
    }
}

void LessonEngine::start() {
    step_ = 0;
    hits_ = 0;
}

bool LessonEngine::at_end() const {
    return !loaded_ || step_ >= lesson_.steps.size();
}

void LessonEngine::next() {
    if (loaded_ && step_ < lesson_.steps.size()) ++step_;
    hits_ = 0;
}

void LessonEngine::back() {
    if (step_ > 0) --step_;
    hits_ = 0;
}

bool LessonEngine::check_event(const AppEvent& event) {
    if (at_end() || step().check == "none") return false;
    // The lesson is the producer's to do: what the guide or an agent does
    // never counts toward it.
    if (event.source != EventSource::user) return false;
    const auto want = step().check;
    const bool hit =
        (want == "note-added" && event.kind == EventKind::note_added) ||
        (want == "note-removed" && event.kind == EventKind::note_removed) ||
        (want == "transport-started" && event.kind == EventKind::transport_started) ||
        (want == "transport-stopped" && event.kind == EventKind::transport_stopped) ||
        (want == "tempo-changed" && event.kind == EventKind::tempo_changed) ||
        (want == "track-added" && event.kind == EventKind::track_added) ||
        (want == "guide-action" && event.kind == EventKind::guide_action);
    if (!hit) return false;
    if (++hits_ >= step().count) next();
    return true;
}

} // namespace blokkily::guide
