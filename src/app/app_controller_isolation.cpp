// Plugin isolation (features/plugin_isolation.feature): each plugin in a
// helper process of its own, on unless switched off in SETTINGS; a plugin
// that crashes, hangs or throws is named until RELOAD loads it again.

#include "app_controller.hpp"
#include "processor_factory.hpp"

#include "blokkily/plugins/sandbox.hpp"

#include <QCoreApplication>
#include <QFileInfo>

namespace {
constexpr auto isolation_key = "plugins/isolate";

QString helperPath() {
#if defined(_WIN32)
    return QCoreApplication::applicationDirPath() + "/blokkily_plugin_host.exe";
#else
    return QCoreApplication::applicationDirPath() + "/blokkily_plugin_host";
#endif
}
} // namespace

bool AppController::pluginIsolationAvailable() const {
    return blokkily::sandbox_supported() && QFileInfo(helperPath()).isExecutable();
}

// On unless switched off; a gate's deterministic device loads plugins in the
// window unless the gate asks otherwise (--isolate-plugins).
bool AppController::pluginIsolation() const {
    return pluginIsolationAvailable() && setting(isolation_key, !deterministicAudio()).toBool();
}

void AppController::applyPluginIsolation() {
    blokkily::set_plugin_isolation(pluginIsolation()
                                       ? std::filesystem::path(helperPath().toStdString())
                                       : std::filesystem::path{});
}

void AppController::setPluginIsolation(bool on) {
    if (on == pluginIsolation() && setting(isolation_key).isValid()) return;
    storeSetting(isolation_key, on);
    applyPluginIsolation();
    emit pluginIsolationChanged();
    if (!engine_) return;
    reload_all_ = true;
    (void)rebuildEngine();
    status_ = on ? QStringLiteral("Plugins run in processes of their own")
                 : QStringLiteral("Plugins run inside Blokkily");
    emit statusChanged();
}

void AppController::reloadPlugins() {
    if (!engine_) return;
    reload_failed_ = true;
    (void)rebuildEngine();
    failed_plugins_.clear();
    emit failedPluginsChanged();
    reportFailedPlugins();
    status_ = failed_plugins_.isEmpty() ? QStringLiteral("Plugins loaded again")
                                        : QStringLiteral("A plugin would not load again");
    emit statusChanged();
}
