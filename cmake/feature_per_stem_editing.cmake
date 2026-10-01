# Feature fragment: per_stem_editing. See features/per_stem_editing.feature.
#
# A stem is a track that owns its instrument. A section (what the interface
# calls a pattern: VERSE, CHORUS) holds one part per stem (Song::sections,
# src/model/song_sections.cpp), saved by records_sections.cpp; a file written
# before sections is given them when it loads. This fragment adds those, the
# verify driver's `--scenario stems` group and the GUI gate that drives the
# real window: it selects stems from the rendered mixer and stem rack, draws
# in the rendered piano roll and rack, presses the rendered lanes, and proves
# through the song model and the production render callback that each stem
# keeps its own part and is heard on its own instrument.

target_sources(blokkily_core PRIVATE
    src/model/song_sections.cpp
    src/project/records_sections.cpp)

if(BLOKKILY_BUILD_GUI)
    target_sources(blokkily PRIVATE src/app/verify/scenario_stems.cpp)
    if(BLOKKILY_BUILD_TESTS)
        add_test(NAME bdd_per_stem_editing
            COMMAND blokkily --verify --scenario stems
                --clap-fixture $<TARGET_FILE:blokkily_test_clap>
                --soundfont-fixture ${BLOKKILY_TEST_SF2}
                --project ${CMAKE_BINARY_DIR}/artifacts/per-stem-editing/stems.blok
                --screenshot ${CMAKE_BINARY_DIR}/artifacts/per-stem-editing.png)
        set_tests_properties(bdd_per_stem_editing PROPERTIES
            LABELS "bdd;e2e;integration;screenshot;clap;song"
            ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}"
            TIMEOUT 120)
    endif()
endif()

if(BLOKKILY_BUILD_TESTS)
    # Sections on the model and in the project file.
    add_executable(blokkily_sections_tests tests/sections_tests.cpp)
    target_link_libraries(blokkily_sections_tests PRIVATE blokkily_core)
    target_compile_options(blokkily_sections_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
    foreach(sections_case
            adopt_keeps_what_plays
            tracks_and_sections_stay_whole
            project_round_trip)
        add_test(NAME sections_${sections_case}
            COMMAND blokkily_sections_tests ${sections_case})
        set_tests_properties(sections_${sections_case} PROPERTIES
            LABELS "unit;song;project;sections"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 60)
    endforeach()
endif()
