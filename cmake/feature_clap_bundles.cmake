# Feature fragment: clap_bundles. A CLAP plugin installed as a macOS bundle
# (a .clap directory with its executable in Contents/MacOS) is found, loaded
# and played (features/plugin_scanning.feature). The change lives in the
# plugin loader and both scanners; this fragment adds the regression test.

if(BLOKKILY_BUILD_TESTS)
    add_executable(blokkily_clap_bundle_tests tests/clap_bundle_tests.cpp)
    target_link_libraries(blokkily_clap_bundle_tests PRIVATE blokkily_core)
    target_compile_options(blokkily_clap_bundle_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
    target_compile_definitions(blokkily_clap_bundle_tests PRIVATE
        BLOKKILY_TEST_CLAP_PATH="$<TARGET_FILE:blokkily_test_clap>"
        BLOKKILY_TEST_WORK="${CMAKE_BINARY_DIR}/test-work")
    add_dependencies(blokkily_clap_bundle_tests blokkily_test_clap)
    add_test(NAME clap_bundle COMMAND blokkily_clap_bundle_tests)
    set_tests_properties(clap_bundle PROPERTIES
        LABELS "unit;integration;plugins;clap;scan;regression"
        ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 60)
endif()
