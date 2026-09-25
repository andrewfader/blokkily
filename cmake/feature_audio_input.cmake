# Feature fragment: audio_input (item 3.2, record stage C, audio input). See docs/plans/daw-program.md.
#
# Audio input into audio clips (features/audio_input.feature): the duplex
# device with its output-only fallback, the deterministic pump's modelled
# inputs and loopback cable, per-track input routes and monitoring (chunk
# stage 6), the capture ring and the take writer that writes WAV files off the
# audio thread, and the placement of a take where it was heard (plan C21).

target_sources(blokkily_core PRIVATE
    src/audio/audio_input.cpp
    src/audio/take_writer.cpp)

if(BLOKKILY_BUILD_GUI)
    target_sources(blokkily PRIVATE
        src/app/app_controller_record_audio.cpp
        src/app/song_model_audio_input.cpp
        src/app/verify/scenario_record_audio.cpp)
endif()

if(BLOKKILY_BUILD_TESTS)
    # From rendered audio through the production callback (the deterministic
    # pump with injected input and a loopback cable), the production bounce
    # read back, and the take files read back.
    # The effect variant builds its master insert through the production
    # processor factory, as the effects tests do.
    add_executable(blokkily_audio_input_tests
        tests/audio_input_tests.cpp
        ${BLOKKILY_ENGINE_SEAMS_APP_SOURCES}
        ${BLOKKILY_SAMPLER_APP_SOURCES})
    target_include_directories(blokkily_audio_input_tests PRIVATE src src/app tests)
    target_link_libraries(blokkily_audio_input_tests PRIVATE blokkily_core)
    target_compile_options(blokkily_audio_input_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
    target_compile_definitions(blokkily_audio_input_tests PRIVATE
        ${BLOKKILY_EFFECT_FIXTURE_DEFINITIONS}
        BLOKKILY_AUDIO_INPUT_WORK="${CMAKE_BINARY_DIR}/audio-input-tests")
    add_dependencies(blokkily_audio_input_tests blokkily_test_clap_effect)
    foreach(input_case
            routes
            loopback_click
            latent_master_insert
            overflow
            monitoring
            take_files
            output_only)
        add_test(NAME audio_input_${input_case}
            COMMAND blokkily_audio_input_tests ${input_case})
        set_tests_properties(audio_input_${input_case} PROPERTIES
            LABELS "unit;integration;audio;recording"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}"
            TIMEOUT 60)
    endforeach()
    set_property(TEST audio_input_loopback_click APPEND PROPERTY LABELS "export;clips")
    set_property(TEST audio_input_latent_master_insert APPEND PROPERTY LABELS "export;clips;clap;effects")
    set_property(TEST audio_input_monitoring APPEND PROPERTY LABELS "export;mixer")

    # Monitoring and capture on the render callback without allocating.
    blokkily_add_realtime_case(audio_input LABELS "audio;recording;integration")

    # The real audio server, opened duplex: skips (77) where there is no input.
    add_executable(blokkily_audio_duplex_device_check tests/audio_duplex_device_check.cpp)
    target_link_libraries(blokkily_audio_duplex_device_check PRIVATE blokkily_core)
    add_test(NAME audio_duplex_device_check COMMAND blokkily_audio_duplex_device_check)
    set_tests_properties(audio_duplex_device_check PROPERTIES
        LABELS "audio;device;realtime;recording"
        SKIP_RETURN_CODE 77
        TIMEOUT 30)

    if(BLOKKILY_BUILD_GUI)
        # Part 3 of "record everything": an Audio In 1-2 take, monitored while
        # it is played, becomes a clip where it was heard; one undo removes
        # the take's notes and its clip; the take moves from the session's
        # temporary folder into <project>.audio/ on the first save.
        add_test(NAME bdd_record_audio
            COMMAND blokkily --verify --scenario record_audio
                --clap-fixture $<TARGET_FILE:blokkily_test_clap>
                --project ${CMAKE_BINARY_DIR}/artifacts/record-audio/record-audio.blok
                --export ${CMAKE_BINARY_DIR}/artifacts/record-audio/record-audio.wav
                --screenshot ${CMAKE_BINARY_DIR}/artifacts/record-audio.png)
        set_tests_properties(bdd_record_audio PROPERTIES
            LABELS "bdd;e2e;integration;screenshot;audio;recording;clips;project;export;mixer;clap"
            ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}"
            TIMEOUT 120)
    endif()
endif()
