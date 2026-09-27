# Feature fragment: sidechain (Item 4, Inter-Track Sidechaining & Multi-Output Plugin Routing).
# See docs/plans/phase2-program.md.

target_sources(blokkily_core PRIVATE
    src/audio/sidechain.cpp
)

if(BLOKKILY_BUILD_TESTS)
    add_executable(blokkily_sidechain_tests tests/sidechain_tests.cpp)
    target_include_directories(blokkily_sidechain_tests PRIVATE tests src)
    target_link_libraries(blokkily_sidechain_tests PRIVATE blokkily_core)
    target_compile_options(blokkily_sidechain_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)

    foreach(sc_case
            compression
            topological_sort
            multi_output)
        add_test(NAME sidechain_${sc_case}
            COMMAND blokkily_sidechain_tests ${sc_case})
        set_tests_properties(sidechain_${sc_case} PROPERTIES
            LABELS "unit;integration;audio;sidechain"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 60)
    endforeach()

    blokkily_add_realtime_case(sidechain LABELS "audio;sidechain")
endif()
