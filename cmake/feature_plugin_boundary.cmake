# Feature fragment: plugin_boundary (item 1.4, F-C plugin boundary v2). See docs/plans/daw-program.md.
#
# The adapters live in blokkily_core already; this fragment owns the boundary
# tests. Each case of blokkily_plugin_boundary_tests is its own CTest test,
# proved through the production CLAP and VST3 adapters against the real
# fixtures (features/plugin_boundary.feature).

if(BLOKKILY_BUILD_TESTS)
    # A CLAP audio effect with a stereo input, proving the input wiring. It
    # lives in a directory of its own so that scans of the instrument fixture
    # directory never meet it.
    add_library(blokkily_test_clap_input MODULE tests/fixtures/test_clap_input.cpp)
    target_include_directories(blokkily_test_clap_input PRIVATE third_party/clap/include)
    set_target_properties(blokkily_test_clap_input PROPERTIES
        PREFIX "" SUFFIX ".clap" OUTPUT_NAME "blokkily-test-input"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/clap-input-fixture")

    add_executable(blokkily_plugin_boundary_tests tests/plugin_boundary_tests.cpp)
    target_link_libraries(blokkily_plugin_boundary_tests PRIVATE blokkily_core ${CMAKE_DL_LIBS})
    target_compile_definitions(blokkily_plugin_boundary_tests PRIVATE
        BLOKKILY_TEST_CLAP_PATH="$<TARGET_FILE:blokkily_test_clap>"
        BLOKKILY_TEST_CLAP_INPUT_PATH="$<TARGET_FILE:blokkily_test_clap_input>"
        BLOKKILY_TEST_VST3_PATH="$<TARGET_FILE_DIR:blokkily_test_vst3_VST3>/../..")
    target_compile_options(blokkily_plugin_boundary_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
    add_dependencies(blokkily_plugin_boundary_tests
        blokkily_test_clap blokkily_test_clap_input blokkily_test_vst3_VST3)
    foreach(boundary_case
            clap_parameters
            clap_gui_turn
            clap_thread_check
            clap_request_callback
            clap_input_wiring
            clap_state
            clap_latency_tail
            clap_transport
            clap_midi_raw
            vst3_load_state_base
            vst3_turn_base
            vst3_turn_edits
            vst3_parameters)
        add_test(NAME plugin_${boundary_case}
            COMMAND blokkily_plugin_boundary_tests ${boundary_case})
        if(boundary_case MATCHES "^clap_")
            set(boundary_labels "unit;integration;plugins;parameters;clap")
        else()
            set(boundary_labels "unit;integration;plugins;parameters;vst3")
        endif()
        if(boundary_case MATCHES "_base$")
            list(APPEND boundary_labels regression)
        endif()
        set_tests_properties(plugin_${boundary_case} PROPERTIES
            LABELS "${boundary_labels}"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 60)
    endforeach()

    # The audio-thread half of the boundary (set_transport, process,
    # take_parameter_edits) allocates nothing, for both formats.
    target_compile_definitions(blokkily_realtime_checks PRIVATE
        BLOKKILY_TEST_VST3_PATH="$<TARGET_FILE_DIR:blokkily_test_vst3_VST3>/../..")
    target_link_libraries(blokkily_realtime_checks PRIVATE ${CMAKE_DL_LIBS})
    add_dependencies(blokkily_realtime_checks blokkily_test_vst3_VST3)
    blokkily_add_realtime_case(plugin_edits LABELS "clap;vst3;plugins;parameters;integration")
endif()
