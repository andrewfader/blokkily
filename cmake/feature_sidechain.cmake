# Feature fragment: sidechain (phase 2, wave 5.2). See
# docs/plans/phase2-program.md and features/sidechain.feature.
#
# An insert's sidechain key (EffectSlot::sidechain) is saved and validated
# with the song and compiled with the arrangement, together with the track
# render order that puts a key's source first; that code is listed by
# feature_dynamic_modulation.cmake. The built-in compressor listens to the
# key. Multi-output plugin routing is not implemented (see the plan).

if(BLOKKILY_BUILD_TESTS)
    add_executable(blokkily_sidechain_tests tests/sidechain_tests.cpp)
    target_include_directories(blokkily_sidechain_tests PRIVATE tests src)
    target_link_libraries(blokkily_sidechain_tests PRIVATE blokkily_core)
    target_compile_definitions(blokkily_sidechain_tests PRIVATE
        BLOKKILY_TEST_ARTIFACTS="${CMAKE_BINARY_DIR}/artifacts")
    target_compile_options(blokkily_sidechain_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)

    foreach(sc_case
            key_pre_fader
            key_aligned
            bounce_includes_sidechain)
        add_test(NAME sidechain_${sc_case}
            COMMAND blokkily_sidechain_tests ${sc_case})
        set_tests_properties(sidechain_${sc_case} PROPERTIES
            LABELS "bdd;unit;integration;audio;sidechain;mixer"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 60)
    endforeach()
    set_property(TEST sidechain_bounce_includes_sidechain APPEND PROPERTY LABELS "export")

    blokkily_add_realtime_case(sidechain LABELS "audio;sidechain;mixer")
endif()
