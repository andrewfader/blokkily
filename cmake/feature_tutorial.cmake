# Feature fragment: tutorial. See features/tutorial.feature.
#
# The HELP button's guided tour (ui/Tutorial.qml) and its mascot
# (ui/Blokkily.qml) are QML types of the window's own module, listed with the
# rest in CMakeLists.txt. This fragment adds the verify driver's
# `--scenario tutorial` group and its gate.

if(BLOKKILY_BUILD_GUI)
    target_sources(blokkily PRIVATE src/app/verify/scenario_tutorial.cpp)
endif()

if(BLOKKILY_BUILD_GUI AND BLOKKILY_BUILD_TESTS)
    # HELP, every stop of the tour against the controls it rings, the window
    # playable underneath, and every way out, with a screenshot mid-tour
    # (tutorial-steps.png) and one at its first stop (tutorial.png).
    add_test(NAME bdd_tutorial
        COMMAND blokkily --verify --scenario tutorial
            --clap-fixture $<TARGET_FILE:blokkily_test_clap>
            --screenshot ${CMAKE_BINARY_DIR}/artifacts/tutorial.png)
    set_tests_properties(bdd_tutorial PROPERTIES
        LABELS "bdd;e2e;integration;screenshot;clap;tutorial"
        ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}"
        TIMEOUT 120)
endif()
