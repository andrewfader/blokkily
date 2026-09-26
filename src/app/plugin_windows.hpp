#pragma once

// The windows plugin editors live in (item 2.6). One editor per processor
// address; the window is the host's side of the editor, the EditorHost the
// adapter talks to.
//
// Where an editor goes is decided before it is created
// (docs/plans/spike-1.7-juce-gui.md, "What 2.6 must do"):
//  - On the xcb platform it is embedded in a Qt window made for it, whose
//    winId is a real X11 window.
//  - On the offscreen platform (verification) the window is virtual and its
//    winId is not a real window. It is still offered as an X11 parent: an
//    adapter that needs a real display to embed (the VST3 adapter, through
//    JUCE) refuses it with "Plugin window needs an X11 display", and one that
//    only records it (the CLAP fixture) embeds.
//  - On Wayland a CLAP editor that supports the Wayland API is embedded in a
//    Qt Wayland surface. An X11-only editor floats as the plugin's own X11
//    top-level through XWayland when it can; otherwise it is refused.
//
// The editor sizes itself in physical pixels, so the host window is made
// that size divided by the device pixel ratio. An editor whose instance is
// destroyed (an instrument swapped, a project loaded) is told by the adapter,
// and its window goes with it; one whose instance was adopted into a rebuilt
// engine keeps its window.

#include "blokkily/model/processor_address.hpp"
#include "blokkily/plugins/plugin.hpp"

#include <QObject>
#include <QString>

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

class QWindow;

class PluginWindows final : public QObject {
    Q_OBJECT
public:
    enum class Placement { embedded, floating };

    explicit PluginWindows(QObject* parent = nullptr);
    // Closes every editor that is still open.
    ~PluginWindows() override;

    // Opens `instance`'s editor for the processor at `where`, in a window
    // titled `title`. False, with the reason in `error`, when the plugin has
    // no editor, the platform cannot show it, or the plugin refuses.
    bool open(blokkily::ProcessorAddress where, blokkily::PluginInstance& instance,
              const QString& title, QString* error);
    // Closes the editor at `where`; false when none is open there.
    bool close(blokkily::ProcessorAddress where);
    void closeAll();

    [[nodiscard]] bool isOpen(blokkily::ProcessorAddress where) const;
    [[nodiscard]] std::size_t count() const noexcept { return editors_.size(); }
    [[nodiscard]] std::vector<blokkily::ProcessorAddress> openAddresses() const;
    // The Qt window an embedded editor lives in; nullptr for a floating one.
    [[nodiscard]] QWindow* window(blokkily::ProcessorAddress where) const;
    [[nodiscard]] std::optional<Placement> placement(blokkily::ProcessorAddress where) const;
    [[nodiscard]] blokkily::PluginInstance* instance(blokkily::ProcessorAddress where) const;
    // How many times an editor at `where` has asked its window to resize.
    [[nodiscard]] int resizeRequests(blokkily::ProcessorAddress where) const;

    // After the audio graph was rebuilt: each editor follows its track to
    // where `remap` moved it (entry i is old track i's new index), and one
    // whose address no longer holds its instance is closed.
    void reconcile(const std::vector<std::optional<std::size_t>>* remap,
                   const std::function<blokkily::PluginInstance*(blokkily::ProcessorAddress)>& at,
                   const std::vector<blokkily::ProcessorAddress>& addresses = {});

signals:
    // An editor opened or closed, by the host or by the plugin itself.
    void changed();

private:
    struct Editor;
    // Drops an editor's entry and its window, without calling the instance.
    void forget(Editor* editor);
    std::vector<std::unique_ptr<Editor>> editors_;
    // Editors the plugin closed, kept until the adapter has returned.
    std::vector<std::unique_ptr<Editor>> retired_;
};
