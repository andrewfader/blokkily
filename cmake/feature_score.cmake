# Feature fragment: score view. See features/score_view.feature.
#
# The score (ui/ScoreView.qml, src/app/score_model.*): a fourth projection of
# the canonical pattern, drawn as true staff notation with the Bravura SMuFL
# font (SIL OFL, resources/fonts/Bravura.otf). Read-only for v1 — it observes
# the pattern and never writes back. The clef picker covers the full roster
# including French violin, subbass and the C clefs; CHANT draws an unmetered
# chant staff with bare noteheads. This fragment adds the verify driver's
# `--scenario score` group and its gate.
#
# If Bravura is absent the view draws staff lines and fallback notehead
# shapes and logs once; the gate fails the run when the font is missing,
# because the SMuFL rendering is the feature.

# The clef of each part and the key and meter of each section live in the
# song and are saved with it (records_notation.cpp).
target_sources(blokkily_core PRIVATE src/project/records_notation.cpp)

if(BLOKKILY_BUILD_GUI AND BLOKKILY_BUILD_TESTS)
    # SCORE shows the pattern's notes as notation; the French violin clef
    # re-spells under the new clef; CHANT draws an unmetered staff with bare
    # noteheads; a 3-sharp key shows three sharps and names A major — with
    # screenshots of the rendered score.
    add_test(NAME bdd_score
        COMMAND blokkily --verify --scenario score --view SCORE
            --clap-fixture $<TARGET_FILE:blokkily_test_clap>
            --vst3-fixture $<TARGET_FILE_DIR:blokkily_test_vst3_VST3>/../..
            --soundfont-fixture ${BLOKKILY_TEST_SF2}
            --screenshot ${CMAKE_BINARY_DIR}/artifacts/score-view.png)
    set_tests_properties(bdd_score PROPERTIES
        LABELS "bdd;e2e;integration;screenshot;score"
        ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}"
        TIMEOUT 180)
endif()
