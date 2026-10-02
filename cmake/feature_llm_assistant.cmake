# Feature fragment: the LLM composition assistant. A producer describes what
# they want in musical words; the assistant shows the session to a model
# (local Ollama by default; Gemini, MiniMax, ChatGPT or OpenRouter on
# request), checks the reply, and writes
# the approved proposal into the canonical pattern as one undo step.
# features/llm_assistant.feature maps each scenario to a check below.
#
# The network is never a test dependency: the unit tests parse and apply
# canned replies, and the BDD gate drives the rendered prompt bar against a
# scripted backend answering from tests/fixtures/llm_script.jsonl.

if(BLOKKILY_BUILD_GUI)
    # The wire format, the backends, the prompt, the model bridge, and the
    # verify driver's `--scenario llm`.
    target_sources(blokkily PRIVATE
        src/llm/trigger_json.cpp
        src/llm/trigger_json.hpp
        src/llm/llm_backend.hpp
        src/llm/llm_backend.cpp
        src/llm/http_cassette.cpp
        src/llm/http_cassette.hpp
        src/llm/openai_compatible_backend.cpp
        src/llm/ollama_backend.cpp
        src/llm/gemini_backend.cpp
        src/llm/scripted_backend.cpp
        src/llm/scripted_backend.hpp
        src/llm/system_prompt.cpp
        src/llm/system_prompt.hpp
        src/app/llm_model.cpp
        src/app/llm_model.hpp
        src/app/verify/scenario_llm.cpp
        src/app/verify/scenario_llm_live.cpp)
    target_include_directories(blokkily PRIVATE src/llm)
    target_link_libraries(blokkily PRIVATE Qt6::Network)
    set_source_files_properties(ui/PromptBar.qml PROPERTIES
        QT_RESOURCE_ALIAS PromptBar.qml)
    qt_target_qml_sources(blokkily QML_FILES ui/PromptBar.qml)
endif()

if(BLOKKILY_BUILD_TESTS AND BLOKKILY_BUILD_GUI)
    # The wire format against canned replies: valid JSON of each mode,
    # prose-wrapped replies, out-of-range fields, triggers outside the
    # pattern, and a serialize/parse round trip; plus the scripted backend
    # the e2e gate runs on; and every live backend's request, reply and
    # error path against a capturing network. Applying, undo and scale snapping are verified
    # end to end against the live models by bdd_llm_assistant, the way every
    # GUI-model behavior in this suite is. No server, no network.
    add_executable(blokkily_llm_tests tests/llm_assistant_tests.cpp
        src/llm/trigger_json.cpp
        src/llm/llm_backend.cpp
        src/llm/http_cassette.cpp
        src/llm/openai_compatible_backend.cpp
        src/llm/scripted_backend.cpp
        src/llm/system_prompt.cpp
        src/llm/ollama_backend.cpp
        src/llm/gemini_backend.cpp)
    target_include_directories(blokkily_llm_tests PRIVATE src/llm tests)
    target_link_libraries(blokkily_llm_tests PRIVATE blokkily_core
        Qt6::Core Qt6::Network)
    target_compile_options(blokkily_llm_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
    # Each live backend runs every scenario that applies to it: a keyless
    # Ollama has no missing-key case, Gemini's endpoint has no URL override.
    set(llm_backend_cases builds_request parses_reply network_error_is_readable
        server_error_is_readable)
    set(llm_cases parse_replace parse_add_and_modify parse_fenced_and_padded parse_skips_reasoning
        parse_rejects_prose parse_clamps_and_repairs parse_drops_out_of_pattern
        roundtrip_triggers scripted_backend_answers prompt_documents_schema
        cassette_records_and_replays cassette_unrecorded_request_fails
        cassette_scrubs_credentials)
    foreach(vendor minimax openai openrouter gemini ollama)
        foreach(backend_case ${llm_backend_cases})
            list(APPEND llm_cases ${vendor}_backend_${backend_case})
        endforeach()
        if(NOT vendor STREQUAL "ollama")
            list(APPEND llm_cases ${vendor}_backend_missing_key_errors)
        endif()
        if(NOT vendor STREQUAL "gemini")
            list(APPEND llm_cases ${vendor}_backend_uses_custom_url_and_model)
        endif()
    endforeach()
    foreach(llm_case ${llm_cases})
        add_test(NAME llm_${llm_case}
            COMMAND blokkily_llm_tests ${llm_case})
        set_tests_properties(llm_${llm_case} PROPERTIES
            LABELS "unit;bdd;llm"
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;${BLOKKILY_HEADLESS_TEST_ENV}"
            TIMEOUT 30)
    endforeach()
endif()

if(BLOKKILY_BUILD_GUI AND BLOKKILY_BUILD_TESTS)
    # The rendered prompt bar, driven with keys and clicks: a prompt typed,
    # the scripted reply proposed, applied through the real buttons, seen on
    # every editor, taken back with undo, and a screenshot.
    add_test(NAME bdd_llm_assistant
        COMMAND blokkily --verify --scenario llm
            --clap-fixture $<TARGET_FILE:blokkily_test_clap>
            --screenshot ${CMAKE_BINARY_DIR}/artifacts/llm-assistant.png)
    set_tests_properties(bdd_llm_assistant PROPERTIES
        LABELS "bdd;e2e;integration;screenshot;llm"
        ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV};BLOKKILY_LLM_SCRIPT=${CMAKE_SOURCE_DIR}/tests/fixtures/llm_script.jsonl"
        TIMEOUT 120)
endif()

if(BLOKKILY_BUILD_GUI AND BLOKKILY_BUILD_TESTS)
    # A live model, replayed: scripts/record-llm-cassettes.sh drives
    # `--scenario llm_live` against a provider for real and keeps every
    # exchange in tests/fixtures/llm_cassettes/<backend>.jsonl; each committed
    # cassette becomes a gate that drives the same scenario through the
    # production backend, its request building and reply parsing, answered
    # from the cassette. No key, no network: the key variable only has to be
    # set for the backend to send, and the URL and model are pinned to the
    # defaults the cassette was recorded against.
    foreach(backend minimax chatgpt openrouter gemini ollama)
        set(cassette ${CMAKE_SOURCE_DIR}/tests/fixtures/llm_cassettes/${backend}.jsonl)
        if(NOT EXISTS ${cassette})
            continue()
        endif()
        string(TOUPPER ${backend} vendor)
        if(backend STREQUAL "chatgpt")
            set(vendor OPENAI)
        endif()
        add_test(NAME bdd_llm_live_${backend}
            COMMAND blokkily --verify --scenario llm_live
                --clap-fixture $<TARGET_FILE:blokkily_test_clap>
                --screenshot ${CMAKE_BINARY_DIR}/artifacts/llm-live-${backend}.png)
        set_tests_properties(bdd_llm_live_${backend} PROPERTIES
            LABELS "bdd;e2e;integration;screenshot;llm"
            ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV};BLOKKILY_LLM_BACKEND=${backend};BLOKKILY_LLM_CASSETTE=${cassette};BLOKKILY_LLM_CASSETTE_MODE=replay;BLOKKILY_${vendor}_KEY=replayed;BLOKKILY_${vendor}_URL=;BLOKKILY_${vendor}_MODEL="
            TIMEOUT 120)
    endforeach()
endif()
