# Feature fragment: render analysis. Loudness (BS.1770-4, EBU Tech 3342), true
# peak and band energy of a render. features/render_analysis.feature maps each
# scenario to a case below; the agent's view of it is the bdd_mcp gate.

target_sources(blokkily_core PRIVATE src/audio/analysis.cpp)

if(BLOKKILY_BUILD_GUI)
    target_sources(blokkily PRIVATE src/app/app_controller_analysis.cpp)
endif()

if(BLOKKILY_BUILD_TESTS)
    add_executable(blokkily_analysis_tests tests/analysis_tests.cpp)
    target_link_libraries(blokkily_analysis_tests PRIVATE blokkily_core)
    target_compile_options(blokkily_analysis_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
    foreach(analysis_case loudness gating loudness_range true_peak bands stereo_and_silence)
        add_test(NAME analysis_${analysis_case} COMMAND blokkily_analysis_tests ${analysis_case})
        set_tests_properties(analysis_${analysis_case} PROPERTIES
            LABELS "unit;bdd;audio;render_analysis" TIMEOUT 120)
    endforeach()
endif()
