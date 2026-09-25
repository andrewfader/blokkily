# Feature fragment: test_support (item W0.7, F-G probe and allocation guard). See docs/plans/daw-program.md.
#
# blokkily_realtime_checks proves that what runs on the audio thread keeps the
# real-time rules. It is the only binary that links tests/support/alloc_guard.cpp,
# which replaces the global operator new and delete: a replaced allocator
# belongs to exactly one executable.
#
# A later feature that touches process() adds tests/realtime/<case>.cpp, which
# registers itself with BLOKKILY_REALTIME_CASE(<case>), and calls
#     blokkily_add_realtime_case(<case> [LABELS extra;labels])
# from its own fragment. That compiles the file into the executable and adds
# the CTest test realtime_<case>, labelled realtime. This fragment is included
# before every feature fragment, so the function exists when they run.

if(BLOKKILY_BUILD_TESTS)
    add_executable(blokkily_realtime_checks
        tests/realtime/main.cpp
        tests/support/alloc_guard.cpp)
    target_include_directories(blokkily_realtime_checks PRIVATE tests)
    target_link_libraries(blokkily_realtime_checks PRIVATE blokkily_core)
    target_compile_definitions(blokkily_realtime_checks PRIVATE
        BLOKKILY_TEST_CLAP_PATH="$<TARGET_FILE:blokkily_test_clap>")
    target_compile_options(blokkily_realtime_checks PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
    add_dependencies(blokkily_realtime_checks blokkily_test_clap)

    function(blokkily_add_realtime_case case_name)
        cmake_parse_arguments(PARSE_ARGV 1 realtime "" "" "LABELS")
        target_sources(blokkily_realtime_checks PRIVATE tests/realtime/${case_name}.cpp)
        add_test(NAME realtime_${case_name} COMMAND blokkily_realtime_checks ${case_name})
        set_tests_properties(realtime_${case_name} PROPERTIES
            LABELS "realtime;${realtime_LABELS}"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}"
            TIMEOUT 60)
    endfunction()

    # The harness checks itself first: a guard that cannot see a deliberate
    # allocation would make every other case pass for nothing.
    blokkily_add_realtime_case(alloc_guard_self_check LABELS "unit")
    blokkily_add_realtime_case(audio_probe LABELS "unit")
    blokkily_add_realtime_case(engine_baseline LABELS "audio;clap;midi;integration")
endif()
