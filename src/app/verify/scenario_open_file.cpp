// features/open_from_desktop.feature: a .blok handed to the executable, as a
// file manager does through the desktop entry, is the song the window opens
// with. open_file_setup writes the song; open_file and open_file_missing are
// started with a path and look at what they opened.
#include "verify/harness.hpp"
#include "song_model.hpp"

#include <QCoreApplication>

#include <iostream>
#include <source_location>

namespace blokkily::verify {
namespace {

constexpr const char* opened_name = "OPENED FROM FILE";

void run_open_file_setup(VerifyContext& ctx) {
    auto& s = ctx.song;
    auto song = s.song();
    song.tracks[0].name = opened_name;
    blokkily::Track second;
    second.name = "SECOND TRACK";
    song.tracks.push_back(second);
    song.add_track_parts(song.tracks.size() - 1);
    s.replace(std::move(song));
    ctx.check(s.trackCount() >= 2);
    ctx.check(ctx.controller.saveProject(ctx.parser.value("project")));
    ctx.reached("open file: a song with its own tracks is saved");
    ctx.finish("wrote the project the launch gates open");
}

void run_open_file(VerifyContext& ctx) {
    auto& c = ctx.controller;
    auto& s = ctx.song;
    const auto check = [&](bool ok, std::source_location loc = std::source_location::current()) {
        if (!ok) std::cerr << "open file failed at " << loc.line() << '\n';
        ctx.check(ok);
    };
    QCoreApplication::processEvents();
    // The song in the window is the file's, not a new one, and opening it
    // left nothing unsaved.
    check(!s.song().tracks.empty() && s.song().tracks[0].name == opened_name);
    check(s.song().tracks.size() >= 2 && s.song().tracks.back().name == "SECOND TRACK");
    std::cerr << "project " << c.projectName().toStdString() << " · "
              << c.projectStatus().toStdString() << '\n';
    check(c.projectName() == "opened song");
    check(!s.dirty());
    // The window says so: the rail's project card and the mixer's strip.
    (void)ctx.window->grabWindow();
    check(VerifyContext::usable(ctx.named("projectCard"), 120, 30));
    ctx.reached("open file: launched with a .blok, the window holds that song");
    check(ctx.save_screenshot());
    ctx.finish("a project handed to the executable opens at launch");
}

void run_open_file_missing(VerifyContext& ctx) {
    auto& c = ctx.controller;
    QCoreApplication::processEvents();
    // A path that is not there still starts the program, with a new song,
    // and the window says why the file did not open.
    ctx.check(c.projectStatus() == "Load failed");
    ctx.check(c.projectName() == "Untitled");
    ctx.check(!ctx.song.song().tracks.empty() && ctx.song.song().tracks[0].name != opened_name);
    ctx.reached("open file: a missing file leaves a new song and says so");
    ctx.finish("a missing launch file is reported, not fatal");
}

[[maybe_unused]] const bool setup = register_scenario("open_file_setup", run_open_file_setup);
[[maybe_unused]] const bool opened = register_scenario("open_file", run_open_file);
[[maybe_unused]] const bool missing = register_scenario("open_file_missing", run_open_file_missing);

} // namespace
} // namespace blokkily::verify
