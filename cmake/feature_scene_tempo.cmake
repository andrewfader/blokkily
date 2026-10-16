# Feature fragment: scene_tempo (features/scene_tempo.feature). A scene of
# the launcher can carry its own tempo (Scene::tempo_bpm, saved on its scene
# record); launching the scene plays it through the engine's tempo override,
# as a followed MIDI clock does, without editing the song's tempo map.

if(BLOKKILY_BUILD_GUI AND BLOKKILY_BUILD_TESTS)
    target_sources(blokkily PRIVATE src/app/verify/scenario_scene_tempo.cpp)
    add_test(NAME bdd_scene_tempo COMMAND blokkily --verify --scenario scene_tempo
        --clap-fixture $<TARGET_FILE:blokkily_test_clap>
        --project ${CMAKE_BINARY_DIR}/artifacts/scene-tempo.blok
        --screenshot ${CMAKE_BINARY_DIR}/artifacts/scene-tempo.png)
    set_tests_properties(bdd_scene_tempo PROPERTIES
        LABELS "bdd;e2e;integration;screenshot;launcher;clap;export;project"
        ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}" TIMEOUT 180)
endif()
