# Feature fragment: sampler editing (features/sampler_editing.feature): a
# multi-sampled instrument imported from named files, the key-by-velocity
# zone map, velocity layers, and the zone's region and loop on its waveform.
# The import's mapping is proved by sampler_multisample_* in
# blokkily_sampler_tests; the panel by bdd_sampler_editing.

if(BLOKKILY_BUILD_GUI AND BLOKKILY_BUILD_TESTS)
    target_sources(blokkily PRIVATE src/app/verify/scenario_sampler_editing.cpp)
    add_test(NAME bdd_sampler_editing COMMAND blokkily --verify --scenario sampler_editing
        --screenshot ${CMAKE_BINARY_DIR}/artifacts/sampler-editing.png)
    set_tests_properties(bdd_sampler_editing PROPERTIES
        LABELS "bdd;e2e;integration;screenshot;sampler;audio;undo"
        ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}" TIMEOUT 120)
endif()
