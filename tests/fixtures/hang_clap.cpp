#include <clap/clap.h>

#include <chrono>
#include <thread>

// A plugin that never returns from its entry point. Real ones exist: a bridged
// 32-bit plugin whose helper process fails to start leaves the host waiting on
// a handshake that never arrives, and an in-process scan then waits forever.
namespace {
bool entry_init(const char*) {
    while (true) std::this_thread::sleep_for(std::chrono::seconds(1));
}
void entry_deinit() {}
const void* get_factory(const char*) { return nullptr; }
} // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry{
    CLAP_VERSION, entry_init, entry_deinit, get_factory};
