cmake_minimum_required(VERSION 3.25)
project(blokkily VERSION 0.1.0 LANGUAGES C CXX)

# Rebuilds across many worktrees are dominated by JUCE and Qt, so use ccache
# whenever it is installed; it is never required. An explicit launcher given
# on the command line wins.
find_program(BLOKKILY_CCACHE ccache)
if(BLOKKILY_CCACHE)
    if(NOT CMAKE_C_COMPILER_LAUNCHER)
        set(CMAKE_C_COMPILER_LAUNCHER "${BLOKKILY_CCACHE}")
    endif()
    if(NOT CMAKE_CXX_COMPILER_LAUNCHER)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${BLOKKILY_CCACHE}")
    endif()
endif()

add_subdirectory(third_party/JUCE EXCLUDE_FROM_ALL)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FLUIDSYNTH REQUIRED IMPORTED_TARGET fluidsynth)
pkg_check_modules(RTAUDIO REQUIRED IMPORTED_TARGET rtaudio)
pkg_check_modules(RTMIDI REQUIRED IMPORTED_TARGET rtmidi)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BLOKKILY_BUILD_TESTS "Build unit tests" ON)
option(BLOKKILY_BUILD_GUI "Build the Qt Quick prototype" ON)
option(BLOKKILY_COVERAGE "Instrument application sources for gcov coverage" OFF)

add_library(blokkily_core
    src/model/pattern.cpp
    src/model/song.cpp
    src/model/tuning.cpp
    src/model/scale.cpp
    src/model/keyboard.cpp
    src/project/project.cpp
    src/project/paths.cpp
    src/project/record_io.cpp
    src/project/records_core.cpp
    src/plugins/clap_catalog.cpp
    src/plugins/clap_instance.cpp
    src/plugins/plugin_scan.cpp
    src/instruments/soundfont_catalog.cpp
    src/instruments/soundfont_synth.cpp
    src/audio/playback.cpp
    src/audio/event_timeline.cpp
    src/audio/mixer.cpp
    src/audio/song_engine.cpp
    src/audio/wave_file.cpp
    src/audio/bounce.cpp
    src/plugins/vst3_instance.cpp
    src/sequencer/scheduler.cpp
    src/sequencer/take.cpp
    src/midi/midi_input.cpp
)
target_include_directories(blokkily_core PUBLIC include PRIVATE third_party/clap/include)
target_link_libraries(blokkily_core PRIVATE ${CMAKE_DL_LIBS})
target_link_libraries(blokkily_core PUBLIC PkgConfig::FLUIDSYNTH)
target_link_libraries(blokkily_core PUBLIC PkgConfig::RTAUDIO)
target_link_libraries(blokkily_core PRIVATE PkgConfig::RTMIDI)
# JUCE is compiled once, GUI-capable, here and nowhere else in the host
# (docs/plans/spike-1.7-juce-gui.md, "core links GUI"). Everything that needs
# JUCE links this target; nothing else links juce:: module targets (the VST3
# test fixtures keep their own copies, as a plugin must). A VST3 editor can
# only be made by an instance the GUI-capable VST3PluginFormat created, which
# is why the adapter in core is compiled against it. JUCE's C sources compile
# here, so blokkily_core keeps -Werror unchanged.
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
target_compile_options(blokkily_core PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>
)

add_executable(blokkily_cli src/app/main.cpp)
target_link_libraries(blokkily_cli PRIVATE blokkily_core)

# Describing a plugin means running its code, so the application never does it
# itself: this helper scans one plugin per process, under the host's deadline.
add_executable(blokkily_scan src/app/scan_main.cpp)
target_link_libraries(blokkily_scan PRIVATE blokkily_core)

if(BLOKKILY_BUILD_GUI)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Quick QuickControls2 QuickDialogs2 QuickShapes Network)
    qt_standard_project_setup(REQUIRES 6.5)
    qt_policy(SET QTP0004 NEW)
    qt_add_executable(blokkily
        src/app/gui_main.cpp
        src/app/pattern_model.cpp
        src/app/pattern_model.hpp
        src/app/transport.cpp
        src/app/transport.hpp
        src/app/app_controller.cpp
        src/app/app_controller_scan.cpp
        src/app/app_controller_take.cpp
        src/app/app_controller.hpp
        src/app/app_controller_internal.hpp
        src/app/song_model.cpp
        src/app/song_model_arrangement.cpp
        src/app/song_model_history.cpp
        src/app/song_model_mixer.cpp
        src/app/song_model_tuning.cpp
        src/app/song_model.hpp
        src/app/keyboard_model.cpp
        src/app/keyboard_model.hpp
        src/app/verify/harness.cpp
        src/app/verify/harness.hpp
        src/app/verify/scenario_default.cpp
        src/app/verify/scenario_midi.cpp
    )
    target_include_directories(blokkily PRIVATE src/app)
    # The window and the panels it is built from. They are served from the
    # module's own directory, beside its qmldir, so the files find one another
    # and the Theme singleton without importing their own module.
    set(BLOKKILY_QML_FILES
        ui/Main.qml
        ui/Theme.qml
        ui/Format.js
        ui/SectionLabel.qml
        ui/Chip.qml
        ui/Picker.qml
        ui/ValueDial.qml
        ui/TransportBar.qml
        ui/InstrumentPanel.qml
        ui/ProjectPanel.qml
        ui/ArrangementView.qml
        ui/StepEditors.qml
        ui/KeyboardPanel.qml
        ui/Inspector.qml
        ui/MixerPanel.qml
        ui/MixerStrip.qml
    )
    foreach(qml_file IN LISTS BLOKKILY_QML_FILES)
        get_filename_component(qml_name "${qml_file}" NAME)
        set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_name}")
    endforeach()
    set_source_files_properties(ui/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
    qt_add_qml_module(blokkily
        URI Blokkily
        VERSION 1.0
        QML_FILES ${BLOKKILY_QML_FILES}
    )
    target_link_libraries(blokkily PRIVATE blokkily_core
        Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::QuickDialogs2
        Qt6::QuickShapes)
    # The application looks for the scan helper beside itself.
    add_dependencies(blokkily blokkily_scan)
endif()

# Headless tests and offscreen gates must not reach whatever display the
# developer's session has: once JUCE GUI code is linked in, a stray DISPLAY
# would make them depend on it. The device checks keep the real environment.
# DISPLAY is a socket path that does not exist rather than empty: JUCE (the
# host's copy and every JUCE plugin's) replaces an empty DISPLAY with ":0.0",
# which on a machine with another user's X server can take ~29 s per VST3
# instantiation to fail (docs/plans/spike-1.7-juce-gui.md, "The DISPLAY= trap").
set(BLOKKILY_HEADLESS_TEST_ENV
    "ALSA_CONFIG_PATH=/dev/null;DISPLAY=/nonexistent/blokkily-no-x11:0;WAYLAND_DISPLAY=")
set(BLOKKILY_OFFSCREEN_GATE_ENV
    "QT_QPA_PLATFORM=offscreen;QSG_RHI_BACKEND=software;${BLOKKILY_HEADLESS_TEST_ENV}")

if(BLOKKILY_BUILD_TESTS)
    enable_testing()
    # Both CLAP fixtures share one directory so that a scan of it meets a
    # working plugin and a hanging one, the way an installation directory does.
    set(BLOKKILY_CLAP_FIXTURE_DIR "${CMAKE_BINARY_DIR}/clap-fixtures")
    add_library(blokkily_test_clap MODULE tests/fixtures/test_clap.cpp)
    target_include_directories(blokkily_test_clap PRIVATE third_party/clap/include)
    set_target_properties(blokkily_test_clap PROPERTIES
        PREFIX "" SUFFIX ".clap" OUTPUT_NAME "blokkily-test"
        LIBRARY_OUTPUT_DIRECTORY "${BLOKKILY_CLAP_FIXTURE_DIR}"
    )
    # A plugin whose entry point never returns, standing in for the bridged
    # plugins that leave a host waiting on a handshake that never completes.
    add_library(blokkily_hang_clap MODULE tests/fixtures/hang_clap.cpp)
    target_include_directories(blokkily_hang_clap PRIVATE third_party/clap/include)
    set_target_properties(blokkily_hang_clap PROPERTIES
        PREFIX "" SUFFIX ".clap" OUTPUT_NAME "blokkily-hang"
        LIBRARY_OUTPUT_DIRECTORY "${BLOKKILY_CLAP_FIXTURE_DIR}"
    )
    juce_add_plugin(blokkily_test_vst3
        COMPANY_NAME Blokkily
        PLUGIN_MANUFACTURER_CODE Blok
        PLUGIN_CODE Btst
        FORMATS VST3
        PRODUCT_NAME "Blokkily Test VST3"
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        VST3_CATEGORIES Instrument Synth)
    target_sources(blokkily_test_vst3 PRIVATE tests/fixtures/test_vst3.cpp)
    target_compile_definitions(blokkily_test_vst3 PRIVATE JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(blokkily_test_vst3 PRIVATE
        juce::juce_audio_utils juce::juce_recommended_config_flags)
    add_executable(blokkily_tests tests/core_tests.cpp)
    target_link_libraries(blokkily_tests PRIVATE blokkily_core)
    target_compile_definitions(blokkily_tests PRIVATE
        BLOKKILY_TEST_CLAP_PATH="$<TARGET_FILE:blokkily_test_clap>"
        BLOKKILY_TEST_VST3_PATH="$<TARGET_FILE_DIR:blokkily_test_vst3_VST3>/../.."
        BLOKKILY_TEST_ARTIFACTS="${CMAKE_BINARY_DIR}/artifacts"
    )
    add_dependencies(blokkily_tests blokkily_test_clap blokkily_test_vst3_VST3)
    add_test(NAME blokkily_core COMMAND blokkily_tests)
    set_tests_properties(blokkily_core PROPERTIES
        LABELS "unit;integration;soundfont;clap;vst3;audio;realtime;project;parameters;mixer;song;export"
        ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 60)

    # The project file policy (plan F-F): old formats open, saves are stable,
    # and paths inside the project folder are stored relative to it.
    add_executable(blokkily_project_format_tests tests/project_format_tests.cpp)
    target_link_libraries(blokkily_project_format_tests PRIVATE blokkily_core)
    add_test(NAME project_format COMMAND blokkily_project_format_tests)
    set_tests_properties(project_format PROPERTIES
        LABELS "unit;bdd;project"
        ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 30)

    add_executable(blokkily_audio_regressions tests/audio_regressions.cpp)
    target_link_libraries(blokkily_audio_regressions PRIVATE blokkily_core)
    target_compile_definitions(blokkily_audio_regressions PRIVATE
        BLOKKILY_TEST_CLAP_PATH="$<TARGET_FILE:blokkily_test_clap>"
        BLOKKILY_TEST_VST3_PATH="$<TARGET_FILE_DIR:blokkily_test_vst3_VST3>/../..")
    add_dependencies(blokkily_audio_regressions blokkily_test_clap blokkily_test_vst3_VST3)
    foreach(regression live_order dense_song dense_preview large_preview invalid_timing density_limit)
        add_test(NAME audio_${regression} COMMAND blokkily_audio_regressions ${regression})
        set_tests_properties(audio_${regression} PROPERTIES
            LABELS "bdd;integration;audio;realtime;regression"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 30)
    endforeach()

    # Plays through whatever audio server is actually running — PipeWire, plain
    # ALSA, PulseAudio, JACK — rather than through the deterministic pump every
    # other audio test uses. It skips itself where there is no server, so the
    # suite stays runnable without sound hardware; it does not get
    # ALSA_CONFIG_PATH=/dev/null, because finding a real device is the point.
    add_executable(blokkily_audio_device_check tests/audio_device_check.cpp)
    target_link_libraries(blokkily_audio_device_check PRIVATE blokkily_core)
    add_dependencies(blokkily_audio_device_check blokkily_test_clap)
    add_test(NAME audio_device_playback
        COMMAND blokkily_audio_device_check
            --clap-fixture $<TARGET_FILE:blokkily_test_clap> --seconds 1.5)
    set_tests_properties(audio_device_playback PROPERTIES
        LABELS "audio;device;realtime;clap"
        SKIP_RETURN_CODE 77
        TIMEOUT 30)

    # A note sent through the system's MIDI layer — a virtual port, the way a
    # controller's driver presents one — must be heard and recorded. The audio
    # side is the deterministic pump; the MIDI side is real, so the check skips
    # itself where there is no MIDI server rather than failing.
    add_executable(blokkily_midi_device_check tests/midi_device_check.cpp)
    target_link_libraries(blokkily_midi_device_check PRIVATE blokkily_core PkgConfig::RTMIDI)
    add_dependencies(blokkily_midi_device_check blokkily_test_clap)
    add_test(NAME midi_device_input
        COMMAND blokkily_midi_device_check --clap-fixture $<TARGET_FILE:blokkily_test_clap>)
    set_tests_properties(midi_device_input PROPERTIES
        LABELS "midi;device;integration;realtime;clap;recording"
        SKIP_RETURN_CODE 77
        TIMEOUT 30)

    if(BLOKKILY_BUILD_GUI)
        find_file(BLOKKILY_TEST_SF2
            NAMES basic.sf2 TimGM6mb.sf2 FluidR3_GM.sf2
            PATHS /usr/share/fweelin /usr/share/sounds/sf2 /usr/share/soundfonts
            NO_DEFAULT_PATH)
        if(NOT BLOKKILY_TEST_SF2)
            message(FATAL_ERROR "BDD integration requires a test SoundFont; set BLOKKILY_TEST_SF2")
        endif()
        target_compile_definitions(blokkily_tests PRIVATE
            BLOKKILY_TEST_SF2_PATH="${BLOKKILY_TEST_SF2}")
        add_dependencies(blokkily blokkily_test_clap blokkily_hang_clap blokkily_test_vst3_VST3)
        add_test(NAME bdd_edit_once_see_everywhere
            COMMAND blokkily --verify
                --clap-fixture $<TARGET_FILE:blokkily_test_clap>
                --vst3-fixture $<TARGET_FILE_DIR:blokkily_test_vst3_VST3>/../..
                --soundfont-fixture ${BLOKKILY_TEST_SF2}
                --project ${CMAKE_BINARY_DIR}/artifacts/verification.blok
                --export ${CMAKE_BINARY_DIR}/artifacts/arrangement-bounce.wav
                --screenshot ${CMAKE_BINARY_DIR}/artifacts/edit-once-see-everywhere.png)
        set_tests_properties(bdd_edit_once_see_everywhere PROPERTIES
            LABELS "bdd;e2e;integration;screenshot;clap;vst3;soundfont;project;parameters;mixer;song;export"
            ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}"
            TIMEOUT 120)

        # The playable surfaces, in a tuning that is not twelve tones and on the
        # chord pads, so the screenshot shows what the scenarios claim.
        add_test(NAME bdd_playable_keyboards
            COMMAND blokkily --verify
                --clap-fixture $<TARGET_FILE:blokkily_test_clap>
                --vst3-fixture $<TARGET_FILE_DIR:blokkily_test_vst3_VST3>/../..
                --soundfont-fixture ${BLOKKILY_TEST_SF2}
                --project ${CMAKE_BINARY_DIR}/artifacts/keyboard-verification.blok
                --view KEYS --tuning "19-EDO" --scale Major --surface GRID
                --screenshot ${CMAKE_BINARY_DIR}/artifacts/playable-keyboards.png)
        set_tests_properties(bdd_playable_keyboards PROPERTIES
            LABELS "bdd;e2e;integration;screenshot;keyboard;tuning;scale;clap;vst3;soundfont;project"
            ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}"
            TIMEOUT 120)

        add_test(NAME bdd_chord_pads
            COMMAND blokkily --verify
                --clap-fixture $<TARGET_FILE:blokkily_test_clap>
                --vst3-fixture $<TARGET_FILE_DIR:blokkily_test_vst3_VST3>/../..
                --soundfont-fixture ${BLOKKILY_TEST_SF2}
                --project ${CMAKE_BINARY_DIR}/artifacts/chord-verification.blok
                --view KEYS --tuning "24-EDO quarter tones" --scale "Maqam Rast"
                --surface CHORDS
                --screenshot ${CMAKE_BINARY_DIR}/artifacts/chord-pads.png)
        set_tests_properties(bdd_chord_pads PROPERTIES
            LABELS "bdd;e2e;integration;screenshot;keyboard;tuning;scale;clap;vst3;soundfont;project"
            ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}"
            TIMEOUT 120)

        # The honeycomb, turned on its side, in a tuning that is not twelve
        # tones: the layouts the scenarios claim, rendered.
        add_test(NAME bdd_hex_keyboard
            COMMAND blokkily --verify
                --clap-fixture $<TARGET_FILE:blokkily_test_clap>
                --vst3-fixture $<TARGET_FILE_DIR:blokkily_test_vst3_VST3>/../..
                --soundfont-fixture ${BLOKKILY_TEST_SF2}
                --project ${CMAKE_BINARY_DIR}/artifacts/hex-verification.blok
                --view KEYS --tuning "31-EDO" --scale Major --surface GRID
                --layout "Wicki-Hayden" --orientation DOWN
                --screenshot ${CMAKE_BINARY_DIR}/artifacts/hex-keyboard.png)
        set_tests_properties(bdd_hex_keyboard PROPERTIES
            LABELS "bdd;e2e;integration;screenshot;keyboard;tuning;scale;clap;vst3;soundfont;project"
            ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}"
            TIMEOUT 120)

        # A MIDI keyboard played into the real application: chosen from the
        # rendered panel, heard through the production callback, and recorded
        # into the pattern under the playhead, where every editor shows it.
        add_test(NAME bdd_midi_recording
            COMMAND blokkily --verify --scenario midi
                --clap-fixture $<TARGET_FILE:blokkily_test_clap>
                --vst3-fixture $<TARGET_FILE_DIR:blokkily_test_vst3_VST3>/../..
                --soundfont-fixture ${BLOKKILY_TEST_SF2}
                --screenshot ${CMAKE_BINARY_DIR}/artifacts/midi-recording.png)
        set_tests_properties(bdd_midi_recording PROPERTIES
            LABELS "bdd;e2e;integration;screenshot;midi;recording;clap;vst3"
            ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}"
            TIMEOUT 120)

        add_test(NAME bdd_plugin_scan_survives_hang
            COMMAND blokkily --verify
                --clap-fixture $<TARGET_FILE:blokkily_test_clap>
                --vst3-fixture $<TARGET_FILE_DIR:blokkily_test_vst3_VST3>/../..
                --soundfont-fixture ${BLOKKILY_TEST_SF2}
                --project ${CMAKE_BINARY_DIR}/artifacts/scan-verification.blok
                --scan-fixture ${BLOKKILY_CLAP_FIXTURE_DIR})
        set_tests_properties(bdd_plugin_scan_survives_hang PROPERTIES
            LABELS "bdd;e2e;integration;clap;plugins;project;scan"
            ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV};BLOKKILY_SCAN_TIMEOUT_MS=1500;BLOKKILY_SCAN_CACHE=${CMAKE_BINARY_DIR}/artifacts/scan-cache.txt"
            TIMEOUT 120)
    endif()
endif()

# Each foundation and feature owns one fragment holding its sources, tests,
# labels and gates, so adding one means adding a file rather than editing this
# one. The order is fixed; the test support comes first, because later
# fragments register real-time cases with it, then the foundations.
foreach(blokkily_feature
        test_support
        timebase
        audio_assets
        plugin_boundary
        engine_seams
        schema
        chord_velocity
        builtin_effects
        sampler
        tempo_meter_ui
        audio_clips
        disk_streaming
        effects
        record
        plugin_windows
        automation
        dynamic_modulation
        audio_input
        output_recording
        clip_warp
        metronome
        sidechain
        scene_launcher
        wave2_fixes
        llm_assistant
        cross_feature)
    include(cmake/feature_${blokkily_feature}.cmake)
endforeach()

if(BLOKKILY_COVERAGE)
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        message(FATAL_ERROR "BLOKKILY_COVERAGE requires GCC and gcov")
    endif()
    # Measure our sources, excluding vendored frameworks and generated QML.
    # This runs after the feature fragments so their sources are measured too.
    foreach(target blokkily_core blokkily blokkily_scan)
        if(TARGET ${target})
            get_target_property(sources ${target} SOURCES)
            foreach(source IN LISTS sources)
                if(source MATCHES "^src/.*\\.cpp$")
                    set_property(SOURCE ${source} APPEND PROPERTY COMPILE_OPTIONS --coverage)
                endif()
            endforeach()
        endif()
    endforeach()
    target_link_options(blokkily_core INTERFACE --coverage)
endif()
