# Feature fragment: disk_streaming (wave 4.2). Audio clips over the streaming
# threshold play from disk through the engine's clip stage
# (src/audio/engine/engine_clips.cpp). See docs/plans/phase2-program.md.

target_sources(blokkily_core PRIVATE
    src/audio/disk_stream.cpp
)

if(BLOKKILY_BUILD_TESTS)
    add_executable(blokkily_disk_streaming_tests tests/disk_streaming_tests.cpp)
    target_include_directories(blokkily_disk_streaming_tests PRIVATE tests)
    target_link_libraries(blokkily_disk_streaming_tests PRIVATE blokkily_core)
    target_compile_definitions(blokkily_disk_streaming_tests PRIVATE
        BLOKKILY_AUDIO_FIXTURES="${BLOKKILY_AUDIO_FIXTURE_DIR}"
        BLOKKILY_STREAM_WORK="${CMAKE_BINARY_DIR}/disk-streaming-tests")
    target_compile_options(blokkily_disk_streaming_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)

    foreach(stream_case
            playback
            resampled
            seek
            underrun
            loop_cue
            bounce)
        add_test(NAME disk_streaming_${stream_case}
            COMMAND blokkily_disk_streaming_tests ${stream_case})
        set_tests_properties(disk_streaming_${stream_case} PROPERTIES
            LABELS "unit;integration;audio;streaming"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 60)
    endforeach()

    set_property(TEST disk_streaming_bounce APPEND PROPERTY LABELS "export")

    blokkily_add_realtime_case(disk_streaming LABELS "audio;streaming")
endif()
