# Feature fragment: export formats (features/export_formats.feature). A bounce
# can be MP3 or Ogg Vorbis as well as WAV (libsndfile encodes them), every
# track and return can be exported as a file of its own, and the song can be
# rendered as an oscilloscope video of its tracks over the master (ffmpeg
# encodes it). Each scenario maps to a case below.

target_sources(blokkily_core PRIVATE src/audio/audio_writer.cpp)

if(BLOKKILY_BUILD_GUI)
    target_sources(blokkily PRIVATE
        src/app/app_controller_export.cpp
        src/app/scope_video.cpp
        src/app/scope_video.hpp)
endif()

if(BLOKKILY_BUILD_TESTS)
    add_executable(blokkily_export_formats_tests tests/export_formats_tests.cpp
        ${BLOKKILY_ENGINE_SEAMS_APP_SOURCES} ${BLOKKILY_SAMPLER_APP_SOURCES})
    target_include_directories(blokkily_export_formats_tests PRIVATE src/app)
    target_link_libraries(blokkily_export_formats_tests PRIVATE blokkily_core)
    target_compile_definitions(blokkily_export_formats_tests PRIVATE
        BLOKKILY_TEST_CLAP_PATH="$<TARGET_FILE:blokkily_test_clap>"
        BLOKKILY_EXPORT_WORK="${CMAKE_BINARY_DIR}/export-formats-tests")
    add_dependencies(blokkily_export_formats_tests blokkily_test_clap)
    foreach(case mp3 ogg extension stems stems_sum
            dither_quiet dither_distortion dither_deterministic dither_bounce)
        add_test(NAME export_formats_${case} COMMAND blokkily_export_formats_tests ${case})
        set_tests_properties(export_formats_${case} PROPERTIES
            LABELS "bdd;integration;audio;export"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 60)
    endforeach()
endif()

if(BLOKKILY_BUILD_GUI AND BLOKKILY_BUILD_TESTS)
    find_program(BLOKKILY_FFMPEG ffmpeg REQUIRED)
    target_sources(blokkily PRIVATE src/app/verify/scenario_export_formats.cpp)
    add_test(NAME bdd_export_formats COMMAND blokkily --verify --scenario export_formats
        --clap-fixture $<TARGET_FILE:blokkily_test_clap>
        --project ${CMAKE_BINARY_DIR}/artifacts/export/export.blok
        --export ${CMAKE_BINARY_DIR}/artifacts/export/song.wav
        --screenshot ${CMAKE_BINARY_DIR}/artifacts/export-formats.png)
    set_tests_properties(bdd_export_formats PROPERTIES
        LABELS "bdd;e2e;integration;screenshot;audio;export;clap"
        ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV};BLOKKILY_FFMPEG=${BLOKKILY_FFMPEG}"
        TIMEOUT 240)
endif()
