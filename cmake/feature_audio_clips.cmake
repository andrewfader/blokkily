# Feature fragment: audio_clips (item 2.2, audio clips). See docs/plans/daw-program.md.
#
# Audio clips: the engine's clip stage (src/audio/engine/engine_clips.cpp),
# the song's clip assets and arithmetic (audio_clips.cpp), the song model's
# clip edits, the controller's asynchronous import, the waveform item and
# the clip lane over the arrangement, and their tests and gate.

target_sources(blokkily_core PRIVATE src/audio/audio_clips.cpp)

if(BLOKKILY_BUILD_GUI)
    target_sources(blokkily PRIVATE
        src/app/app_controller_audio.cpp
        src/app/song_model_audio.cpp
        src/app/waveform_item.cpp
        src/app/waveform_item.hpp
        src/app/verify/scenario_audio.cpp)
    # The clip lane is served beside the other panels, from the module's own
    # directory, so it finds Theme and the waveform item without an import.
    set_source_files_properties(ui/AudioClipLane.qml PROPERTIES QT_RESOURCE_ALIAS AudioClipLane.qml)
    qt_target_qml_sources(blokkily QML_FILES ui/AudioClipLane.qml)
endif()

if(BLOKKILY_BUILD_TESTS)
    # features/audio_clips.feature, from rendered audio through the production
    # callback and the production bounce, on real files.
    add_executable(blokkily_audio_clips_tests tests/audio_clips_tests.cpp)
    target_include_directories(blokkily_audio_clips_tests PRIVATE tests)
    target_link_libraries(blokkily_audio_clips_tests PRIVATE blokkily_core)
    target_compile_definitions(blokkily_audio_clips_tests PRIVATE
        BLOKKILY_TEST_CLAP_PATH="$<TARGET_FILE:blokkily_test_clap>"
        BLOKKILY_AUDIO_FIXTURES="${BLOKKILY_AUDIO_FIXTURE_DIR}"
        BLOKKILY_AUDIO_CLIP_WORK="${CMAKE_BINARY_DIR}/audio-clip-tests")
    target_compile_options(blokkily_audio_clips_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
    add_dependencies(blokkily_audio_clips_tests blokkily_test_clap blokkily_audio_fixtures)
    foreach(clips_case
            start_sample
            offset_length
            gain
            fades
            mixer_live
            recompile_continuity
            clap_sum
            overlap_sum
            bounce_readback
            tempo_map
            resampled_file
            project_paths
            missing_and_changed
            adopted_import)
        add_test(NAME audio_clips_${clips_case}
            COMMAND blokkily_audio_clips_tests ${clips_case})
        set_tests_properties(audio_clips_${clips_case} PROPERTIES
            LABELS "unit;integration;audio;clips"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 60)
    endforeach()
    set_property(TEST audio_clips_mixer_live APPEND PROPERTY LABELS "mixer")
    set_property(TEST audio_clips_clap_sum APPEND PROPERTY LABELS "clap")
    set_property(TEST audio_clips_bounce_readback APPEND PROPERTY LABELS "export")
    set_property(TEST audio_clips_tempo_map APPEND PROPERTY LABELS "timebase")
    set_property(TEST audio_clips_project_paths APPEND PROPERTY LABELS "project")
    set_property(TEST audio_clips_missing_and_changed APPEND PROPERTY LABELS "project;assets")
    set_property(TEST audio_clips_adopted_import APPEND PROPERTY LABELS "assets")

    # Clips play through process() without allocating, while another thread
    # recompiles them, across a seek and loop wraps.
    blokkily_add_realtime_case(audio_clips LABELS "audio;clips;integration")

    if(BLOKKILY_BUILD_GUI)
        # The real application: import off the control thread, the clip lane
        # drawn from the bar layout, drag, trim, fades and gain through
        # synthesized mouse and wheel events, heard through the production
        # callback, undo, save with relative paths, a bounce read back, and a
        # missing file flagged on reload.
        add_test(NAME bdd_audio_clips
            COMMAND blokkily --verify --scenario audio
                --clap-fixture $<TARGET_FILE:blokkily_test_clap>
                --project ${CMAKE_BINARY_DIR}/artifacts/audio-clips/audio-clips.blok
                --screenshot ${CMAKE_BINARY_DIR}/artifacts/audio-clips.png)
        set_tests_properties(bdd_audio_clips PROPERTIES
            LABELS "bdd;e2e;integration;screenshot;audio;clips;project;export;mixer"
            ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}"
            TIMEOUT 120)
    endif()
endif()
