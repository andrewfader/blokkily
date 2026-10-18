# Feature fragment: DAWproject export (features/dawproject.feature). The song
# leaves as a .dawproject - the open exchange format Bitwig Studio, Studio
# One and Cubase read - holding what the engine plays, the mixer, the tempo
# map, the automation and the plugins' states. Each scenario maps to a case
# below; every case reads the archive back.

target_sources(blokkily_core PRIVATE src/project/dawproject.cpp src/project/dawproject_import.cpp)

if(BLOKKILY_BUILD_GUI)
    target_sources(blokkily PRIVATE src/app/app_controller_dawproject.cpp)
    set_source_files_properties(src/app/app_controller_dawproject.cpp PROPERTIES
        COMPILE_DEFINITIONS "BLOKKILY_VERSION=\"${PROJECT_VERSION}\"")
endif()

if(BLOKKILY_BUILD_TESTS)
    # The schema check runs xmllint against the official schema, vendored in
    # tests/fixtures/dawproject. Linux and macOS have it (libxml2); Windows
    # runners do not, so there alone the check is left out, loudly.
    if(WIN32)
        find_program(BLOKKILY_XMLLINT xmllint)
        if(NOT BLOKKILY_XMLLINT)
            message(WARNING "xmllint not found: the DAWproject schema check (dawproject_schema) is not registered")
        endif()
    else()
        find_program(BLOKKILY_XMLLINT xmllint REQUIRED)
    endif()
    add_executable(blokkily_dawproject_tests tests/dawproject_tests.cpp
        ${BLOKKILY_ENGINE_SEAMS_APP_SOURCES} ${BLOKKILY_SAMPLER_APP_SOURCES})
    target_include_directories(blokkily_dawproject_tests PRIVATE src/app)
    target_link_libraries(blokkily_dawproject_tests PRIVATE blokkily_core)
    target_compile_definitions(blokkily_dawproject_tests PRIVATE
        BLOKKILY_TEST_CLAP_PATH="$<TARGET_FILE:blokkily_test_clap>"
        BLOKKILY_TEST_VST3_PATH="$<TARGET_FILE_DIR:blokkily_test_vst3_VST3>/../.."
        BLOKKILY_DAWPROJECT_WORK="${CMAKE_BINARY_DIR}/dawproject-tests"
        BLOKKILY_DAWPROJECT_SCHEMA_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/dawproject"
        BLOKKILY_XMLLINT="${BLOKKILY_XMLLINT}"
        BLOKKILY_CMAKE_COMMAND="${CMAKE_COMMAND}")
    add_dependencies(blokkily_dawproject_tests blokkily_test_clap blokkily_test_vst3_VST3)
    set(blokkily_dawproject_cases notes render clap_state vst3_state audio_clip omissions
        import_round_trip import_placeholders import_foreign)
    if(BLOKKILY_XMLLINT)
        list(APPEND blokkily_dawproject_cases schema)
    endif()
    foreach(case IN LISTS blokkily_dawproject_cases)
        add_test(NAME dawproject_${case} COMMAND blokkily_dawproject_tests ${case})
        set_tests_properties(dawproject_${case} PROPERTIES
            LABELS "bdd;integration;export;project;clap;vst3"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 60)
    endforeach()
endif()

if(BLOKKILY_BUILD_GUI AND BLOKKILY_BUILD_TESTS)
    target_sources(blokkily PRIVATE src/app/verify/scenario_dawproject.cpp)
    add_test(NAME bdd_dawproject COMMAND blokkily --verify --scenario dawproject
        --clap-fixture $<TARGET_FILE:blokkily_test_clap>
        --project ${CMAKE_BINARY_DIR}/artifacts/dawproject/song.blok
        --export ${CMAKE_BINARY_DIR}/artifacts/dawproject/song.dawproject
        --screenshot ${CMAKE_BINARY_DIR}/artifacts/dawproject.png)
    set_tests_properties(bdd_dawproject PROPERTIES
        LABELS "bdd;e2e;integration;screenshot;export;project;clap"
        ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}"
        TIMEOUT 120)
endif()
