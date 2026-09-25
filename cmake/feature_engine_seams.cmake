# Feature fragment: engine_seams (item 1.1, F-D engine seams, instance reuse, graph signature). See docs/plans/daw-program.md.
#
# Included from CMakeLists.txt after every core target exists, in a fixed
# order. This file owns the feature's sources (target_sources on blokkily_core
# or blokkily), its tests, labels and CTest gates. Headless tests use
# ${BLOKKILY_HEADLESS_TEST_ENV} and offscreen GUI gates use
# ${BLOKKILY_OFFSCREEN_GATE_ENV} in their ENVIRONMENT. Register tests only
# inside if(BLOKKILY_BUILD_TESTS), and GUI gates only inside
# if(BLOKKILY_BUILD_GUI).

# The engine's internal stages, one file per feature that fills one in. Each
# is a no-op until its owning item lands (plan F-D).
target_sources(blokkily_core PRIVATE
    src/audio/engine/engine_clips.cpp
    src/audio/engine/engine_input.cpp
    src/audio/engine/engine_effects.cpp
    src/audio/engine/engine_automation.cpp)

# The application's side of the seams: processor creation, the graph
# signature and instance adoption, and recompile coalescing. None of it needs
# Qt, so the headless tests below compile the same files the application runs.
set(BLOKKILY_ENGINE_SEAMS_APP_SOURCES
    src/app/processor_factory.cpp
    src/app/processor_factory.hpp
    src/app/engine_graph.cpp
    src/app/engine_graph.hpp
    src/app/recompile_coalescer.hpp)
if(BLOKKILY_BUILD_GUI)
    target_sources(blokkily PRIVATE ${BLOKKILY_ENGINE_SEAMS_APP_SOURCES}
        src/app/verify/scenario_engine_seams.cpp)
    # The scenario reads the CLAP fixture's lifecycle log from the module the
    # application loaded.
    target_link_libraries(blokkily PRIVATE ${CMAKE_DL_LIBS})
endif()

if(BLOKKILY_BUILD_TESTS)
    find_file(BLOKKILY_TEST_SF2
        NAMES basic.sf2 TimGM6mb.sf2 FluidR3_GM.sf2
        PATHS /usr/share/fweelin /usr/share/sounds/sf2 /usr/share/soundfonts
        NO_DEFAULT_PATH)
    if(NOT BLOKKILY_TEST_SF2)
        message(FATAL_ERROR "engine seam tests require a test SoundFont; set BLOKKILY_TEST_SF2")
    endif()

    add_executable(blokkily_engine_seams_tests
        tests/engine_seams_tests.cpp
        ${BLOKKILY_ENGINE_SEAMS_APP_SOURCES})
    target_include_directories(blokkily_engine_seams_tests PRIVATE src src/app tests)
    target_link_libraries(blokkily_engine_seams_tests PRIVATE blokkily_core ${CMAKE_DL_LIBS})
    target_compile_definitions(blokkily_engine_seams_tests PRIVATE
        BLOKKILY_TEST_CLAP_PATH="$<TARGET_FILE:blokkily_test_clap>"
        BLOKKILY_TEST_VST3_PATH="$<TARGET_FILE_DIR:blokkily_test_vst3_VST3>/../.."
        BLOKKILY_TEST_SF2_PATH="${BLOKKILY_TEST_SF2}")
    target_compile_options(blokkily_engine_seams_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
    add_dependencies(blokkily_engine_seams_tests blokkily_test_clap blokkily_test_vst3_VST3)
    foreach(seam
            instrumentless_track
            edit_ring
            adoption
            adoption_follows_remap
            running_state
            processor_factory
            graph_signature
            coalescing
            coalescing_retry)
        add_test(NAME engine_seams_${seam} COMMAND blokkily_engine_seams_tests ${seam})
        set_tests_properties(engine_seams_${seam} PROPERTIES
            LABELS "unit;integration;audio;engine"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 60)
    endforeach()

    target_include_directories(blokkily_realtime_checks PRIVATE src)
    blokkily_add_realtime_case(engine_seams LABELS "audio;clap;integration;engine")

    if(BLOKKILY_BUILD_GUI)
        # The seams in the real application: an unchanged instrument survives a
        # rebuild as the same instance, and a burst of edits in one turn of the
        # event loop is one recompile that plays the last of them.
        add_test(NAME bdd_engine_seams
            COMMAND blokkily --verify --scenario engine_seams
                --clap-fixture $<TARGET_FILE:blokkily_test_clap>
                --vst3-fixture $<TARGET_FILE_DIR:blokkily_test_vst3_VST3>/../..
                --soundfont-fixture ${BLOKKILY_TEST_SF2}
                --screenshot ${CMAKE_BINARY_DIR}/artifacts/engine-seams.png)
        set_tests_properties(bdd_engine_seams PROPERTIES
            LABELS "bdd;e2e;integration;screenshot;clap;engine"
            ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}"
            TIMEOUT 120)
    endif()
endif()
