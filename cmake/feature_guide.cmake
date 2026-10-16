# Feature fragment: guide. See features/guide.feature.
#
# The floating guide (ui/guide/GuideOverlay.qml, src/guide/): the Blokkily
# mascot narrating user and MCP actions with why explanations, theory Q&A,
# interactive JSON lessons, and guide-performed edits through the same
# commands the window's buttons call. The overlay and lessons are QML types
# and resources of the window's own module, listed with the rest in
# CMakeLists.txt. This fragment adds the verify driver's `--scenario guide`
# group and its gate.

if(BLOKKILY_BUILD_GUI AND BLOKKILY_BUILD_TESTS)
    # GUIDE toggles the mascot; notes are narrated with a why; theory
    # questions are answered without an LLM; TEACH ME runs a lesson with a
    # spotlight ring and checks; the guide's own edits are one undo step and
    # announced; destructive guide actions ask first; the window stays
    # playable underneath — with screenshots mid-narration.
    # The floating guide: mascot + bubble on screen, narration on a note
    # add, spotlight ring round the play button, a lesson advancing on its
    # check, and a guide action attributed to the guide. Screenshots:
    # guide-overlay.png (narration) and guide-lesson.png (lesson step).
    add_test(NAME bdd_guide
        COMMAND blokkily --verify --scenario guide
            --clap-fixture $<TARGET_FILE:blokkily_test_clap>
            --vst3-fixture $<TARGET_FILE_DIR:blokkily_test_vst3_VST3>/../..
            --soundfont-fixture ${BLOKKILY_TEST_SF2}
            --screenshot ${CMAKE_BINARY_DIR}/artifacts/guide-overlay.png)
    set_tests_properties(bdd_guide PROPERTIES
        LABELS "bdd;e2e;integration;screenshot;guide"
        ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}"
        TIMEOUT 120)
endif()
