#pragma once

// The verification driver behind `blokkily --verify`. It drives the real
// window the way a producer would (clicks, drags, keys), reads the rendered
// scene back, and prints one BDD PASS / BDD FAIL line naming the first
// scenario that broke. Each scenario group lives in its own file,
// scenario_<name>.cpp, and registers itself with register_scenario().

#include "keyboard_model.hpp"
#include "app_controller.hpp"
#include "llm_model.hpp"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QPointF>
#include <QQuickItem>
#include <QQuickWindow>
#include <QString>

#include <functional>
#include <string>
#include <vector>

namespace blokkily::verify {

// Everything a scenario drives, and the verdict it accumulates.
class VerifyContext {
public:
    VerifyContext(QGuiApplication& app, QCommandLineParser& parser, QQuickWindow* window,
                  SongModel& song, PatternModel& pattern, Transport& transport,
                  AppController& controller, KeyboardModel& keyboard, LlmModel& llm,
                  blokkily::RtAudioOutput* output);

    QGuiApplication& app;
    QCommandLineParser& parser;
    QQuickWindow* window;
    // The size the window opened at on its screen, before the driver pinned
    // the canonical 1280 x 1080 every gate renders at.
    QSize opened_size;
    SongModel& song;
    PatternModel& pattern;
    Transport& transport;
    AppController& controller;
    KeyboardModel& keyboard;
    LlmModel& llm;
    // The deterministic device the production render callback feeds.
    blokkily::RtAudioOutput* output;

    // The verdict so far, and the first scenario reached while it was false.
    bool valid = false;
    QString failed;

    // Repeater delegates hang off the item that laid them out rather than
    // off the window, so a named item is looked for down the visual tree.
    QQuickItem* named(const QString& name) const;
    // findChild on the window: sees items that are QObject children.
    QQuickItem* item(const char* name) const;
    // Repeater delegates are visual children only, so findChild cannot see
    // them; walk childItems instead, down through the layouts that a
    // delegate nests its controls in.
    static QQuickItem* child_named(QQuickItem* parent, const QString& name);

    // Presses the rendered editor rather than calling the model behind it:
    // an editor that stops turning a click into an edit must fail the gate.
    void click_at(QQuickItem* item, QPointF local, Qt::MouseButton button,
                  Qt::KeyboardModifiers modifiers = Qt::NoModifier) const;
    void mouse_at(QQuickItem* target, QPointF local, QEvent::Type type,
                  Qt::MouseButton button, Qt::MouseButtons held) const;
    // Types one key at the window, the way a producer typing does.
    void type_key(Qt::Key key, const QString& text) const;
    void chord_key(Qt::Key key, Qt::KeyboardModifiers modifiers) const;

    // One 1024-sample stereo block of the production callback; the loudest
    // sample back, or -1 when the device did not run the callback.
    float pump();
    // The loudest sample of one side of the last pump's block: 0 is the
    // left, 1 the right.
    [[nodiscard]] float side_peak(int side) const;
    // Runs the event loop for a bounded time, so polled state catches up.
    static void settle(int milliseconds);

    // Marks a scenario as reached. BLOKKILY_TRACE=1 prints each one, so a
    // gate that hangs says where; the first one reached while failing is
    // named in the BDD FAIL line.
    void reached(const char* scenario);
    // Every action runs whether or not an earlier check failed, so one
    // failure cannot leave the song in a state the next scenario misreads;
    // only the verdict accumulates.
    void check(bool ok);
    // Shown is not the same as usable: a panel squeezed to nothing by a
    // neighbour still reports itself visible.
    static bool usable(const QQuickItem* item, double width, double height);

    // Renders the real scene and writes it where --screenshot says. The gate
    // checks dimensions and that pixels vary; a person or an agent still has
    // to look at it.
    bool save_screenshot() const;
    // Same as save_screenshot, but to a path the scenario chooses — used
    // by scenario groups that capture the bar at several phases (idle,
    // proposal, applied) for visual inspection beyond the gate's end frame.
    bool save_screenshot_to(const QString& path) const;

    // Readings of the rendered editors, not of the model behind them: a
    // projection that stops following the canonical pattern must fail.
    bool rendered_step(int step) const;
    QString tracker_note(int row) const;
    bool roll_draws(int step) const;

    // Prints the verdict line, "BDD PASS" or "BDD FAIL[ at: <scenario>]"
    // followed by `details`, and ends the application with 0 or 3.
    void finish(const std::string& details);

private:
    bool trace_ = false;
    std::vector<float> stereo_;
};

using Scenario = std::function<void(VerifyContext&)>;

// Makes `run` the scenario group `--scenario <name>` selects. The group run
// without --scenario is registered as "default".
bool register_scenario(const std::string& name, Scenario run);

// Schedules the selected scenario group on the event loop. An unknown or
// empty name runs the default group.
void schedule(VerifyContext& context, const QString& scenario);

}  // namespace blokkily::verify
