# Feature fragment: plugin_windows (item 2.6, native plugin windows). See docs/plans/daw-program.md
# and docs/plans/spike-1.7-juce-gui.md.
#
# A plugin's own editor, opened from the E button on a mixer strip or EDITOR
# in the instrument panel: CLAP through clap.gui (with clap.timer-support and
# clap.posix-fd-support served by a main-thread run loop), VST3 through JUCE's
# editor, which core can make because it links the GUI-capable blokkily_juce
# (the spike's "core links GUI"). Knob turns in an editor reach the song
# through the engine's edit ring, one step of history per gesture.

# The run loop adapters register plugin timers and descriptors with.
target_sources(blokkily_core PRIVATE src/plugins/plugin_run_loop.cpp)

if(BLOKKILY_BUILD_GUI)
    target_sources(blokkily PRIVATE
        src/app/editor_gestures.hpp
        src/app/plugin_run_loop_qt.cpp
        src/app/plugin_run_loop_qt.hpp
        src/app/plugin_windows.cpp
        src/app/plugin_windows.hpp
        src/app/app_controller_editors.cpp
        src/app/song_model_plugins.cpp
        src/app/verify/scenario_plugin_windows.cpp)
    # The scenario reads the CLAP fixture's editor report by the fixture's own
    # field numbering.
    set_source_files_properties(src/app/verify/scenario_plugin_windows.cpp PROPERTIES
        INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    target_link_libraries(blokkily PRIVATE ${CMAKE_DL_LIBS})
    # The E button on a strip and the EDITOR bar in the instrument panel.
    set(BLOKKILY_PLUGIN_WINDOWS_QML ui/PluginEditorButton.qml ui/PluginEditorBar.qml)
    foreach(qml_file IN LISTS BLOKKILY_PLUGIN_WINDOWS_QML)
        get_filename_component(qml_name "${qml_file}" NAME)
        set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_name}")
    endforeach()
    qt_target_qml_sources(blokkily QML_FILES ${BLOKKILY_PLUGIN_WINDOWS_QML})
endif()

if(BLOKKILY_BUILD_TESTS)
    # Design 7 core tests, through the production adapters and the real
    # fixtures, with a ManualRunLoop for the application's event loop.
    add_executable(blokkily_plugin_windows_tests
        tests/plugin_windows_tests.cpp
        src/app/editor_gestures.hpp
        src/app/processor_factory.cpp
        src/app/engine_graph.cpp
        # The factory registers the sampler (feature_sampler.cmake, earlier).
        ${BLOKKILY_SAMPLER_APP_SOURCES})
    target_include_directories(blokkily_plugin_windows_tests PRIVATE src src/app tests)
    target_link_libraries(blokkily_plugin_windows_tests PRIVATE blokkily_core ${CMAKE_DL_LIBS})
    target_compile_definitions(blokkily_plugin_windows_tests PRIVATE
        BLOKKILY_TEST_CLAP_PATH="$<TARGET_FILE:blokkily_test_clap>"
        BLOKKILY_TEST_VST3_PATH="$<TARGET_FILE_DIR:blokkily_test_vst3_VST3>/../..")
    target_compile_options(blokkily_plugin_windows_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
    add_dependencies(blokkily_plugin_windows_tests blokkily_test_clap blokkily_test_vst3_VST3)
    foreach(window_case
            run_loop
            clap_embed
            clap_floating
            clap_requests
            clap_timer
            clap_posix_fd
            clap_destroy_open
            gesture_step
            survives_adoption
            vst3_refused)
        add_test(NAME plugin_windows_${window_case}
            COMMAND blokkily_plugin_windows_tests ${window_case})
        set_tests_properties(plugin_windows_${window_case} PROPERTIES
            LABELS "unit;integration;plugins;windows;clap"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 60)
    endforeach()
    set_property(TEST plugin_windows_gesture_step APPEND PROPERTY LABELS "audio;parameters")
    set_property(TEST plugin_windows_survives_adoption APPEND PROPERTY LABELS "engine")
    set_property(TEST plugin_windows_vst3_refused APPEND PROPERTY LABELS "vst3")

    # No display at all (DISPLAY unset, then empty): a VST3 editor is refused
    # at once rather than letting JUCE try ":0.0". Its own process, because it
    # changes the environment.
    add_executable(blokkily_editor_nodisplay_check tests/editor_nodisplay_check.cpp)
    target_link_libraries(blokkily_editor_nodisplay_check PRIVATE blokkily_core)
    target_compile_definitions(blokkily_editor_nodisplay_check PRIVATE
        BLOKKILY_TEST_VST3_PATH="$<TARGET_FILE_DIR:blokkily_test_vst3_VST3>/../..")
    add_dependencies(blokkily_editor_nodisplay_check blokkily_test_vst3_VST3)
    add_test(NAME blokkily_editor_nodisplay_check COMMAND blokkily_editor_nodisplay_check)
    set_tests_properties(blokkily_editor_nodisplay_check PROPERTIES
        LABELS "unit;integration;plugins;windows;vst3"
        ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 60)

    # An open editor, its timer and its descriptor cost the audio thread
    # nothing.
    blokkily_add_realtime_case(plugin_windows LABELS "clap;plugins;windows;integration")

    if(BLOKKILY_BUILD_GUI)
        # The whole feature in the real application, offscreen: steps 1-10 of
        # the plan's 2.6 gate.
        add_test(NAME bdd_plugin_windows
            COMMAND blokkily --verify --scenario plugin_windows
                --clap-fixture $<TARGET_FILE:blokkily_test_clap>
                --vst3-fixture $<TARGET_FILE_DIR:blokkily_test_vst3_VST3>/../..
                --soundfont-fixture ${BLOKKILY_TEST_SF2}
                --project ${CMAKE_BINARY_DIR}/artifacts/plugin-windows.blok
                --screenshot ${CMAKE_BINARY_DIR}/artifacts/plugin-windows.png)
        set_tests_properties(bdd_plugin_windows PROPERTIES
            LABELS "bdd;e2e;integration;screenshot;clap;vst3;plugins;windows;parameters;project"
            ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}"
            TIMEOUT 120)

        # The only real-pixel proof: the VST3 fixture's editor embedded in a
        # real Qt xcb window on a real X server, grabbed back from the server.
        # It keeps the session's DISPLAY (the point is a real display) and
        # skips with 77 where there is no X server to connect to.
        add_executable(blokkily_plugin_window_display_check
            tests/plugin_window_display_check.cpp
            src/app/plugin_run_loop_qt.cpp
            src/app/plugin_run_loop_qt.hpp
            src/app/plugin_windows.cpp
            src/app/plugin_windows.hpp)
        target_include_directories(blokkily_plugin_window_display_check PRIVATE src/app)
        target_link_libraries(blokkily_plugin_window_display_check PRIVATE
            blokkily_core Qt6::Core Qt6::Gui)
        set_target_properties(blokkily_plugin_window_display_check PROPERTIES AUTOMOC ON)
        target_compile_definitions(blokkily_plugin_window_display_check PRIVATE
            BLOKKILY_TEST_VST3_PATH="$<TARGET_FILE_DIR:blokkily_test_vst3_VST3>/../.."
            BLOKKILY_TEST_ARTIFACTS="${CMAKE_BINARY_DIR}/artifacts")
        add_dependencies(blokkily_plugin_window_display_check blokkily_test_vst3_VST3)
        add_test(NAME plugin_window_display_check COMMAND blokkily_plugin_window_display_check)
        set_tests_properties(plugin_window_display_check PROPERTIES
            LABELS "integration;screenshot;display;plugins;windows;vst3"
            ENVIRONMENT "QT_QPA_PLATFORM=xcb;ALSA_CONFIG_PATH=/dev/null;WAYLAND_DISPLAY="
            SKIP_RETURN_CODE 77
            TIMEOUT 60)
    endif()
endif()
