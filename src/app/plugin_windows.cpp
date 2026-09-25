#include "plugin_windows.hpp"

#include <QEvent>
#include <QGuiApplication>
#include <QTimer>
#include <QWindow>

#include <algorithm>
#include <cmath>

namespace {

// A window an embedded editor lives in. Closing it closes the editor.
class HostWindow final : public QWindow {
public:
    std::function<void()> on_close;

protected:
    bool event(QEvent* event) override {
        if (event->type() == QEvent::Close && on_close) {
            // The owner closes the editor and schedules this window's deletion;
            // the event itself is still delivered normally.
            auto close = on_close;
            close();
        }
        return QWindow::event(event);
    }
};

int logical(std::uint32_t physical, qreal ratio) {
    return static_cast<int>(std::ceil(static_cast<double>(physical) / std::max<qreal>(ratio, 1.0)));
}

} // namespace

struct PluginWindows::Editor final : blokkily::EditorHost {
    PluginWindows* owner = nullptr;
    blokkily::ProcessorAddress where;
    blokkily::PluginInstance* instance = nullptr;
    HostWindow* window = nullptr;   // owned; deleted later, never inside its own event
    Placement placement = Placement::embedded;
    int resizes = 0;

    ~Editor() override {
        if (window != nullptr) {
            window->on_close = nullptr;
            window->hide();
            window->deleteLater();
        }
    }
    void request_resize(std::uint32_t width, std::uint32_t height) override {
        ++resizes;
        if (window != nullptr && width > 0 && height > 0) {
            const auto ratio = window->devicePixelRatio();
            window->resize(logical(width, ratio), logical(height, ratio));
        }
    }
    void request_show() override {
        if (window != nullptr) window->show();
    }
    void request_hide() override {
        if (window != nullptr) window->hide();
    }
    // The editor is gone: the plugin closed it, or its instance is being
    // destroyed. The instance must not be called again.
    void closed() override {
        instance = nullptr;
        owner->forget(this);
    }
};

PluginWindows::PluginWindows(QObject* parent) : QObject(parent) {}

PluginWindows::~PluginWindows() {
    closeAll();
    retired_.clear();
}

bool PluginWindows::open(blokkily::ProcessorAddress where, blokkily::PluginInstance& instance,
                         const QString& title, QString* error) {
    using blokkily::WindowApi;
    auto fail = [error](const QString& message) {
        if (error != nullptr) *error = message;
        return false;
    };
    if (isOpen(where)) return fail(QStringLiteral("The editor is already open"));
    if (!instance.has_editor()) return fail(QStringLiteral("This instrument has no editor"));

    const QString platform = QGuiApplication::platformName();
    const bool x11_parent = platform == QLatin1String("xcb") || platform == QLatin1String("offscreen");
    const bool embed = x11_parent && instance.supports_editor(WindowApi::x11, false);
    if (!embed && !instance.supports_editor(WindowApi::x11, true))
        return fail(QStringLiteral("Plugin window needs an X11 display"));

    auto editor = std::make_unique<Editor>();
    editor->owner = this;
    editor->where = where;
    editor->instance = &instance;
    std::string reason;
    blokkily::EditorSize size;
    if (embed) {
        editor->placement = Placement::embedded;
        editor->window = new HostWindow();
        editor->window->setTitle(title);
        editor->window->resize(320, 200);
        editor->window->create();
        const blokkily::NativeParent parent{WindowApi::x11,
                                            static_cast<std::uintptr_t>(editor->window->winId()),
                                            editor->window->devicePixelRatio()};
        if (!instance.open_editor(&parent, *editor, &size, &reason)) {
            editor->instance = nullptr;
            return fail(QString::fromStdString(reason));
        }
        if (size.width > 0 && size.height > 0) {
            const auto ratio = editor->window->devicePixelRatio();
            editor->window->resize(logical(size.width, ratio), logical(size.height, ratio));
        }
        auto* raw = editor.get();
        editor->window->on_close = [this, raw] { (void)close(raw->where); };
        editor->window->show();
    } else {
        editor->placement = Placement::floating;
        if (!instance.open_editor(nullptr, *editor, &size, &reason)) {
            editor->instance = nullptr;
            return fail(QString::fromStdString(reason));
        }
    }
    editors_.push_back(std::move(editor));
    emit changed();
    return true;
}

bool PluginWindows::close(blokkily::ProcessorAddress where) {
    const auto found = std::find_if(editors_.begin(), editors_.end(),
                                    [&](const auto& editor) { return editor->where == where; });
    if (found == editors_.end()) return false;
    auto editor = std::move(*found);
    editors_.erase(found);
    if (editor->instance != nullptr) editor->instance->close_editor();
    editor.reset();
    emit changed();
    return true;
}

void PluginWindows::closeAll() {
    auto editors = std::move(editors_);
    editors_.clear();
    for (auto& editor : editors)
        if (editor->instance != nullptr) editor->instance->close_editor();
    const bool any = !editors.empty();
    editors.clear();
    if (any) emit changed();
}

void PluginWindows::forget(Editor* editor) {
    const auto found = std::find_if(editors_.begin(), editors_.end(),
                                    [&](const auto& candidate) { return candidate.get() == editor; });
    if (found == editors_.end()) return;
    // The adapter is still inside this editor's closed() when this runs, so
    // the entry is kept until the next turn of the event loop. Its window is
    // hidden now.
    auto kept = std::move(*found);
    editors_.erase(found);
    if (kept->window != nullptr) kept->window->hide();
    retired_.push_back(std::move(kept));
    QTimer::singleShot(0, this, [this] { retired_.clear(); });
    emit changed();
}

bool PluginWindows::isOpen(blokkily::ProcessorAddress where) const {
    return std::any_of(editors_.begin(), editors_.end(),
                       [&](const auto& editor) { return editor->where == where; });
}

std::vector<blokkily::ProcessorAddress> PluginWindows::openAddresses() const {
    std::vector<blokkily::ProcessorAddress> addresses;
    for (const auto& editor : editors_) addresses.push_back(editor->where);
    return addresses;
}

QWindow* PluginWindows::window(blokkily::ProcessorAddress where) const {
    for (const auto& editor : editors_)
        if (editor->where == where) return editor->window;
    return nullptr;
}

std::optional<PluginWindows::Placement> PluginWindows::placement(
    blokkily::ProcessorAddress where) const {
    for (const auto& editor : editors_)
        if (editor->where == where) return editor->placement;
    return std::nullopt;
}

blokkily::PluginInstance* PluginWindows::instance(blokkily::ProcessorAddress where) const {
    for (const auto& editor : editors_)
        if (editor->where == where) return editor->instance;
    return nullptr;
}

int PluginWindows::resizeRequests(blokkily::ProcessorAddress where) const {
    for (const auto& editor : editors_)
        if (editor->where == where) return editor->resizes;
    return 0;
}

void PluginWindows::reconcile(
    const std::vector<std::optional<std::size_t>>* remap,
    const std::function<blokkily::PluginInstance*(blokkily::ProcessorAddress)>& at) {
    std::vector<blokkily::ProcessorAddress> stale;
    for (auto& editor : editors_) {
        if (remap != nullptr && editor->where.kind == blokkily::BusKind::track) {
            const auto bus = editor->where.bus;
            if (bus < remap->size() && (*remap)[bus])
                editor->where.bus = static_cast<std::uint32_t>(*(*remap)[bus]);
        }
        if (at(editor->where) != editor->instance) stale.push_back(editor->where);
    }
    for (const auto& where : stale) (void)close(where);
}
