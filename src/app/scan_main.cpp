#include "blokkily/plugins/plugin_scan.hpp"

#include <iostream>
#include <string>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

// Describes a single plugin and prints what it holds. The host runs this as a
// child process with a deadline: a plugin that hangs, crashes, or refuses to
// load costs one helper process, never the window.
int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "usage: blokkily_scan <CLAP|VST3> <path>\n";
        return 2;
    }
#if defined(_WIN32)
    // The records are bytes the host parses, not text: in Windows' default
    // text mode every newline would reach it as "\r\n".
    (void)_setmode(_fileno(stdout), _O_BINARY);
#endif
    std::string error;
    const auto records = blokkily::scan_candidate({argv[1], std::filesystem::path(argv[2])},
                                                  &error);
    if (!error.empty()) {
        std::cerr << error << '\n';
        return 1;
    }
    std::cout << blokkily::write_scan_records(records) << std::flush;
    return 0;
}
