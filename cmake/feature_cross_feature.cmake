if(BLOKKILY_BUILD_GUI)
    target_sources(blokkily PRIVATE src/app/app_controller_cross.cpp)
endif()
if(BLOKKILY_BUILD_GUI AND BLOKKILY_BUILD_TESTS)
    target_sources(blokkily PRIVATE src/app/verify/scenario_cross.cpp)
    add_test(NAME bdd_cross_feature COMMAND blokkily --verify --scenario cross
        --clap-fixture $<TARGET_FILE:blokkily_test_clap>
        --clap-effect-fixture $<TARGET_FILE:blokkily_test_clap_effect>
        --project ${CMAKE_BINARY_DIR}/artifacts/cross/cross.blok
        --screenshot ${CMAKE_BINARY_DIR}/artifacts/cross-feature.png)
    set_tests_properties(bdd_cross_feature PROPERTIES
        LABELS "bdd;e2e;integration;screenshot;clap;effects;project;automation"
        ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}" TIMEOUT 120)
endif()
if(BLOKKILY_BUILD_GUI AND BLOKKILY_BUILD_TESTS)
    target_sources(blokkily PRIVATE src/app/verify/scenario_everything.cpp)
    add_test(NAME bdd_session_everything COMMAND blokkily --verify --scenario everything
        --clap-fixture $<TARGET_FILE:blokkily_test_clap>
        --vst3-fixture $<TARGET_FILE_DIR:blokkily_test_vst3_VST3>/../..
        --clap-effect-fixture $<TARGET_FILE:blokkily_test_clap_effect>
        --vst3-effect-fixture $<TARGET_FILE_DIR:blokkily_test_vst3_effect_VST3>/../..
        --soundfont-fixture ${BLOKKILY_TEST_SF2}
        --project ${CMAKE_BINARY_DIR}/artifacts/everything/everything.blok
        --export ${CMAKE_BINARY_DIR}/artifacts/everything/everything.wav
        --screenshot ${CMAKE_BINARY_DIR}/artifacts/session-everything.png)
    set_tests_properties(bdd_session_everything PROPERTIES
        LABELS "bdd;e2e;integration;screenshot;audio;clap;vst3;soundfont;project;export;recording"
        ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}" TIMEOUT 120)
endif()
