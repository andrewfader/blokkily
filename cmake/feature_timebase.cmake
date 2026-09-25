# Feature fragment: timebase (item 1.2, F-A tempo map, meter map, tick/sample). See docs/plans/daw-program.md.
#
# Included from CMakeLists.txt after every core target exists, in a fixed
# order. This file owns the feature's sources (target_sources on blokkily_core
# or blokkily), its tests, labels and CTest gates. Headless tests use
# ${BLOKKILY_HEADLESS_TEST_ENV} and offscreen GUI gates use
# ${BLOKKILY_OFFSCREEN_GATE_ENV} in their ENVIRONMENT. Register tests only
# inside if(BLOKKILY_BUILD_TESTS), and GUI gates only inside
# if(BLOKKILY_BUILD_GUI).

# The tempo map, the meter map and the tick clock, and their project records.
target_sources(blokkily_core PRIVATE
    src/model/timebase.cpp
    src/project/records_timebase.cpp)

if(BLOKKILY_BUILD_GUI)
    # The bar layout and the timebase edits of the song model.
    target_sources(blokkily PRIVATE src/app/song_model_timebase.cpp)
endif()

if(BLOKKILY_BUILD_TESTS)
    find_file(BLOKKILY_TEST_SF2
        NAMES basic.sf2 TimGM6mb.sf2 FluidR3_GM.sf2
        PATHS /usr/share/fweelin /usr/share/sounds/sf2 /usr/share/soundfonts
        NO_DEFAULT_PATH)
    if(NOT BLOKKILY_TEST_SF2)
        message(FATAL_ERROR "timebase tests require a test SoundFont; set BLOKKILY_TEST_SF2")
    endif()

    # features/timebase.feature: the maps, the clock, and the engine, the
    # bounce and a recording playing through them, on the real CLAP, VST3 and
    # SoundFont instruments.
    add_executable(blokkily_timebase_tests tests/timebase_tests.cpp)
    target_include_directories(blokkily_timebase_tests PRIVATE tests)
    target_link_libraries(blokkily_timebase_tests PRIVATE blokkily_core)
    target_compile_definitions(blokkily_timebase_tests PRIVATE
        BLOKKILY_TEST_CLAP_PATH="$<TARGET_FILE:blokkily_test_clap>"
        BLOKKILY_TEST_VST3_PATH="$<TARGET_FILE_DIR:blokkily_test_vst3_VST3>/../.."
        BLOKKILY_TEST_SF2_PATH="${BLOKKILY_TEST_SF2}")
    target_compile_options(blokkily_timebase_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
    add_dependencies(blokkily_timebase_tests blokkily_test_clap blokkily_test_vst3_VST3)
    foreach(timebase_case
            tempo_map_constant
            tempo_map_step
            tempo_map_ramp
            tempo_map_edits
            meter_map
            rebar
            song_length_audio
            project_records
            legacy_tempo_clamped)
        add_test(NAME timebase_${timebase_case}
            COMMAND blokkily_timebase_tests ${timebase_case})
        set_tests_properties(timebase_${timebase_case} PROPERTIES
            LABELS "unit;timebase;project"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 60)
    endforeach()
    foreach(timebase_case
            engine_step
            engine_ramp
            engine_seven_eight
            engine_playhead
            seek_to_event_tick
            capture_keeps_its_clock
            bounce_across_tempo
            recording_across_tempo)
        add_test(NAME timebase_${timebase_case}
            COMMAND blokkily_timebase_tests ${timebase_case})
        set_tests_properties(timebase_${timebase_case} PROPERTIES
            LABELS "integration;audio;clap;timebase;engine;export;recording"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 60)
    endforeach()
    add_test(NAME timebase_soundfont_across_tempo
        COMMAND blokkily_timebase_tests soundfont_across_tempo)
    set_tests_properties(timebase_soundfont_across_tempo PROPERTIES
        LABELS "integration;audio;soundfont;timebase"
        ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 60)
    add_test(NAME timebase_vst3_across_tempo
        COMMAND blokkily_timebase_tests vst3_across_tempo)
    set_tests_properties(timebase_vst3_across_tempo PROPERTIES
        LABELS "integration;audio;vst3;timebase"
        ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 60)

    # Regression: a tempo edit while the song plays keeps the playhead's bar,
    # and moving the playhead does not allocate in the render callback.
    blokkily_add_realtime_case(tempo_edit_keeps_bar LABELS "regression;audio;clap;engine")
endif()
