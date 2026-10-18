# Feature fragment: choosing the audio system (features/audio_systems.feature):
# ALSA, PulseAudio, or JACK - also how PipeWire's JACK interface is reached.

if(BLOKKILY_BUILD_GUI)
    target_sources(blokkily PRIVATE src/app/app_controller_audio_system.cpp)
endif()

if(BLOKKILY_BUILD_TESTS)
    add_executable(blokkily_audio_systems_tests tests/audio_systems_tests.cpp)
    target_link_libraries(blokkily_audio_systems_tests PRIVATE blokkily_core)
    foreach(case listed missing devices)
        add_test(NAME audio_systems_${case} COMMAND blokkily_audio_systems_tests ${case})
        set_tests_properties(audio_systems_${case} PROPERTIES
            LABELS "unit;audio" ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 30)
    endforeach()
    # The real JACK (or PipeWire's JACK) server, played through: skips (77)
    # where there is none, as the default device check does.
    add_test(NAME audio_device_jack
        COMMAND blokkily_audio_device_check --api jack
            --clap-fixture $<TARGET_FILE:blokkily_test_clap> --seconds 1.0)
    set_tests_properties(audio_device_jack PROPERTIES
        LABELS "audio;device;realtime;clap;jack"
        SKIP_RETURN_CODE 77
        TIMEOUT 30)
endif()

if(BLOKKILY_BUILD_GUI AND BLOKKILY_BUILD_TESTS)
    target_sources(blokkily PRIVATE src/app/verify/scenario_audio_system.cpp)
    add_test(NAME bdd_audio_system COMMAND blokkily --verify --scenario audio_system
        --screenshot ${CMAKE_BINARY_DIR}/artifacts/audio-system.png)
    set_tests_properties(bdd_audio_system PROPERTIES
        LABELS "bdd;e2e;integration;screenshot;audio"
        ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}" TIMEOUT 60)
endif()
