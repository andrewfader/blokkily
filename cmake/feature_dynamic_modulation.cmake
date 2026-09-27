# Feature fragment: dynamic_modulation (Item 3, Universal Dynamic Modulation Engine).
# See docs/plans/phase2-program.md.

target_sources(blokkily_core PRIVATE
    src/audio/modulator.cpp
)

if(BLOKKILY_BUILD_TESTS)
    add_executable(blokkily_dynamic_modulation_tests tests/dynamic_modulation_tests.cpp)
    target_include_directories(blokkily_dynamic_modulation_tests PRIVATE tests)
    target_link_libraries(blokkily_dynamic_modulation_tests PRIVATE blokkily_core)
    target_compile_options(blokkily_dynamic_modulation_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)

    foreach(mod_case
            lfo_shapes
            envelope_follower
            macro_routing
            engine_dispatch)
        add_test(NAME dynamic_modulation_${mod_case}
            COMMAND blokkily_dynamic_modulation_tests ${mod_case})
        set_tests_properties(dynamic_modulation_${mod_case} PROPERTIES
            LABELS "unit;integration;audio;modulation"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 60)
    endforeach()

    blokkily_add_realtime_case(dynamic_modulation LABELS "audio;modulation")
endif()
