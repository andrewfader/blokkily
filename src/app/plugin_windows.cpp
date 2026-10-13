#include "plugin_windows.hpp"

#include "x11_host_window.hpp"

#include <QEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QSocketNotifier>
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

int to_logical(std::uint32_t physical, qreal ratio) {
    return static_cast<int>(std::ceil(static_cast<double>(physical) / std::max<qreal>(ratio, 1.0)));
}

} // namespace

struct PluginWindows::Editor final : blokkily::EditorHost {
    PluginWindows* owner = nullptr;
    blokkily::ProcessorAddress where;
    blokkily::PluginInstance* instance = nullptr;
    HostWindow* window = nullptr;   // owned; deleted later, never inside its own event
    std::uintptr_t x11_window = 0;  // owned, on owner->x11_; for Placement::xwayland
    Placement placement = Placement::embedded;
    // The editor measures in logical units (Cocoa points, a JUCE editor on
    // macOS or Windows), which the window takes as they are.
    bool logical = false;
    int resizes = 0;

    ~Editor() override {
        if (window != nullptr) {
            window->on_close = nullptr;
            window->hide();
            window->deleteLater();
        }
        if (x11_window != 0 && owner->x11_) owner->x11_->destroy(x11_window);
    }
    void request_resize(std::uint32_t width, std::uint32_t height) override {
        ++resizes;
        if (window != nullptr && width > 0 && height > 0) {
            const auto ratio = logical ? 1.0 : window->devicePixelRatio();
            window->resize(to_logical(width, ratio), to_logical(height, ratio));
        }
        // An X11 editor under XWayland is as many X pixels as it asks for.
        if (x11_window != 0) owner->x11_->resize(x11_window, width, height);
    }
    void request_show() override {
        if (window != nullptr) window->show();
        if (x11_window != 0) owner->x11_->show(x11_window);
    }
    void request_hide() override {
        if (window != nullptr) window->hide();
        if (x11_window != 0) owner->x11_->hide(x11_window);
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
    // The notifier goes before the connection whose descriptor it watches.
    delete x11_events_;
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

    // What a Qt window's winId is on this platform: an X11 window on xcb (and,
    // for verification, offscreen), a Wayland surface, an NSView on Cocoa,
    // an HWND on Windows.
    const QString platform = QGuiApplication::platformName();
    const bool offscreen = platform == QLatin1String("offscreen");
    const bool wayland_parent = platform.startsWith(QLatin1String("wayland"));
    std::optional<WindowApi> parent_api;
    if (platform == QLatin1String("xcb") || offscreen) parent_api = WindowApi::x11;
    else if (wayland_parent) parent_api = WindowApi::wayland;
    else if (platform == QLatin1String("cocoa")) parent_api = WindowApi::cocoa;
    else if (platform == QLatin1String("windows")) parent_api = WindowApi::win32;
    const bool embed = parent_api && instance.supports_editor(*parent_api, false);
    // On Wayland an X11 editor is embedded in a window of the host's own on
    // the X server, when there is one.
    const bool xwayland = wayland_parent && !embed &&
                          instance.supports_editor(WindowApi::x11, false) && x11() != nullptr;
    // A floating editor is the plugin's own top-level, in the platform's own
    // windowing. Offscreen there is no screen for it, except an X server the
    // adapter checks for itself.
    const auto native = blokkily::native_window_api();
    const bool floating_possible = !offscreen || native == WindowApi::x11;
    if (!embed && !xwayland && !(floating_possible && instance.supports_editor(native, true)))
        return fail(native == WindowApi::x11 ? QStringLiteral("Plugin window needs an X11 display")
                                             : QStringLiteral("Plugin window needs a display"));

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
        const blokkily::NativeParent parent{*parent_api,
                                            static_cast<std::uintptr_t>(editor->window->winId()),
                                            editor->window->devicePixelRatio()};
        if (!instance.open_editor(&parent, *editor, &size, &reason)) {
            editor->instance = nullptr;
            return fail(QString::fromStdString(reason));
        }
        editor->logical = size.logical;
        if (size.width > 0 && size.height > 0) {
            const auto ratio = size.logical ? 1.0 : editor->window->devicePixelRatio();
            editor->window->resize(to_logical(size.width, ratio), to_logical(size.height, ratio));
        }
        auto* raw = editor.get();
        editor->window->on_close = [this, raw] { (void)close(raw->where); };
        editor->window->show();
    } else if (xwayland) {
        editor->placement = Placement::xwayland;
        editor->x11_window = x11_->create(title.toStdString(), 320, 200);
        if (editor->x11_window == 0) return fail(QStringLiteral("Plugin window needs an X11 display"));
        // The scale Qt draws this screen at; the X server under a Wayland
        // desktop counts the same physical pixels.
        const auto* screen = QGuiApplication::primaryScreen();
        const blokkily::NativeParent parent{WindowApi::x11, editor->x11_window,
                                            screen != nullptr ? screen->devicePixelRatio() : 1.0};
        if (!instance.open_editor(&parent, *editor, &size, &reason)) {
            editor->instance = nullptr;
            return fail(QString::fromStdString(reason));
        }
        if (size.width > 0 && size.height > 0)
            x11_->resize(editor->x11_window, size.width, size.height);
        x11_->show(editor->x11_window);
        // Sync on create may have queued events the descriptor will not
        // announce again.
        QTimer::singleShot(0, this, [this] { serveX11(); });
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
    if (kept->x11_window != 0) x11_->hide(kept->x11_window);
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

std::uintptr_t PluginWindows::x11Window(blokkily::ProcessorAddress where) const {
    for (const auto& editor : editors_)
        if (editor->where == where) return editor->x11_window;
    return 0;
}

X11HostWindows* PluginWindows::x11() {
    if (!x11_tried_) {
        x11_tried_ = true;
        x11_ = X11HostWindows::connect();
        if (x11_) {
            x11_events_ = new QSocketNotifier(x11_->descriptor(), QSocketNotifier::Read, this);
            connect(x11_events_, &QSocketNotifier::activated, this, [this] { serveX11(); });
        }
    }
    return x11_.get();
}

void PluginWindows::serveX11() {
    if (!x11_) return;
    for (const auto window : x11_->take_close_requests()) {
        const auto found = std::find_if(editors_.begin(), editors_.end(),
            [&](const auto& editor) { return editor->x11_window == window; });
        if (found != editors_.end()) (void)close((*found)->where);
    }
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
    const std::function<blokkily::PluginInstance*(blokkily::ProcessorAddress)>& at,
    const std::vector<blokkily::ProcessorAddress>& addresses) {
    bool moved = false;
    std::vector<blokkily::ProcessorAddress> stale;
    for (auto& editor : editors_) {
        if (remap != nullptr && editor->where.kind == blokkily::BusKind::track) {
            const auto bus = editor->where.bus;
            if (bus < remap->size() && (*remap)[bus])
                editor->where.bus = static_cast<std::uint32_t>(*(*remap)[bus]);
        }
        if (at(editor->where) != editor->instance) {
            const auto found = std::find_if(addresses.begin(), addresses.end(),
                [&](auto where) { return at(where) == editor->instance; });
            if (found == addresses.end()) stale.push_back(editor->where);
            else { editor->where = *found; moved = true; }
        }
    }
    for (const auto& where : stale) (void)close(where);
    if (moved) emit changed();
}
