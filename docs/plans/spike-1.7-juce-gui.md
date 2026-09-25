# Spike 1.7: JUCE GUI code in the host

Status: done (throwaway spike, 2026-09-25). This note is the only output. The
probe code was not committed. It is described under "How it was measured" so
that it can be rebuilt.

**Decision: core links GUI.** Build JUCE once, GUI-capable, in a single
`blokkily_juce` static library. `blokkily_core` links that library instead of
`juce_audio_processors_headless`, and the app links the same library for its
plugin-window code. Do **not** make a separate `blokkily_plugin_gui` library
that compiles its own copy of JUCE. Item 2.6 must also meet the six
conditions under "What 2.6 must do".

Environment: JUCE 9.0.1, Qt 6.11.2, GCC Debug build. The session has
`DISPLAY=:1` (XWayland) and `WAYLAND_DISPLAY=wayland-1`. `/tmp/.X11-unix/X0`
belongs to the `cosmic-greeter` user's X server, so this user cannot connect
to it. The machine has no Xvfb.

## The three answers

### 1. Do the tests stay deterministic with GUI-capable JUCE linked in, with DISPLAY unset and with it set?

**Yes.** Linking GUI-capable JUCE into the host changes nothing that can be
measured.

One precondition applies, and the current test environment already has the
problem it guards against: an empty `DISPLAY` does not isolate JUCE. See
"The `DISPLAY=` trap" below.

| Tree | Test env | Result |
|---|---|---|
| baseline (`master`, headless JUCE in core) | as committed (`DISPLAY=;WAYLAND_DISPLAY=`) | 19/19 |
| A: core links `juce::juce_audio_processors juce::juce_gui_basics` directly | as committed | 19/19 |
| A | `DISPLAY=:1`, `WAYLAND_DISPLAY=wayland-1` inherited (no override) | 19/19, twice (`--repeat until-fail:2`) |
| B: core links `blokkily_juce` (recommended layout) | as committed (`DISPLAY=`) | **4 GUI gates time out at 120 s**. Reproduced alone and 3 times out of 3 (see the trap below) |
| B | `DISPLAY=/nonexistent/blokkily-no-x11:0` | 19/19 |
| B | `DISPLAY=:1` inherited | 19/19 |

**X traffic.** An `LD_PRELOAD` spy logged every `connect()` and `dlopen()`.
strace and gdb could not attach under the sandbox. With `DISPLAY=:1`, the X
connections were **identical** in the baseline tree and in the GUI-linked
tree:

- `blokkily_tests` makes 1 connection to `@/tmp/.X11-unix/X1`.
- The `bdd_edit_once_see_everywhere` gate makes 20.
- `blokkily_scan VST3 …` and `blokkily_scan CLAP …` make none.

**Where the connections come from.** They come from the VST3 fixture, not
from the host:

- The fixture links its own static copy of JUCE through `juce_audio_utils`.
  It has 449 `juce::XWindowSystem` symbols. The baseline host has 0.
- On every instantiation, JUCE's plugin-side `detail::MessageThread::run()`
  (`juce_audio_plugin_client/detail/juce_LinuxMessageThread.h:74`) calls
  `XWindowSystem::getInstance()`, which calls `XOpenDisplay`.
- A backtrace taken at `connect()` shows `XOpenDisplay` called from a thread
  inside `Blokkily Test VST3.so`.

So every test process that instantiates the VST3 fixture already opens X
today. GUI code in the host adds no connections.

**Cost of linking GUI JUCE into the host:**

- **Binary size (Debug):**
  - `blokkily_tests`: 30.6 MB → 103 MB
  - `blokkily_scan`: 24.4 MB → 97 MB
  - `blokkily`: 50.2 MB → 123 MB
  - Every executable that links core grows by about 73 MB.
- **New runtime libraries:** `libfreetype` and `libfontconfig`. libX11 is
  loaded with `dlopen` at first use, not linked.
- **Compile time:** the JUCE objects inside core took about 35 s summed with
  headless JUCE and about 133 s with GUI JUCE (layout A). In layout B,
  `blokkily_juce` took about 87 s with ccache warm. These are sums of
  per-object wall times from `.ninja_log`. The load average was 20–40, so the
  numbers are rough.
- **`-Werror`:** in layout A, core's
  `-Wall -Wextra -Wpedantic -Werror` applies to JUCE's C sources. It fails in
  `juce_graphics/image_formats/jpglib/jcmaster.c:524`
  (`-Werror=implicit-fallthrough`). Layout B avoids this because JUCE compiles
  in its own target, and core keeps `-Werror` unchanged. Layout A would need
  `-Werror` restricted to `$<COMPILE_LANGUAGE:CXX>`. With that restriction,
  the C++ side of the GUI modules builds cleanly.

### 2. Does `ScopedJuceInitialiser_GUI` connect to X?

**No.** Constructing it (probe mode `init`) does none of the following, with
`DISPLAY=:1` or with `DISPLAY` unset:

- no `connect()`
- no `dlopen` of libX11
- no X socket

It only creates the message-queue socketpair: two sockets appear, and neither
is connected. JUCE opens X lazily, at the first `XWindowSystem::getInstance()`.
Measured triggers:

- `juce::Desktop::getInstance().getDisplays()`: 1 connection to
  `@/tmp/.X11-unix/X1`, plus D-Bus (`/run/user/1000/bus`) for dark-mode
  detection.
- `Component::addToDesktop` (creating a peer).
- In a JUCE-built plugin, instantiation itself, through the plugin's own
  message thread (see answer 1).
- Creating a VST3 instance through the host's `VST3PluginFormat`, and scanning
  its descriptions, do not open X on the host side. The host's JUCE opened X
  only through `getDisplays()` or a peer.

### 3. Can a JUCE editor be embedded under an offscreen fake `winId`? Can it float under XWayland?

It must be **refused**. Embedding under a fake `winId` kills the process. On a
real X server it works, both floating and embedded.

The probe hosts the suite's VST3 fixture through `VST3PluginFormat` and calls
`createEditorIfNeeded()`. For the spike, the fixture was given an editor that
fills 320×200 with `#C8FF3C`.

| Case | Result |
|---|---|
| Qt `offscreen`, `DISPLAY=:1`, `editor->addToDesktop(0, (void*) window->winId())` (`winId` = `0x1`) | **Process exits with code 1.** Xlib's default handler prints `BadWindow (invalid Window parameter)`, `X_CreateWindow`, resource `0x1`, and calls `exit(1)`. Stack: `LinuxComponentPeer` → `XWindowSystem::createWindow` → `setWindowType` → `XInternAtom` → `_XReply` → `_XError` → `exit`. |
| Qt `wayland`, `DISPLAY=:1`, same call (`winId` is a pointer, `0x…b7fc4560`) | **Same: exits with code 1** (`BadWindow`, `X_CreateWindow`). A Wayland `winId` is not an X window either. |
| Qt `offscreen`, no `DISPLAY` | Creating the editor succeeds. `addToDesktop` then **segfaults** (code 139) in `juce::XEmbedComponent::Pimpl::peerChanged`, reached from `VST3PluginWindow::attachPluginWindow`. Before the crash, JUCE fired `juce_Messaging_linux.cpp:87` (message queue overloaded) 64 times. |
| No Qt, no `DISPLAY`, floating `addToDesktop(windowHasTitleBar)` | **Segfaults** the same way. |
| Guarded: refuse when `QGuiApplication::platformName() != "xcb"` (offscreen, `DISPLAY=:1`) | Prints `REFUSED: Plugin window needs an X11 display`, exits 0, and makes no X connection from the host side. |
| **Floating top-level under XWayland** (`DISPLAY=:1`, no Qt window) | Works. The peer is X window `0xa00005`, 320×200, `_NET_WM_WINDOW_TYPE_NORMAL`. It stayed alive through 4 s of JUCE dispatch and tore down cleanly. `import -window` grab: 320×200, a single colour, centre `#C8FF3C`. I inspected the PNG: the whole image is solid lime `#C8FF3C`, with no border or artefacts. |
| **Embedded in a real Qt `xcb` window** (`QT_QPA_PLATFORM=xcb`, `DISPLAY=:1`, `winId` `0x800009`) | Works. The JUCE peer is `0xc00005`, reparented into the Qt window, and the grab shows it rendered. Qt window grab: 560×350 at device pixel ratio 1.75. I inspected the PNG: the 320×200 lime editor fills the top-left, and the rest of the unpainted `QWindow` is black. The editor is **not** scaled by Qt's device pixel ratio. |

Both layouts A and B gave the same results for floating and for `xcb`
embedding.

## The `DISPLAY=` trap (must fix; the current env already has it)

- **The fallback.** When `DISPLAY` is empty or unset, JUCE substitutes
  `":0.0"` (`juce_XWindowSystem_linux.cpp:3379`) and tries `XOpenDisplay`
  twice. This affects the host's copy of JUCE and every JUCE plugin's copy.
- **What that does on this machine.** `:0` is another user's X server.
  - libxcb tries `@/tmp/.X11-unix/X0` (`ECONNREFUSED`), then
    `/tmp/.X11-unix/X0` (`EACCES`), then TCP `[::1]:6000` and
    `127.0.0.1:6000`.
  - The TCP attempts sometimes fail at once (`ECONNREFUSED`, in the baseline
    gate process). Sometimes each one takes 7.2 s to fail with `ETIMEDOUT`
    (a bare `XOpenDisplay(":0.0")` test program; the layout B gate process).
    This happened on the same machine within the same minute.
  - I did not find out why the TCP behaviour differs from one process to
    another. It was reproducible for each binary.
  - A slow attempt takes 14.3 s, so JUCE's two attempts take 28.7 s.
- **How that hangs a gate.** The plugin's `MessageThread::start()` waits up to
  10 s for `run()` to finish its `XWindowSystem::getInstance()` call.
  `~MessageThread` calls `stopThread(-1)`, which waits without limit. So one
  VST3 instantiation or teardown can stall the audio/model thread for tens of
  seconds. That is why the layout B gates timed out.
- **Other values tried:**
  - `DISPLAY=:65535` behaves the same (14.3 s per attempt).
  - `DISPLAY=/nonexistent/blokkily-no-x11:0` makes no connection of any kind
    and fails in 0.00 s. The suite passes with it in every layout.
  - With `DISPLAY=blokkily-no-display:0`, the host name has to be resolved
    first, so that value depends on DNS.

**Fix.** Set `DISPLAY` to a non-empty socket path that does not exist, rather
than to the empty string. The fix does not depend on this spike's decision,
because the baseline is exposed too: its gates open `:0` 40 times per run with
`DISPLAY=`.

## What 2.6 must do

1. **Choose the window mode before creating the editor.**
   - **Embed** only when `QGuiApplication::platformName() == "xcb"`.
   - Otherwise, **float** the editor as a JUCE top-level. That requires a
     non-empty `DISPLAY` that the host itself has opened successfully, for
     example with `xcb_connect(getenv("DISPLAY"))`. Never let JUCE resolve an
     empty `DISPLAY`.
   - Otherwise, **refuse** with "Plugin window needs an X11 display". Refusal
     is the only safe outcome: embedding into an offscreen or Wayland
     `winId` exits the process, and any editor peer without X segfaults.
   - This session's Qt runs on Wayland (`QT_QPA_PLATFORM=wayland;xcb`), so
     here VST3 editors will float through XWayland unless the app forces
     `xcb`.
2. **Install an Xlib error handler in the host.** JUCE installs its handlers,
   and calls `XInitThreads`, only when `JUCEApplicationBase::isStandaloneApp()`
   is true (`juce_XWindowSystem_linux.cpp:1589-1608`). That is never the case
   in a Qt host. Without our own handler, any X error, such as a stale parent
   or a plugin misbehaving, runs Xlib's default handler, which calls `exit(1)`.
   The handler has to be installed through the same `libX11.so.6` that JUCE
   loads with `dlopen`.
3. **Plugin messages arrive on the plugin's own thread.** A JUCE-built VST3
   runs its own message thread. The plugin's `IRunLoop` hook registers through
   `ScopedRunLoop`, but the thread exists from the moment of instantiation.
   Parameter changes from the editor therefore arrive on a foreign thread,
   which is why the plan's tests need bounded polling. The host still has to
   pump its own JUCE queue (the `LinuxEventLoop` bridge). Otherwise
   `juce_Messaging_linux.cpp:87` fires once the queue is full.
4. **Size in physical pixels.** At device pixel ratio 1.75, a 320×200 editor
   covers 320×200 device pixels of a `QWindow` whose logical size is 320×200.
   Size the host window from the editor's physical size divided by the device
   pixel ratio, or scale the editor.
5. **Keep JUCE configuration macros `PUBLIC` on `blokkily_juce`.** A consumer
   that defines them differently gets link errors or ODR violations. Measured:
   `JUCE_MODAL_LOOPS_PERMITTED=1` in the consumer only gave
   `undefined reference to juce::MessageManager::runDispatchLoopUntil(int)`.
   Production code should not need modal loops.
6. **The adapter must use `VST3PluginFormat`, not the headless format.**
   `VST3PluginInstanceHeadless::createEditor()` returns `nullptr`
   (`juce_VST3PluginFormatImpl.h:2751`). Only an instance created by the
   GUI-capable `VST3PluginFormat` can create an editor, which is why the
   adapter in core has to be compiled against GUI-capable JUCE.

## Why not a separate `blokkily_plugin_gui` library

- **The instance-creating code has to be GUI-capable.** The editor needs an
  instance made by `VST3PluginFormat`. That instance is created in
  `vst3_instance.cpp`, inside core. A separate GUI library would either
  duplicate instance creation or make core expose JUCE types.
- **Two copies of JUCE in one process.** JUCE module targets compile their
  sources into every consumer. A headless-JUCE `blokkily_core` plus a
  GUI-JUCE `blokkily_plugin_gui` would put two differently configured copies
  of `juce_core` and `juce_events` in the same executable: two
  `MessageManager` singletons and ODR violations. Avoiding that needs a
  single JUCE target anyway, which is the recommended layout.
- **The isolation it would buy does not exist.** Test and scan processes
  already contain GUI-capable JUCE through the VST3 fixture and open X through
  it. With `DISPLAY` isolated as above, nothing in the host touches X unless a
  plugin window is requested.
- **What 2.6 can still do.** Keep its window code in its own source files
  (`src/app/plugin_windows.*`) in the app target.

## Exact CMake and link lines

In `CMakeLists.txt`, replacing
`target_link_libraries(blokkily_core PRIVATE juce::juce_audio_processors_headless)`
and the `JUCE_PLUGINHOST_*` block on `blokkily_core`. Layout B was tested
exactly like this, except that the spike also defined
`JUCE_MODAL_LOOPS_PERMITTED=1` for its probe.

```cmake
# JUCE is compiled once, GUI-capable, here and nowhere else in the host.
# Everything that needs JUCE links this target; nothing else links juce::
# module targets (the VST3 test fixture keeps its own copy, as a plugin must).
add_library(blokkily_juce STATIC)
target_link_libraries(blokkily_juce
    PRIVATE juce::juce_audio_processors juce::juce_gui_basics
    PUBLIC  juce::juce_recommended_config_flags)
target_compile_definitions(blokkily_juce PUBLIC
    JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=0 JUCE_PLUGINHOST_LV2=0
    JUCE_PLUGINHOST_VST=0 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
# Consumers must see JUCE's module-availability macros and include paths.
target_compile_definitions(blokkily_juce INTERFACE
    $<TARGET_PROPERTY:blokkily_juce,COMPILE_DEFINITIONS>)
target_include_directories(blokkily_juce INTERFACE
    $<TARGET_PROPERTY:blokkily_juce,INCLUDE_DIRECTORIES>)

target_link_libraries(blokkily_core PRIVATE blokkily_juce)
# blokkily_core keeps -Wall -Wextra -Wpedantic -Werror unchanged: JUCE's C
# sources are compiled in blokkily_juce, not in core.
```

In 2.6's fragment `cmake/feature_plugin_windows.cmake`, for the app-side
window code that includes JUCE headers:

```cmake
target_link_libraries(blokkily PRIVATE blokkily_juce)
```

In the adapter, `src/plugins/vst3_instance.cpp`: include
`<juce_audio_processors/juce_audio_processors.h>` and use
`juce::VST3PluginFormat` wherever `juce::VST3PluginFormatHeadless` is used now.
Scan, instantiation and the whole suite pass with this change.

Test isolation, replacing the current `BLOKKILY_HEADLESS_TEST_ENV` (tested:
19/19 in layout B; the device checks keep the real environment as before):

```cmake
set(BLOKKILY_HEADLESS_TEST_ENV
    "ALSA_CONFIG_PATH=/dev/null;DISPLAY=/nonexistent/blokkily-no-x11:0;WAYLAND_DISPLAY=")
```

The fixture editor tested for 2.6's pixel check (`tests/fixtures/test_vst3.cpp`):

```cpp
struct Editor final : juce::AudioProcessorEditor {
    explicit Editor(juce::AudioProcessor& p) : AudioProcessorEditor(p) { setSize(320, 200); }
    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xffc8ff3c)); }
};
bool hasEditor() const override { return true; }
juce::AudioProcessorEditor* createEditor() override { return new Editor(*this); }
```

## How it was measured (not committed)

- **Build variants** from spike CMake options:
  - Layout A: `BLOKKILY_SPIKE_JUCE_GUI`, core links the modules directly.
  - Layout B: `BLOKKILY_SPIKE_JUCE_LIB`, `blokkily_juce`.
  - `BLOKKILY_SPIKE_KEEP_DISPLAY`: drops `DISPLAY=`/`WAYLAND_DISPLAY=` from
    the test environment.
  - `BLOKKILY_SPIKE_DISPLAY_PATH`: sets `DISPLAY` to the nonexistent socket
    path.
- **Probe** (`spike_juce_gui_probe`): Qt 6 Gui plus JUCE. Its modes:
  - `init` and `displays`: construct the initialiser, then optionally call
    `getDisplays()`.
  - `scan`: find and instantiate the VST3 fixture.
  - `editor-offscreen`: `addToDesktop(0, winId)` under whatever
    `QT_QPA_PLATFORM` is set.
  - `editor-guarded`: refuse when the platform is not `xcb`.
  - `editor-floating`: a JUCE top-level window.
  - `editor-xcb`: a shown Qt `xcb` window with the editor embedded.

  Each mode reports its socket count and whether libX11 is mapped. The
  editor modes pump JUCE and Qt for 4 s, then tear down.
- **`LD_PRELOAD` spy.** It logs `connect()` targets and timings and
  `dlopen()` calls. On `exit(≠0)` or `SIGSEGV` it prints a backtrace, and
  frames are symbolised with `addr2line`.
- **Pixel grabs.** `import -window <id>` from ImageMagick. The centre pixel
  was read with `magick … '%[hex:p{w/2,h/2}]'`. I opened and inspected every
  PNG.
- **Not tested:**
  - Xvfb (not installed).
  - A third-party JUCE or non-JUCE VST3 editor.
  - CLAP GUI (out of scope).
  - Keyboard focus and XEmbed focus hand-off.
  - Resizing.
  - Release builds.
  - Why loopback TCP to port 6000 sometimes times out rather than being
    refused.
