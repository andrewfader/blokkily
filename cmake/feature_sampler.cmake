# Feature fragment: sampler (items 1.9 and 2.3, sampler). See docs/plans/daw-program.md.
#
# Item 1.9 is the sampler's DSP core: SamplerProgram (pure data and its state
# blob) and SamplerInstrument (a PluginInstance that decodes through the shared
# AudioAssetCache and plays keyed zones or drum/slice kits). No factory
# registration or UI yet; that is item 2.3.
#
# Every case of blokkily_sampler_tests is its own CTest test (sampler_<case>),
# proved from audio rendered by SamplerInstrument::process on the generated
# audio fixtures (features/sampler.feature). realtime_sampler_voices proves the
# render path allocates nothing.

target_sources(blokkily_core PRIVATE
    src/instruments/sampler_program.cpp
    src/instruments/sampler.cpp)

if(BLOKKILY_BUILD_TESTS)
    add_executable(blokkily_sampler_tests tests/sampler_tests.cpp)
    target_include_directories(blokkily_sampler_tests PRIVATE tests)
    target_link_libraries(blokkily_sampler_tests PRIVATE blokkily_core)
    target_compile_definitions(blokkily_sampler_tests PRIVATE
        BLOKKILY_AUDIO_FIXTURES="${BLOKKILY_AUDIO_FIXTURE_DIR}")
    target_compile_options(blokkily_sampler_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
    add_dependencies(blokkily_sampler_tests blokkily_audio_fixtures)
    foreach(sampler_case
            file_metadata
            missing_sample
            keyed_pitch
            key_velocity_range
            velocity
            envelope
            loop
            loop_seam
            kit_slices
            one_shot_and_choke
            state
            relative_paths
            parameters
            pattern_locks
            kit_handoff)
        add_test(NAME sampler_${sampler_case}
            COMMAND blokkily_sampler_tests ${sampler_case})
        set_tests_properties(sampler_${sampler_case} PROPERTIES
            LABELS "unit;audio;sampler"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 60)
    endforeach()

    target_compile_definitions(blokkily_realtime_checks PRIVATE
        BLOKKILY_AUDIO_FIXTURES="${BLOKKILY_AUDIO_FIXTURE_DIR}")
    add_dependencies(blokkily_realtime_checks blokkily_audio_fixtures)
    blokkily_add_realtime_case(sampler_voices LABELS "audio;sampler;integration")
endif()
