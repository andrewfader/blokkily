# Feature fragment: schema (item 1.5, F-E program schema). See docs/plans/daw-program.md.
#
# The model additions (audio files and clips, effects, sends, returns, track
# inputs, automation lanes) and their additive project records. No engine or
# UI code: later items play and edit what this stores.

target_sources(blokkily_core PRIVATE
    src/model/automation.cpp
    src/model/song_schema.cpp
    src/project/records_audio.cpp
    src/project/records_effects.cpp
    src/project/records_input.cpp
    src/project/records_automation.cpp)

if(BLOKKILY_BUILD_TESTS)
    # Executable scenarios for features/program_schema.feature.
    add_executable(blokkily_schema_tests tests/schema_tests.cpp)
    target_link_libraries(blokkily_schema_tests PRIVATE blokkily_core)
    target_compile_options(blokkily_schema_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
    add_test(NAME program_schema COMMAND blokkily_schema_tests)
    set_tests_properties(program_schema PROPERTIES
        LABELS "unit;project;song;automation"
        ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 30)
endif()
