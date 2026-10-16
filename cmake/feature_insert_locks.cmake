# Feature fragment: insert_locks. A step's parameter lock can target one of
# its track's insert effects as well as its instrument
# (features/insert_locks.feature). The model, scheduler and engine changes
# live in files blokkily_core already builds; this fragment adds the additive
# lockslot record and the scenario tests.

target_sources(blokkily_core PRIVATE src/project/records_lock_targets.cpp)

if(BLOKKILY_BUILD_TESTS)
    add_executable(blokkily_insert_locks_tests
        tests/insert_locks_tests.cpp
        ${BLOKKILY_ENGINE_SEAMS_APP_SOURCES}
        ${BLOKKILY_SAMPLER_APP_SOURCES})
    target_include_directories(blokkily_insert_locks_tests PRIVATE src src/app tests)
    target_link_libraries(blokkily_insert_locks_tests PRIVATE blokkily_core)
    target_compile_definitions(blokkily_insert_locks_tests PRIVATE
        ${BLOKKILY_EFFECT_FIXTURE_DEFINITIONS}
        BLOKKILY_TEST_CLAP_PATH="$<TARGET_FILE:blokkily_test_clap>"
        BLOKKILY_TEST_ARTIFACTS="${CMAKE_BINARY_DIR}/artifacts")
    target_compile_options(blokkily_insert_locks_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
    add_dependencies(blokkily_insert_locks_tests blokkily_test_clap blokkily_test_clap_effect)
    foreach(lock_case heard modulation instrument_untouched bounce project remove_insert
            removed_restores bus_locks bus_locks_model)
        add_test(NAME insert_locks_${lock_case} COMMAND blokkily_insert_locks_tests ${lock_case})
        set_tests_properties(insert_locks_${lock_case} PROPERTIES
            LABELS "bdd;unit;integration;audio;effects;clap;parameters"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 120)
    endforeach()
    set_property(TEST insert_locks_bounce APPEND PROPERTY LABELS "export")
    set_property(TEST insert_locks_project APPEND PROPERTY LABELS "project")
endif()

if(BLOKKILY_BUILD_GUI)
    # The verify driver's `--scenario insert_locks` group.
    target_sources(blokkily PRIVATE src/app/verify/scenario_insert_locks.cpp)
    if(BLOKKILY_BUILD_TESTS)
        # A step's lock pointed at the CLAP effect from the inspector's ON and
        # PARAM pickers, heard through the production callback at the locked
        # Gain; the tracker's marking; undo and redo.
        add_test(NAME bdd_insert_locks
            COMMAND blokkily --verify --scenario insert_locks
                --clap-fixture $<TARGET_FILE:blokkily_test_clap>
                --clap-effect-fixture $<TARGET_FILE:blokkily_test_clap_effect>
                --screenshot ${CMAKE_BINARY_DIR}/artifacts/insert-locks.png)
        set_tests_properties(bdd_insert_locks PROPERTIES
            LABELS "bdd;e2e;integration;screenshot;clap;effects;parameters"
            ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}"
            TIMEOUT 120)
        add_dependencies(blokkily blokkily_test_clap_effect)
    endif()
endif()
