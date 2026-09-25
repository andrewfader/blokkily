# Feature fragment: wave2_fixes (the confirmed Wave 2 review findings). See
# docs/plans/daw-program.md.
#
# An export renders the song from silence however long the session played
# before it (every processor, delay line and compensation is reset at bounce
# start and again after it); the arrangement handoff between a recompile and
# the render callback is one atomic state word, so a slot being taken is
# never refilled; every processor the engine holds is served on the main
# thread, inserts included; and a take is stamped where the performer heard
# it, the output latency earlier than the block it arrived in.

if(BLOKKILY_BUILD_TESTS)
    add_executable(blokkily_wave2_fixes_tests
        tests/wave2_fixes_tests.cpp
        ${BLOKKILY_ENGINE_SEAMS_APP_SOURCES}
        ${BLOKKILY_SAMPLER_APP_SOURCES})
    target_include_directories(blokkily_wave2_fixes_tests PRIVATE src src/app tests)
    target_link_libraries(blokkily_wave2_fixes_tests PRIVATE blokkily_core ${CMAKE_DL_LIBS})
    target_compile_definitions(blokkily_wave2_fixes_tests PRIVATE
        ${BLOKKILY_EFFECT_FIXTURE_DEFINITIONS}
        BLOKKILY_TEST_CLAP_PATH="$<TARGET_FILE:blokkily_test_clap>"
        BLOKKILY_TEST_ARTIFACTS="${CMAKE_BINARY_DIR}/artifacts")
    target_compile_options(blokkily_wave2_fixes_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
    add_dependencies(blokkily_wave2_fixes_tests
        blokkily_test_clap blokkily_test_clap_effect blokkily_test_vst3_effect_VST3)
    foreach(wave2_case
            bounce_after_playback
            bounce_resumes_clean
            handoff_probe
            handoff_stress
            take_on_heard_beat
            serve_every_processor)
        add_test(NAME wave2_${wave2_case} COMMAND blokkily_wave2_fixes_tests ${wave2_case})
        set_tests_properties(wave2_${wave2_case} PROPERTIES
            LABELS "regression;unit;integration;audio"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 300)
    endforeach()
    set_property(TEST wave2_bounce_after_playback wave2_bounce_resumes_clean
                 APPEND PROPERTY LABELS "export;effects;clap;vst3;plugins")
    set_property(TEST wave2_handoff_probe wave2_handoff_stress
                 APPEND PROPERTY LABELS "engine;clips")
    set_property(TEST wave2_take_on_heard_beat
                 APPEND PROPERTY LABELS "recording;midi;clap;effects")
    set_property(TEST wave2_serve_every_processor
                 APPEND PROPERTY LABELS "plugins;clap;effects")
endif()

if(BLOKKILY_BUILD_TESTS)
    # The handoff under a racing control thread, the latency-stamped capture
    # and reset_processing(), in the production process(), allocation-free.
    blokkily_add_realtime_case(wave2_fixes LABELS "audio;clap;effects;recording;engine")
endif()
