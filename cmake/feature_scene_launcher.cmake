# Feature fragment: scene_launcher (Item 5, Non-Linear Clip / Scene Launcher Matrix).
# See docs/plans/phase2-program.md.

target_sources(blokkily_core PRIVATE
    src/model/scene_launcher.cpp
    src/audio/engine/engine_launcher.cpp
    src/project/records_scene_launcher.cpp
)

if(BLOKKILY_BUILD_TESTS)
    add_executable(blokkily_scene_launcher_tests tests/scene_launcher_tests.cpp)
    target_include_directories(blokkily_scene_launcher_tests PRIVATE tests src)
    target_link_libraries(blokkily_scene_launcher_tests PRIVATE blokkily_core)
    target_compile_definitions(blokkily_scene_launcher_tests PRIVATE
        BLOKKILY_TEST_CLAP_PATH="$<TARGET_FILE:blokkily_test_clap>"
        BLOKKILY_TEST_ARTIFACTS="${CMAKE_BINARY_DIR}/artifacts")
    target_compile_options(blokkily_scene_launcher_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
    add_dependencies(blokkily_scene_launcher_tests blokkily_test_clap)

    foreach(launcher_case
            quantized_launch
            downbeat_after_wrap
            follow_actions
            stop_releases
            record_prints_arrangement
            edit_while_launched
            serialization)
        add_test(NAME scene_launcher_${launcher_case}
            COMMAND blokkily_scene_launcher_tests ${launcher_case})
        set_tests_properties(scene_launcher_${launcher_case} PROPERTIES
            LABELS "bdd;unit;integration;audio;launcher;clap;plugins"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 120)
    endforeach()
    set_property(TEST scene_launcher_record_prints_arrangement APPEND PROPERTY LABELS "export")
    set_property(TEST scene_launcher_serialization APPEND PROPERTY LABELS "project;schema")

    blokkily_add_realtime_case(scene_launcher LABELS "audio;launcher")
endif()
