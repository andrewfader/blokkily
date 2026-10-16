# Feature fragment: autosave and crash recovery (features/autosave.feature).
# An unsaved song is copied to the recovery folder (src/app/recovery.hpp)
# while it has changes; a copy a window left when it did not close normally
# is offered back at the next launch.

if(BLOKKILY_BUILD_GUI)
    target_sources(blokkily PRIVATE
        src/app/recovery.cpp
        src/app/recovery.hpp
        src/app/app_controller_recovery.cpp)
endif()

if(BLOKKILY_BUILD_GUI AND BLOKKILY_BUILD_TESTS)
    target_sources(blokkily PRIVATE src/app/verify/scenario_recovery.cpp)
    set(BLOKKILY_RECOVERY_ENV
        "${BLOKKILY_OFFSCREEN_GATE_ENV};BLOKKILY_RECOVERY_DIR=${CMAKE_BINARY_DIR}/artifacts/recovery/store;BLOKKILY_AUTOSAVE_MS=200")
    add_test(NAME bdd_recovery_crash COMMAND blokkily --verify --scenario recovery_crash
        --project ${CMAKE_BINARY_DIR}/artifacts/recovery/crashed.blok)
    set_tests_properties(bdd_recovery_crash PROPERTIES
        FIXTURES_SETUP blokkily_crashed_session
        RESOURCE_LOCK blokkily_recovery_store
        LABELS "bdd;e2e;integration;project;recovery"
        ENVIRONMENT "${BLOKKILY_RECOVERY_ENV}" TIMEOUT 60)
    add_test(NAME bdd_recovery_restore COMMAND blokkily --verify --scenario recovery_restore
        --project ${CMAKE_BINARY_DIR}/artifacts/recovery/crashed.blok
        --screenshot ${CMAKE_BINARY_DIR}/artifacts/recovery.png)
    set_tests_properties(bdd_recovery_restore PROPERTIES
        FIXTURES_REQUIRED blokkily_crashed_session
        RESOURCE_LOCK blokkily_recovery_store
        LABELS "bdd;e2e;integration;screenshot;project;recovery"
        ENVIRONMENT "${BLOKKILY_RECOVERY_ENV}" TIMEOUT 60)
endif()
