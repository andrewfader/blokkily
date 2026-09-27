# Feature fragment: scene_launcher (Item 5, Non-Linear Clip / Scene Launcher Matrix).
# See docs/plans/phase2-program.md.

target_sources(blokkily_core PRIVATE
    src/model/scene_launcher.cpp
    src/audio/scene_launcher_engine.cpp
    src/project/records_scene_launcher.cpp
)

if(BLOKKILY_BUILD_TESTS)
    add_executable(blokkily_scene_launcher_tests tests/scene_launcher_tests.cpp)
    target_include_directories(blokkily_scene_launcher_tests PRIVATE tests src)
    target_link_libraries(blokkily_scene_launcher_tests PRIVATE blokkily_core)
    target_compile_options(blokkily_scene_launcher_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)

    foreach(launcher_case
            quantized_launch
            follow_actions
            jam_recording
            serialization)
        add_test(NAME scene_launcher_${launcher_case}
            COMMAND blokkily_scene_launcher_tests ${launcher_case})
        set_tests_properties(scene_launcher_${launcher_case} PROPERTIES
            LABELS "unit;integration;audio;launcher"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 60)
    endforeach()

    blokkily_add_realtime_case(scene_launcher LABELS "audio;launcher")
endif()
