# Feature fragment: record (item 2.5, record stage A). See docs/plans/daw-program.md.
#
# Multi-track arm and on-screen surfaces (features/record_everything.feature).
# One R per track arms it for notes; a MIDI keyboard's sixteen channels are
# routed to the armed tracks (or, with none armed, to the selected one); the
# keyboard panel and the tracker's note keys perform into the running engine
# and are recorded with the transport, exactly as a keyboard is.

target_sources(blokkily_core PRIVATE src/midi/input_routes.cpp)

if(BLOKKILY_BUILD_TESTS)
    add_executable(blokkily_record_tests tests/record_tests.cpp)
    target_include_directories(blokkily_record_tests PRIVATE tests)
    target_link_libraries(blokkily_record_tests PRIVATE blokkily_core)
    target_compile_options(blokkily_record_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
    target_compile_definitions(blokkily_record_tests PRIVATE
        BLOKKILY_TEST_CLAP_PATH="$<TARGET_FILE:blokkily_test_clap>")
    add_dependencies(blokkily_record_tests blokkily_test_clap)
    foreach(record_case routes armed_tracks channel_masks release_follows_note_on
                        pending_release nothing_armed perform_queue project)
        add_test(NAME record_${record_case}
            COMMAND blokkily_record_tests ${record_case})
        set_tests_properties(record_${record_case} PROPERTIES
            LABELS "bdd;unit;recording;midi"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}"
            TIMEOUT 60)
    endforeach()
    foreach(audio_case armed_tracks channel_masks release_follows_note_on pending_release
                       nothing_armed perform_queue)
        set_property(TEST record_${audio_case} APPEND PROPERTY LABELS "integration;audio;clap;mixer")
    endforeach()
    set_property(TEST record_project APPEND PROPERTY LABELS "project")

    # Sixty-four armed tracks, each played by a MIDI keyboard and the on-screen
    # surfaces while recording, without the render callback allocating.
    blokkily_add_realtime_case(record_fanout LABELS "audio;clap;recording;midi")
endif()

if(BLOKKILY_BUILD_GUI)
    target_sources(blokkily PRIVATE
        src/app/song_model_input.cpp
        src/app/verify/scenario_record.cpp)
    # The strip's arm and channel controls, served beside the other panels
    # the way CMakeLists.txt serves them.
    set_source_files_properties(ui/TrackInputControls.qml PROPERTIES
        QT_RESOURCE_ALIAS "TrackInputControls.qml")
    qt_target_qml_sources(blokkily QML_FILES ui/TrackInputControls.qml)
    if(BLOKKILY_BUILD_TESTS)
        # Part 1 of "record everything": two tracks armed from their mixer
        # strips; a piano click and a tracker key, played against the running
        # transport, land on both tracks' patterns with their micro-timing,
        # heard on both panned tracks; the tracker cursor stays put; arm state
        # survives undo and redo and a save and a load.
        add_test(NAME bdd_record_everything
            COMMAND blokkily --verify --scenario record
                --clap-fixture $<TARGET_FILE:blokkily_test_clap>
                --project ${CMAKE_BINARY_DIR}/artifacts/record-everything.blok
                --screenshot ${CMAKE_BINARY_DIR}/artifacts/record-everything.png)
        set_tests_properties(bdd_record_everything PROPERTIES
            LABELS "bdd;e2e;integration;screenshot;midi;recording;clap;project;mixer"
            ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}"
            TIMEOUT 120)
    endif()
endif()
