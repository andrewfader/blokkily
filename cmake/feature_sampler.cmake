# Feature fragment: sampler (items 1.9 and 2.3, sampler). See docs/plans/daw-program.md.
#
# Item 1.9 is the sampler's DSP core: SamplerProgram (pure data and its state
# blob) and SamplerInstrument (a PluginInstance that decodes through the shared
# AudioAssetCache and plays keyed zones or drum/slice kits).
#
# Item 2.3 is the sampler in the application: "Sampler" and "Drum Sampler" in
# the processor factory and the browser (src/app/sampler_processor.*), the
# panel's property and edits on AppController (app_controller_sampler.cpp),
# instrument state pushed into the running processor without a rebuild, undo
# giving a running sampler its earlier program back, ui/SamplerPanel.qml, the
# headless sampler_app_* tests and the bdd_sampler gate.
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

# ---------------------------------------------------------------- item 2.3
# The factory's sampler registration, shared by the application and every
# headless test that compiles the processor factory.
set(BLOKKILY_SAMPLER_APP_SOURCES
    src/app/sampler_processor.cpp
    src/app/sampler_processor.hpp)
if(TARGET blokkily_engine_seams_tests)
    target_sources(blokkily_engine_seams_tests PRIVATE ${BLOKKILY_SAMPLER_APP_SOURCES})
endif()

if(BLOKKILY_BUILD_GUI)
    target_sources(blokkily PRIVATE
        ${BLOKKILY_SAMPLER_APP_SOURCES}
        src/app/app_controller_sampler.cpp
        src/app/verify/scenario_sampler.cpp)
    set_source_files_properties(ui/SamplerPanel.qml PROPERTIES QT_RESOURCE_ALIAS SamplerPanel.qml)
    qt_target_qml_sources(blokkily QML_FILES ui/SamplerPanel.qml)
endif()

if(BLOKKILY_BUILD_TESTS)
    # The sampler through the factory, the application's graph code and the
    # production SongEngine, pumped deterministically (features/sampler.feature).
    add_executable(blokkily_sampler_app_tests
        tests/sampler_app_tests.cpp
        ${BLOKKILY_ENGINE_SEAMS_APP_SOURCES}
        ${BLOKKILY_SAMPLER_APP_SOURCES})
    target_include_directories(blokkily_sampler_app_tests PRIVATE src src/app tests)
    target_link_libraries(blokkily_sampler_app_tests PRIVATE blokkily_core ${CMAKE_DL_LIBS})
    target_compile_definitions(blokkily_sampler_app_tests PRIVATE
        BLOKKILY_AUDIO_FIXTURES="${BLOKKILY_AUDIO_FIXTURE_DIR}"
        BLOKKILY_TEST_ARTIFACTS="${CMAKE_BINARY_DIR}/artifacts")
    target_compile_options(blokkily_sampler_app_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
    add_dependencies(blokkily_sampler_app_tests blokkily_audio_fixtures)
    foreach(sampler_app_case factory live_swap bounce_parity)
        add_test(NAME sampler_app_${sampler_app_case}
            COMMAND blokkily_sampler_app_tests ${sampler_app_case})
        set_tests_properties(sampler_app_${sampler_app_case} PROPERTIES
            LABELS "bdd;integration;audio;sampler;engine"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 60)
    endforeach()
    set_property(TEST sampler_app_bounce_parity APPEND PROPERTY LABELS "export")

    if(BLOKKILY_BUILD_GUI)
        add_dependencies(blokkily blokkily_audio_fixtures)
        # The sampler chosen in the rendered browser, played through the
        # production callback, edited from its panel while the song runs (the
        # same engine, no rebuild, no recompile), undone, chopped into pads,
        # saved, loaded and bounced; the screenshot shows the panel.
        add_test(NAME bdd_sampler
            COMMAND blokkily --verify --scenario sampler
                --project ${CMAKE_BINARY_DIR}/artifacts/sampler/sampler.blok
                --export ${CMAKE_BINARY_DIR}/artifacts/sampler-bounce.wav
                --screenshot ${CMAKE_BINARY_DIR}/artifacts/sampler.png)
        set_tests_properties(bdd_sampler PROPERTIES
            LABELS "bdd;e2e;integration;screenshot;sampler;audio;project;export;undo"
            ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV};BLOKKILY_AUDIO_FIXTURES=${BLOKKILY_AUDIO_FIXTURE_DIR}"
            TIMEOUT 120)
    endif()
endif()
