# Feature fragment: plugin isolation (features/plugin_isolation.feature). A
# plugin runs in blokkily_plugin_host, a helper process of its own, behind the
# PluginInstance boundary; a crash or a hang there silences its track and
# leaves the window running.

find_package(Threads REQUIRED)
target_sources(blokkily_core PRIVATE
    src/plugins/sandbox.cpp
    src/plugins/sandbox_host.cpp)
target_link_libraries(blokkily_core PUBLIC Threads::Threads)
if(UNIX AND NOT APPLE)
    # shm_open lives in librt before glibc 2.34.
    target_link_libraries(blokkily_core PRIVATE rt)
endif()

add_executable(blokkily_plugin_host src/app/plugin_host_main.cpp)
target_link_libraries(blokkily_plugin_host PRIVATE blokkily_core)
install(TARGETS blokkily_plugin_host RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
if(BLOKKILY_BUILD_GUI)
    add_dependencies(blokkily blokkily_plugin_host)
endif()

if(BLOKKILY_BUILD_TESTS)
    # The test synth built to break on cue: key 127 crashes its process, key
    # 126 never returns. Kept out of the scanned fixture directory.
    add_library(blokkily_faulty_clap MODULE tests/fixtures/test_clap.cpp)
    target_include_directories(blokkily_faulty_clap PRIVATE third_party/clap/include)
    target_compile_definitions(blokkily_faulty_clap PRIVATE BLOKKILY_FAULTY_FIXTURE=1)
    if(APPLE)
        target_sources(blokkily_faulty_clap PRIVATE tests/fixtures/test_clap_native_mac.mm)
        target_link_libraries(blokkily_faulty_clap PRIVATE "-framework Cocoa")
    elseif(WIN32)
        target_sources(blokkily_faulty_clap PRIVATE tests/fixtures/test_clap_native_win.cpp)
        target_link_libraries(blokkily_faulty_clap PRIVATE user32 gdi32)
    endif()
    set_target_properties(blokkily_faulty_clap PROPERTIES
        PREFIX "" SUFFIX ".clap" OUTPUT_NAME "blokkily-faulty"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/faulty-fixtures")

    if(UNIX AND NOT APPLE)
        add_executable(blokkily_plugin_isolation_tests tests/plugin_isolation_tests.cpp
            src/app/processor_factory.cpp ${BLOKKILY_SAMPLER_APP_SOURCES})
        target_include_directories(blokkily_plugin_isolation_tests PRIVATE src src/app tests)
        target_link_libraries(blokkily_plugin_isolation_tests PRIVATE blokkily_core)
        target_compile_definitions(blokkily_plugin_isolation_tests PRIVATE
            BLOKKILY_TEST_CLAP_PATH="$<TARGET_FILE:blokkily_test_clap>"
            BLOKKILY_FAULTY_CLAP_PATH="$<TARGET_FILE:blokkily_faulty_clap>"
            BLOKKILY_TEST_CLAP_EFFECT_PATH="$<TARGET_FILE:blokkily_test_clap_effect>"
            BLOKKILY_TEST_VST3_PATH="$<TARGET_FILE_DIR:blokkily_test_vst3_VST3>/../.."
            BLOKKILY_PLUGIN_HOST_PATH="$<TARGET_FILE:blokkily_plugin_host>")
        target_compile_options(blokkily_plugin_isolation_tests PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
        add_dependencies(blokkily_plugin_isolation_tests blokkily_plugin_host
            blokkily_test_clap blokkily_faulty_clap blokkily_test_clap_effect blokkily_test_vst3_VST3)
        foreach(isolation_case clap vst3 state crash hang refused compensated editor effect macro period)
            add_test(NAME plugin_isolation_${isolation_case}
                COMMAND blokkily_plugin_isolation_tests ${isolation_case})
            set_tests_properties(plugin_isolation_${isolation_case} PROPERTIES
                LABELS "bdd;integration;plugins;clap;isolation"
                ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 60)
        endforeach()
        set_tests_properties(plugin_isolation_vst3 PROPERTIES
            LABELS "bdd;integration;plugins;vst3;isolation")
    endif()
endif()

if(BLOKKILY_BUILD_GUI)
    target_sources(blokkily PRIVATE src/app/app_controller_isolation.cpp)
endif()
if(BLOKKILY_BUILD_GUI AND BLOKKILY_BUILD_TESTS AND UNIX AND NOT APPLE)
    target_sources(blokkily PRIVATE src/app/verify/scenario_plugin_isolation.cpp)
    add_dependencies(blokkily blokkily_faulty_clap)
    # The window with its plugins in helper processes: one crashes on cue,
    # the rest play on, and RELOAD brings it back (proved from track meters
    # fed by the production render callback).
    add_test(NAME bdd_plugin_isolation COMMAND blokkily --verify --scenario plugin_isolation
        --isolate-plugins
        --clap-fixture $<TARGET_FILE:blokkily_test_clap>
        --faulty-clap $<TARGET_FILE:blokkily_faulty_clap>
        --screenshot ${CMAKE_BINARY_DIR}/artifacts/plugin-isolation.png)
    set_tests_properties(bdd_plugin_isolation PROPERTIES
        LABELS "bdd;e2e;integration;screenshot;clap;isolation"
        ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}" TIMEOUT 120)
endif()

if(BLOKKILY_BUILD_TESTS AND UNIX AND NOT APPLE)
    # The isolated instruments through the real audio server, in real time:
    # the audio thread never waits for a helper, so a held note that comes
    # back with silent callbacks is a helper that cannot keep up. Skips (77)
    # where there is no server.
    add_test(NAME audio_device_isolated
        COMMAND blokkily_audio_device_check --clap-fixture $<TARGET_FILE:blokkily_test_clap>
            --isolate $<TARGET_FILE:blokkily_plugin_host> --seconds 1.5)
    add_test(NAME audio_device_isolated_jack
        COMMAND blokkily_audio_device_check --api jack --clap-fixture $<TARGET_FILE:blokkily_test_clap>
            --isolate $<TARGET_FILE:blokkily_plugin_host> --seconds 1.5)
    set_tests_properties(audio_device_isolated audio_device_isolated_jack PROPERTIES
        LABELS "audio;device;realtime;clap;isolation" SKIP_RETURN_CODE 77 TIMEOUT 30)
endif()
