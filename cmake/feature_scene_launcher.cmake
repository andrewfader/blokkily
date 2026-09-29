# Feature fragment: scene_launcher (phase 2, wave 6.1). See
# docs/plans/phase2-program.md and features/scene_launcher.feature.
#
# The grid is part of the song (Song::launcher, records_scene_launcher.cpp);
# the engine compiles it with the arrangement and plays it inside the render
# callback (src/audio/engine/engine_launcher.cpp); the LAUNCH view edits and
# launches it, and arrangement recording prints what was launched into the
# song (Song::print_take).

target_sources(blokkily_core PRIVATE
    src/model/scene_launcher.cpp
    src/audio/engine/engine_launcher.cpp
    src/project/records_scene_launcher.cpp
)

if(BLOKKILY_BUILD_GUI)
    target_sources(blokkily PRIVATE
        src/app/song_model_launcher.cpp
        src/app/app_controller_launcher.cpp
        src/app/verify/scenario_launcher.cpp)
    set_source_files_properties(ui/LauncherView.qml PROPERTIES
        QT_RESOURCE_ALIAS "LauncherView.qml")
    qt_target_qml_sources(blokkily QML_FILES ui/LauncherView.qml)
endif()

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
            follow_across_wrap
            stop_releases
            arrangement_hand_back
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

    blokkily_add_realtime_case(scene_launcher LABELS "audio;launcher;clap")

    if(BLOKKILY_BUILD_GUI)
        # The launcher in the real application: the LAUNCH view picked from
        # the view switcher, scenes and cells made by clicking the grid, a
        # scene launched from its chip and heard through the production
        # callback, takes printed into the arrangement, saved and exported.
        add_test(NAME bdd_launcher
            COMMAND blokkily --verify --scenario launcher
                --clap-fixture $<TARGET_FILE:blokkily_test_clap>
                --vst3-fixture $<TARGET_FILE_DIR:blokkily_test_vst3_VST3>/../..
                --soundfont-fixture ${BLOKKILY_TEST_SF2}
                --project ${CMAKE_BINARY_DIR}/artifacts/launcher.blok
                --export ${CMAKE_BINARY_DIR}/artifacts/launcher.wav
                --screenshot ${CMAKE_BINARY_DIR}/artifacts/launcher.png)
        set_tests_properties(bdd_launcher PROPERTIES
            LABELS "bdd;e2e;integration;screenshot;clap;launcher;export"
            ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}"
            TIMEOUT 300)
    endif()
endif()
