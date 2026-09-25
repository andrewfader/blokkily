# Feature fragment: clip_warp (item 3.6, clip warp). See docs/plans/daw-program.md.
#
# Audio clips that follow the song's tempo map, take a stretch ratio and a
# pitch shift (decision 7). Renditions are rendered by Rubber Band 4 in its
# offline mode on a worker thread and kept in the AudioAssetCache as derived
# assets, so the render callback still only reads decoded memory. Rubber Band
# is REQUIRED and PRIVATE to blokkily_core. It is GPL: see README.md.

pkg_check_modules(RUBBERBAND REQUIRED IMPORTED_TARGET rubberband>=4.0)

target_sources(blokkily_core PRIVATE
    src/model/clip_warp.cpp
    src/audio/clip_warp.cpp
    src/project/records_clip_warp.cpp)
target_link_libraries(blokkily_core PRIVATE PkgConfig::RUBBERBAND)

if(BLOKKILY_BUILD_GUI)
    target_sources(blokkily PRIVATE
        src/app/app_controller_warp.cpp
        src/app/verify/scenario_clip_warp.cpp)
    # The warp panel is served beside the clip lane that opens it.
    set_source_files_properties(ui/ClipWarpPanel.qml PROPERTIES QT_RESOURCE_ALIAS ClipWarpPanel.qml)
    qt_target_qml_sources(blokkily QML_FILES ui/ClipWarpPanel.qml)
endif()

if(BLOKKILY_BUILD_TESTS)
    # features/clip_warp.feature, from rendered audio through the production
    # worker, callback and bounce, on real files.
    add_executable(blokkily_clip_warp_tests tests/clip_warp_tests.cpp)
    target_include_directories(blokkily_clip_warp_tests PRIVATE tests)
    target_link_libraries(blokkily_clip_warp_tests PRIVATE blokkily_core)
    target_compile_definitions(blokkily_clip_warp_tests PRIVATE
        BLOKKILY_CLIP_WARP_WORK="${CMAKE_BINARY_DIR}/clip-warp-tests")
    target_compile_options(blokkily_clip_warp_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
    foreach(warp_case
            follow_tempo
            follow_constant
            pitch_octave
            ratio_double
            pending_silent
            project_round_trip
            detect_bpm
            cache_derived)
        add_test(NAME clip_warp_${warp_case} COMMAND blokkily_clip_warp_tests ${warp_case})
        set_tests_properties(clip_warp_${warp_case} PROPERTIES
            LABELS "unit;integration;audio;clips;warp"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 120)
    endforeach()
    set_property(TEST clip_warp_follow_tempo APPEND PROPERTY LABELS "export;timebase")
    set_property(TEST clip_warp_follow_constant APPEND PROPERTY LABELS "timebase")
    set_property(TEST clip_warp_project_round_trip APPEND PROPERTY LABELS "project")
    set_property(TEST clip_warp_cache_derived APPEND PROPERTY LABELS "assets")

    # Renditions render on the worker and swap into a running engine without
    # the render callback allocating.
    blokkily_add_realtime_case(clip_warp LABELS "audio;clips;warp;integration")

    if(BLOKKILY_BUILD_GUI)
        # The real application: the warp panel opened from a clip, its
        # fields typed into, the rendering state shown while the worker
        # renders, the result heard through the production callback, undo,
        # save and load, and an export read back.
        add_test(NAME bdd_clip_warp
            COMMAND blokkily --verify --scenario clip_warp
                --clap-fixture $<TARGET_FILE:blokkily_test_clap>
                --project ${CMAKE_BINARY_DIR}/artifacts/clip-warp/clip-warp.blok
                --screenshot ${CMAKE_BINARY_DIR}/artifacts/clip-warp.png)
        set_tests_properties(bdd_clip_warp PROPERTIES
            LABELS "bdd;e2e;integration;screenshot;audio;clips;warp;project;export"
            ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}"
            TIMEOUT 180)
    endif()
endif()
