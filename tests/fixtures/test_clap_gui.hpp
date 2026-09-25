#pragma once

// What the CLAP fixture's editor (clap.gui) records, read by the tests and the
// verify scenario through the exported blokkily_test_gui_report(). The editor
// draws nothing: it records the calls the host makes, the window handle it
// was given, and what its timer and descriptor callbacks received, so a host
// can be checked without a display.

namespace blokkily::test_clap_gui {

enum Report : int {
    creates = 0,       // clap_plugin_gui.create calls that succeeded
    destroys,          // clap_plugin_gui.destroy calls
    shows,
    hides,
    set_parents,
    last_parent,       // the X11 window handle of the last set_parent
    last_floating,     // 1 when the last create asked for a floating window
    open_editors,      // editors created and not yet destroyed
    timer_ticks,       // on_timer calls
    fd_events,         // on_fd calls that found a byte to read
    timers_registered, // timers held with the host right now
    fds_registered,    // descriptors watched by the host right now
    width,             // the size of the last editor, in physical pixels
    height,
    scale_percent,     // the last set_scale, times 100
    report_size
};

// The editor's size, as get_size reports it.
constexpr long editor_width = 320;
constexpr long editor_height = 200;

} // namespace blokkily::test_clap_gui
