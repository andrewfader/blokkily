# Feature fragment: the track arpeggiator (features/arpeggiator.feature). A
# stage between a track's gathered notes and its instrument
# (src/audio/engine/engine_arpeggiator.hpp), its settings on the Track, the
# `arp` project record, and the ARP rows of the device panel.

target_sources(blokkily_core PRIVATE
    src/audio/engine/engine_arpeggiator.cpp
    src/project/records_arpeggiator.cpp)

if(BLOKKILY_BUILD_GUI)
    target_sources(blokkily PRIVATE src/app/song_model_arpeggiator.cpp)
endif()

if(BLOKKILY_BUILD_TESTS)
    add_executable(blokkily_arpeggiator_tests tests/arpeggiator_tests.cpp
        ${BLOKKILY_ENGINE_SEAMS_APP_SOURCES} ${BLOKKILY_SAMPLER_APP_SOURCES})
    target_include_directories(blokkily_arpeggiator_tests PRIVATE src/app src tests)
    target_link_libraries(blokkily_arpeggiator_tests PRIVATE blokkily_core)
    target_compile_definitions(blokkily_arpeggiator_tests PRIVATE
        BLOKKILY_TEST_CLAP_PATH="$<TARGET_FILE:blokkily_test_clap>")
    add_dependencies(blokkily_arpeggiator_tests blokkily_test_clap)
    foreach(case stage_up stage_modes stage_release_and_latch engine_render records)
        add_test(NAME arpeggiator_${case} COMMAND blokkily_arpeggiator_tests ${case})
        set_tests_properties(arpeggiator_${case} PROPERTIES
            LABELS "unit;bdd;integration;sequencer;arpeggiator"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 60)
    endforeach()
    set_property(TEST arpeggiator_engine_render APPEND PROPERTY LABELS "audio;clap")
    set_property(TEST arpeggiator_records APPEND PROPERTY LABELS "project")
    blokkily_add_realtime_case(arpeggiator LABELS "audio;engine;arpeggiator")
endif()

if(BLOKKILY_BUILD_GUI AND BLOKKILY_BUILD_TESTS)
    target_sources(blokkily PRIVATE src/app/verify/scenario_arpeggiator.cpp)
    add_test(NAME bdd_arpeggiator COMMAND blokkily --verify --scenario arpeggiator
        --clap-fixture $<TARGET_FILE:blokkily_test_clap>
        --project ${CMAKE_BINARY_DIR}/artifacts/arpeggiator.blok
        --screenshot ${CMAKE_BINARY_DIR}/artifacts/arpeggiator.png)
    set_tests_properties(bdd_arpeggiator PROPERTIES
        LABELS "bdd;e2e;integration;screenshot;clap;project;arpeggiator"
        ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}" TIMEOUT 120)
endif()
