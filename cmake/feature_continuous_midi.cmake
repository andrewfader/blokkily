# Feature fragment: continuous_midi (phase 2, wave 4.1). See
# docs/plans/phase2-program.md and features/continuous_midi.feature.
#
# Controller movements (pitch bend, control changes, channel pressure) in the
# canonical pattern: the model (event.hpp, pattern.cpp), the scheduler and the
# timeline, the engine's chase (song_engine.cpp), the `control` record, the
# take, and the instruments that play them.

target_sources(blokkily_core PRIVATE
    src/project/records_continuous.cpp
    src/project/records_expression.cpp)

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
    target_include_directories(blokkily_continuous_midi_tests PRIVATE tests third_party/clap/include)
    target_link_libraries(blokkily_continuous_midi_tests PRIVATE blokkily_core ${CMAKE_DL_LIBS})
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
            export_matches
            poly_pressure
            mpe_clap_live
            mpe_clap_playback
            mpe_soundfont
            mpe_vst3
            mpe_records)
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
    set_property(TEST continuous_midi_poly_pressure APPEND PROPERTY LABELS "clap;recording;project;export")
    set_property(TEST continuous_midi_mpe_clap_live continuous_midi_mpe_clap_playback
                 APPEND PROPERTY LABELS "clap;mpe")
    set_property(TEST continuous_midi_mpe_clap_live APPEND PROPERTY LABELS "recording")
    set_property(TEST continuous_midi_mpe_clap_playback APPEND PROPERTY LABELS "export;launcher")
    set_property(TEST continuous_midi_mpe_soundfont APPEND PROPERTY LABELS "soundfont;mpe")
    set_property(TEST continuous_midi_mpe_vst3 APPEND PROPERTY LABELS "vst3;mpe")
    set_property(TEST continuous_midi_mpe_records APPEND PROPERTY LABELS "project;mpe")
endif()

if(BLOKKILY_BUILD_TESTS)
    # Controller movements played and chased by process() without allocating.
    blokkily_add_realtime_case(continuous_midi LABELS "audio;clap;midi")
endif()

# The piano roll's controller lane in the real application (wave 4.1): a lane
# chosen from its picker, a bend drawn, moved and erased with the mouse on the
# rendered lane, and the edit heard on its exact sample through the running
# engine's export, against the CLAP fixture given the same bend.
if(BLOKKILY_BUILD_GUI)
    target_sources(blokkily PRIVATE src/app/verify/scenario_controllers.cpp)
    if(BLOKKILY_BUILD_TESTS)
        add_test(NAME bdd_controller_lanes
            COMMAND blokkily --verify --scenario controllers
                --clap-fixture $<TARGET_FILE:blokkily_test_clap>
                --vst3-fixture $<TARGET_FILE_DIR:blokkily_test_vst3_VST3>/../..
                --soundfont-fixture ${BLOKKILY_TEST_SF2}
                --export ${CMAKE_BINARY_DIR}/artifacts/controller-lanes.wav
                --screenshot ${CMAKE_BINARY_DIR}/artifacts/controller-lanes.png)
        set_tests_properties(bdd_controller_lanes PROPERTIES
            LABELS "bdd;e2e;integration;screenshot;midi;clap;export"
            ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}"
            TIMEOUT 120)
    endif()
endif()
