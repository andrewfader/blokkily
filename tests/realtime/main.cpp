// blokkily_realtime_checks: proves that what runs on the audio thread keeps
// the real-time rules, starting with "does not allocate". Run with a case name
// to run that case, or with --list to print every registered case.

#include "realtime_case.hpp"

#include <array>
#include <cstring>
#include <exception>

namespace blokkily::realtime {
namespace {

// Fixed storage, so registering needs no allocation and no initialisation
// order between translation units: zero-initialised static data is ready
// before any constructor runs.
constexpr std::size_t maximum_cases = 64;
std::array<Case, maximum_cases> table{};
std::size_t registered = 0;
bool overflowed = false;

} // namespace

std::span<const Case> cases() noexcept { return {table.data(), registered}; }

Registration::Registration(const char* name, CaseFunction run) noexcept {
    if (registered == maximum_cases) {
        overflowed = true;
        return;
    }
    table[registered++] = {name, run};
}

} // namespace blokkily::realtime

int main(int argc, char** argv) {
    using namespace blokkily::realtime;
    if (overflowed) {
        std::fprintf(stderr, "REALTIME FAIL: more cases than the table holds\n");
        return 1;
    }
    if (argc == 2 && std::strcmp(argv[1], "--list") == 0) {
        for (const auto& entry : cases()) std::printf("%s\n", entry.name);
        return 0;
    }
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <case> | --list\n", argv[0]);
        return 2;
    }
    for (const auto& entry : cases()) {
        if (std::strcmp(entry.name, argv[1]) != 0) continue;
        try {
            entry.run();
        } catch (const std::exception& error) {
            std::fprintf(stderr, "REALTIME FAIL %s: %s\n", entry.name, error.what());
            return 1;
        }
        std::printf("realtime %s: pass\n", entry.name);
        return 0;
    }
    std::fprintf(stderr, "REALTIME FAIL: no case named '%s' (see --list)\n", argv[1]);
    return 1;
}
