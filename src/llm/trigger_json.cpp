#include "trigger_json.hpp"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <variant>

namespace blokkily::llm {
namespace {

QJsonObject noteToJson(const Note& note) {
    return QJsonObject{{"key", note.key},
                       {"velocity", static_cast<double>(note.velocity)},
                       {"cents", note.cents}};
}

QJsonObject chordToJson(const Chord& chord, Tick ticks_per_step) {
    QJsonArray intervals;
    for (const auto interval : chord.intervals) intervals.append(interval);
    QJsonObject object{{"root", chord.root},
                       {"intervals", intervals},
                       {"inversion", chord.inversion},
                       {"velocity", static_cast<double>(chord.velocity)},
                       {"strum_steps", static_cast<double>(chord.strum) /
                                           static_cast<double>(ticks_per_step)}};
    if (!chord.velocities.empty()) {
        QJsonArray velocities;
        for (const auto velocity : chord.velocities)
            velocities.append(static_cast<double>(velocity));
        object["velocities"] = velocities;
    }
    if (!chord.durations.empty()) {
        QJsonArray durations;
        for (const auto duration : chord.durations)
            durations.append(static_cast<double>(duration) /
                             static_cast<double>(ticks_per_step));
        object["durations_steps"] = durations;
    }
    return object;
}

double numberOr(const QJsonObject& object, const char* key, double fallback) {
    const auto value = object.value(key);
    return value.isDouble() ? value.toDouble() : fallback;
}

// Clamps `value` into [low, high]; when it had to move, says so in
// `corrections` so the panel can admit the reply was repaired.
double clamped(double value, double low, double high, const char* what,
               QStringList* corrections) {
    const double result = std::clamp(value, low, high);
    if (result != value && corrections != nullptr)
        corrections->append(QStringLiteral("%1 clamped from %2 to %3")
                                .arg(QString::fromLatin1(what))
                                .arg(value)
                                .arg(result));
    return result;
}

} // namespace

QJsonArray triggersToJson(std::span<const Trigger> triggers, Tick ticks_per_step) {
    QJsonArray array;
    for (const auto& trigger : triggers) {
        QJsonObject object{
            {"step", static_cast<double>(trigger.start) /
                         static_cast<double>(ticks_per_step)},
            {"length_steps", static_cast<double>(trigger.duration) /
                                 static_cast<double>(ticks_per_step)},
            {"probability", static_cast<double>(trigger.probability)},
            {"ratchets", static_cast<int>(trigger.ratchets)},
            {"micro_offset", static_cast<double>(trigger.micro_offset)},
            {"play_on_loop", static_cast<int>(trigger.play_on_loop)},
        };
        if (std::holds_alternative<Note>(trigger.musical_data))
            object["note"] = noteToJson(std::get<Note>(trigger.musical_data));
        else
            object["chord"] = chordToJson(std::get<Chord>(trigger.musical_data),
                                          ticks_per_step);
        array.append(object);
    }
    return array;
}

std::optional<Trigger> triggerFromJson(const QJsonObject& object, Tick ticks_per_step,
                                       int key_limit, QString* error) {
    if (!object.contains("note") && !object.contains("chord")) {
        if (error != nullptr) *error = QStringLiteral("a trigger has neither note nor chord");
        return std::nullopt;
    }
    const double step = numberOr(object, "step", -1.0);
    if (step < 0.0) {
        if (error != nullptr) *error = QStringLiteral("a trigger has no step");
        return std::nullopt;
    }
    Trigger trigger;
    trigger.start = static_cast<Tick>(std::llround(step * static_cast<double>(ticks_per_step)));
    const double length = numberOr(object, "length_steps", 1.0);
    trigger.duration = std::max<Tick>(
        1, static_cast<Tick>(std::llround(length * static_cast<double>(ticks_per_step))));
    trigger.micro_offset =
        static_cast<Tick>(std::llround(numberOr(object, "micro_offset", 0.0)));
    trigger.probability = static_cast<float>(
        clamped(numberOr(object, "probability", 1.0), 0.0, 1.0, "probability", nullptr));
    trigger.ratchets = static_cast<std::uint8_t>(std::clamp(
        static_cast<int>(numberOr(object, "ratchets", 1.0)), 1, 16));
    trigger.play_on_loop = static_cast<std::uint8_t>(std::clamp(
        static_cast<int>(numberOr(object, "play_on_loop", 0.0)), 0, 255));

    const auto keyOf = [&](const QJsonObject& holder, const char* name, int fallback) {
        const int key = static_cast<int>(numberOr(holder, name, fallback));
        return static_cast<std::int16_t>(std::clamp(key, 0, std::max(0, key_limit - 1)));
    };

    if (object.value("chord").isObject()) {
        const auto chordObject = object.value("chord").toObject();
        Chord chord;
        chord.root = keyOf(chordObject, "root", 60);
        chord.velocity = static_cast<float>(
            clamped(numberOr(chordObject, "velocity", 0.8), 0.0, 1.0, "velocity", nullptr));
        QJsonArray intervals = chordObject.value("intervals").toArray();
        if (intervals.isEmpty()) intervals = QJsonArray{0, 4, 7};
        chord.intervals.clear();
        for (const auto value : intervals)
            chord.intervals.push_back(static_cast<std::int16_t>(value.toInt()));
        chord.inversion = static_cast<std::int8_t>(
            std::clamp(static_cast<int>(numberOr(chordObject, "inversion", 0.0)), 0, 8));
        chord.strum = static_cast<Tick>(std::llround(
            numberOr(chordObject, "strum_steps", 0.0) * static_cast<double>(ticks_per_step)));
        const auto velocities = chordObject.value("velocities").toArray();
        if (!velocities.isEmpty()) {
            for (const auto value : velocities)
                chord.velocities.push_back(static_cast<float>(
                    std::clamp(value.toDouble(0.8), 0.0, 1.0)));
        }
        const auto durations = chordObject.value("durations_steps").toArray();
        if (!durations.isEmpty()) {
            for (const auto value : durations)
                chord.durations.push_back(std::max<Tick>(
                    1, static_cast<Tick>(std::llround(
                           value.toDouble(1.0) * static_cast<double>(ticks_per_step)))));
        }
        trigger.musical_data = chord;
    } else {
        const auto noteObject = object.value("note").toObject();
        Note note;
        note.key = keyOf(noteObject, "key", 60);
        note.velocity = static_cast<float>(
            clamped(numberOr(noteObject, "velocity", 0.8), 0.0, 1.0, "velocity", nullptr));
        note.cents = clamped(numberOr(noteObject, "cents", 0.0), -100.0, 100.0, "cents",
                             nullptr);
        trigger.musical_data = note;
    }
    return trigger;
}

std::optional<QJsonObject> extractJsonObject(const QString& text, QString* error) {
    QString trimmed = text;
    // Reasoning models (MiniMax-M3, DeepSeek-R1, Qwen3) think aloud in a
    // <think> block before answering, and the thinking quotes braces from
    // its drafts. Only what follows the thinking is the answer.
    static const QRegularExpression thinking(
        QStringLiteral("<think>.*?</think>"),
        QRegularExpression::DotMatchesEverythingOption |
            QRegularExpression::CaseInsensitiveOption);
    trimmed.remove(thinking);
    trimmed = trimmed.trimmed();
    // Models like to fence JSON in a markdown block; strip the fence.
    if (trimmed.startsWith(QLatin1String("```"))) {
        const auto firstNewline = trimmed.indexOf(QLatin1Char('\n'));
        const auto closing = trimmed.lastIndexOf(QLatin1String("```"));
        if (firstNewline > 0 && closing > firstNewline)
            trimmed = trimmed.mid(firstNewline + 1, closing - firstNewline - 1).trimmed();
    }
    // Prose around the object is dropped: the first '{' to the last '}'.
    const auto open = trimmed.indexOf(QLatin1Char('{'));
    const auto close = trimmed.lastIndexOf(QLatin1Char('}'));
    if (open < 0 || close <= open) {
        if (error != nullptr)
            *error = QStringLiteral("the reply held no JSON object");
        return std::nullopt;
    }
    QJsonParseError parseError{};
    const auto document =
        QJsonDocument::fromJson(trimmed.mid(open, close - open + 1).toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error != nullptr)
            *error = QStringLiteral("the reply was not valid JSON: %1")
                         .arg(parseError.errorString());
        return std::nullopt;
    }
    return document.object();
}

ParseResult parseResponse(const QString& text, Tick ticks_per_step, int pattern_steps,
                          int key_limit) {
    ParseResult result;
    QString error;
    const auto object = extractJsonObject(text, &error);
    if (!object.has_value()) {
        result.error = error;
        return result;
    }
    const auto modeText = object->value("mode").toString(QStringLiteral("replace")).toLower();
    if (modeText == QLatin1String("add"))
        result.response.mode = Mode::add;
    else if (modeText == QLatin1String("modify"))
        result.response.mode = Mode::modify;
    else
        result.response.mode = Mode::replace;

    const auto triggers = object->value("triggers").toArray();
    if (triggers.isEmpty()) {
        result.error = QStringLiteral("the reply named no triggers");
        return result;
    }
    const Tick pattern_length = static_cast<Tick>(pattern_steps) * ticks_per_step;
    int dropped = 0;
    for (const auto value : triggers) {
        if (!value.isObject()) {
            ++dropped;
            continue;
        }
        QString triggerError;
        auto trigger =
            triggerFromJson(value.toObject(), ticks_per_step, key_limit, &triggerError);
        if (!trigger.has_value()) {
            result.corrections.append(QStringLiteral("dropped: %1").arg(triggerError));
            ++dropped;
            continue;
        }
        // A trigger that starts outside the pattern can never sound; one that
        // runs past the end is tucked back inside it.
        if (trigger->start >= pattern_length) {
            ++dropped;
            result.corrections.append(QStringLiteral("dropped a trigger past the pattern's end"));
            continue;
        }
        if (trigger->start + trigger->duration > pattern_length) {
            trigger->duration = pattern_length - trigger->start;
            result.corrections.append(QStringLiteral("a note was shortened to fit the pattern"));
        }
        result.response.triggers.push_back(*trigger);
    }
    if (result.response.triggers.empty()) {
        result.error = QStringLiteral("every trigger in the reply was unusable");
        return result;
    }
    if (dropped > 0)
        result.corrections.append(QStringLiteral("%1 trigger(s) dropped").arg(dropped));
    result.ok = true;
    return result;
}

} // namespace blokkily::llm
