# Feature fragment: continuous_midi (phase 2, wave 4.1). See
# docs/plans/phase2-program.md and features/continuous_midi.feature.
#
# Controller movements (pitch bend, control changes, channel pressure) in the
# canonical pattern: the model (event.hpp, pattern.cpp), the scheduler and the
# timeline, the engine's chase (song_engine.cpp), the `control` record, the
# take, and the instruments that play them.

target_sources(blokkily_core PRIVATE src/project/records_continuous.cpp)

if(BLOKKILY_BUILD_TESTS)
    if(NOT BLOKKILY_TEST_SF2)
        find_file(BLOKKILY_TEST_SF2
            NAMES basic.sf2 TimGM6mb.sf2 FluidR3_GM.sf2
            PATHS /usr/share/fweelin /usr/share/sounds/sf2 /usr/share/soundfonts
            NO_DEFAULT_PATH)
    endif()
    if(NOT BLOKKILY_TEST_SF2)
        message(FATAL_ERROR "continuous MIDI tests require a test SoundFont; set BLOKKILY_TEST_SF2")
    endif()
    add_executable(blokkily_continuous_midi_tests tests/continuous_midi_tests.cpp)
    target_include_directories(blokkily_continuous_midi_tests PRIVATE tests)
    target_link_libraries(blokkily_continuous_midi_tests PRIVATE blokkily_core)
    target_compile_definitions(blokkily_continuous_midi_tests PRIVATE
        BLOKKILY_TEST_CLAP_PATH="$<TARGET_FILE:blokkily_test_clap>"
        BLOKKILY_TEST_VST3_PATH="$<TARGET_FILE_DIR:blokkily_test_vst3_VST3>/../.."
        BLOKKILY_TEST_SF2_PATH="${BLOKKILY_TEST_SF2}"
        BLOKKILY_CONTINUOUS_WORK="${CMAKE_BINARY_DIR}/continuous-midi-tests")
    target_compile_options(blokkily_continuous_midi_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
    add_dependencies(blokkily_continuous_midi_tests blokkily_test_clap blokkily_test_vst3_VST3)
    foreach(midi_case
            soundfont_bend
            soundfont_sustain
            vst3_bend
            sampler_bend_sustain
            pattern_sample_accurate
            chase
            record_take
            export_matches)
        add_test(NAME continuous_midi_${midi_case}
            COMMAND blokkily_continuous_midi_tests ${midi_case})
        set_tests_properties(continuous_midi_${midi_case} PROPERTIES
            LABELS "unit;integration;audio;midi"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 60)
    endforeach()
    set_property(TEST continuous_midi_soundfont_bend continuous_midi_soundfont_sustain
                 APPEND PROPERTY LABELS "soundfont")
    set_property(TEST continuous_midi_vst3_bend continuous_midi_chase continuous_midi_export_matches
                 APPEND PROPERTY LABELS "vst3")
    set_property(TEST continuous_midi_pattern_sample_accurate APPEND PROPERTY LABELS "clap")
    set_property(TEST continuous_midi_record_take APPEND PROPERTY LABELS "clap;recording;project")
    set_property(TEST continuous_midi_export_matches APPEND PROPERTY LABELS "export")
endif()

if(BLOKKILY_BUILD_TESTS)
    # Controller movements played and chased by process() without allocating.
    blokkily_add_realtime_case(continuous_midi LABELS "audio;clap;midi")
endif()
