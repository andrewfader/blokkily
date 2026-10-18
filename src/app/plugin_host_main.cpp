#include "blokkily/plugins/sandbox.hpp"

// Runs one plugin for the application, in a process of its own
// (features/plugin_isolation.feature): a plugin that crashes or hangs here
// costs this helper, never the window. The application starts it with the
// shared memory and the socket it serves; it is not meant to be run by hand.
int main(int argc, char* argv[]) { return blokkily::run_sandbox_host(argc, argv); }
