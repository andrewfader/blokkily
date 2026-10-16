#pragma once

// Scripted interactive lessons as data. A lesson is JSON (see
// ui/guide/lessons/) so agents can author more without touching C++:
//
// {
//   "id": "first-beat",
//   "title": "Your first beat",
//   "steps": [
//     { "title": "STEPS",
//       "text": "Click a step to light it…",
//       "spotlight": "stepGrid",          // objectName to ring ("" for none)
//       "action": {"id": "add-note",      // optional guide-performed action
//                  "args": {"step": "0", "key": "36"}},
//       "check": "note-added" }           // "none" | "note-added" |
//   ]                                     // "transport-started" | "tempo-changed"
// }
//
// A step's check names the AppEvent kind that completes it; the engine
// watches the bus and advances when the producer (or the guide) does it.

#include "blokkily/guide/event_bus.hpp"

#include <map>
#include <string>
#include <vector>

namespace blokkily::guide {

struct LessonAction {
    std::string id;
    std::map<std::string, std::string> args;
};

struct LessonStep {
    std::string title;
    std::string text;
    std::string spotlight; // objectName to ring; "" for none
    LessonAction action;
    bool has_action = false;
    std::string check = "none"; // see above
};

struct Lesson {
    std::string id;
    std::string title;
    std::vector<LessonStep> steps;
};

class LessonEngine {
public:
    // Parses one lesson document. False + *error on malformed JSON.
    bool load(const std::string& json, std::string* error = nullptr);
    [[nodiscard]] bool has_lesson() const { return loaded_; }

    void start(); // step 0
    [[nodiscard]] const Lesson& lesson() const { return lesson_; }
    [[nodiscard]] std::size_t step_index() const { return step_; }
    [[nodiscard]] const LessonStep& step() const { return lesson_.steps[step_]; }
    [[nodiscard]] bool at_end() const;
    void next();
    void back();

    // True when the event satisfies the current step's check; advances.
    // "none" checks never auto-advance (the Continue button calls next()).
    bool check_event(const AppEvent& event);

private:
    Lesson lesson_;
    std::size_t step_ = 0;
    bool loaded_ = false;
};

} // namespace blokkily::guide
