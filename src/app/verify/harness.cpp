#include "verify/harness.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <utility>

namespace blokkily::verify {

namespace {

// Function-local, so registrations from other files' static initialisers
// never meet an unconstructed table.
std::map<std::string, Scenario>& scenarios() {
    static std::map<std::string, Scenario> table;
    return table;
}

QQuickItem* find_item(QQuickItem* from, const QString& name) {
    if (from == nullptr) return nullptr;
    if (from->objectName() == name) return from;
    for (auto* child : from->childItems())
        if (auto* found = find_item(child, name)) return found;
    return nullptr;
}

}  // namespace

VerifyContext::VerifyContext(QGuiApplication& app, QCommandLineParser& parser,
                             QQuickWindow* window, SongModel& song, PatternModel& pattern,
                             Transport& transport, AppController& controller,
                             KeyboardModel& keyboard, blokkily::RtAudioOutput* output)
    : app(app), parser(parser), window(window), song(song), pattern(pattern),
      transport(transport), controller(controller), keyboard(keyboard), output(output),
      trace_(qEnvironmentVariableIsSet("BLOKKILY_TRACE")), stereo_(1024) {}

QQuickItem* VerifyContext::named(const QString& name) const {
    return find_item(window->contentItem(), name);
}

QQuickItem* VerifyContext::item(const char* name) const {
    return window->findChild<QQuickItem*>(QString::fromLatin1(name));
}

QQuickItem* VerifyContext::child_named(QQuickItem* parent, const QString& name) {
    if (parent == nullptr) return nullptr;
    for (auto* child : parent->childItems()) {
        if (child->objectName() == name) return child;
        if (auto* found = child_named(child, name)) return found;
    }
    return nullptr;
}

void VerifyContext::click_at(QQuickItem* item, QPointF local, Qt::MouseButton button,
                             Qt::KeyboardModifiers modifiers) const {
    if (item == nullptr) return;
    const QPointF scene = item->mapToScene(local);
    const QPointF global = window->mapToGlobal(scene);
    QMouseEvent press(QEvent::MouseButtonPress, scene, scene, global, button, button,
                      modifiers);
    QCoreApplication::sendEvent(window, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, scene, scene, global, button,
                        Qt::NoButton, modifiers);
    QCoreApplication::sendEvent(window, &release);
}

void VerifyContext::mouse_at(QQuickItem* target, QPointF local, QEvent::Type type,
                             Qt::MouseButton button, Qt::MouseButtons held) const {
    if (target == nullptr) return;
    const QPointF scene = target->mapToScene(local);
    QMouseEvent event(type, scene, scene, window->mapToGlobal(scene), button, held,
                      Qt::NoModifier);
    QCoreApplication::sendEvent(window, &event);
}

void VerifyContext::type_key(Qt::Key key, const QString& text) const {
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, text);
    QCoreApplication::sendEvent(window, &press);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier, text);
    QCoreApplication::sendEvent(window, &release);
}

void VerifyContext::chord_key(Qt::Key key, Qt::KeyboardModifiers modifiers) const {
    QKeyEvent press(QEvent::KeyPress, key, modifiers);
    QCoreApplication::sendEvent(window, &press);
    QKeyEvent release(QEvent::KeyRelease, key, modifiers);
    QCoreApplication::sendEvent(window, &release);
}

float VerifyContext::pump() {
    // A pump stands for the device running a block later. By then the event
    // loop has turned and any recompile an edit asked for has been made; a
    // driver that pumps straight after an edit, without turning the loop,
    // must still hear it.
    controller.flushRecompile();
    std::fill(stereo_.begin(), stereo_.end(), 0.0F);
    if (output == nullptr || !output->pump(stereo_)) return -1.0F;
    float peak = 0.0F;
    for (const float sample : stereo_) peak = std::max(peak, std::abs(sample));
    return peak;
}

void VerifyContext::settle(int milliseconds) {
    QEventLoop waiting;
    QTimer::singleShot(milliseconds, &waiting, &QEventLoop::quit);
    waiting.exec();
}

void VerifyContext::reached(const char* scenario) {
    if (trace_) std::cerr << "reached: " << scenario << (valid ? "" : " (failing)") << std::endl;
    if (!valid && failed.isEmpty()) failed = QString::fromLatin1(scenario);
}

void VerifyContext::check(bool ok) { valid = valid && ok; }

bool VerifyContext::usable(const QQuickItem* item, double width, double height) {
    return item != nullptr && item->width() >= width && item->height() >= height;
}

bool VerifyContext::save_screenshot() const {
    const QString screenshot = parser.value("screenshot");
    if (screenshot.isEmpty()) return true;
    QDir{}.mkpath(QFileInfo(screenshot).absolutePath());
    const QImage image = window->grabWindow();
    if (image.isNull() || image.width() != 1280 || image.height() != 800) return false;
    const auto first = image.pixelColor(0, 0);
    bool varied = false;
    for (int y = 0; y < image.height() && !varied; y += 20)
        for (int x = 0; x < image.width(); x += 20)
            if (image.pixelColor(x, y) != first) { varied = true; break; }
    return varied && image.save(screenshot, "PNG");
}

bool VerifyContext::rendered_step(int step) const {
    const auto* grid = window->findChild<QQuickItem*>("stepGrid");
    if (grid == nullptr) return false;
    const QString wanted = QString("step%1").arg(step);
    for (const auto* cell : grid->childItems())
        if (cell->objectName() == wanted) return cell->property("active").toBool();
    return false;
}

QString VerifyContext::tracker_note(int row) const {
    auto* cell = named(QString("trackerNote%1").arg(row));
    if (cell == nullptr) return QString();
    for (const auto* child : cell->childItems())
        if (child->property("text").isValid()) return child->property("text").toString();
    return QString();
}

bool VerifyContext::roll_draws(int step) const {
    return named(QString("rollNote%1").arg(step)) != nullptr;
}

void VerifyContext::finish(const std::string& details) {
    std::cout << (valid ? "BDD PASS" : "BDD FAIL")
              << (failed.isEmpty() ? "" : " at: " + failed.toStdString())
              << details << '\n';
    app.exit(valid ? 0 : 3);
}

bool register_scenario(const std::string& name, Scenario run) {
    return scenarios().insert_or_assign(name, std::move(run)).second;
}

void schedule(VerifyContext& context, const QString& scenario) {
    QTimer::singleShot(500, &context.app, [&context, name = scenario.toStdString()] {
        context.valid = context.pattern.rowCount() == 4;
        context.failed.clear();
        auto& table = scenarios();
        auto found = table.find(name);
        if (found == table.end()) found = table.find("default");
        if (found == table.end()) {
            std::cout << "BDD FAIL at: no scenario named " << name << '\n';
            context.app.exit(3);
            return;
        }
        found->second(context);
    });
}

}  // namespace blokkily::verify
