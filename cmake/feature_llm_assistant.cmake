# Feature fragment: the LLM composition assistant. A producer describes what
# they want in musical words; the assistant shows the session to a model
# (local Ollama by default, Gemini on request), checks the reply, and writes
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
        src/llm/openai_compatible_backend.hpp
        src/llm/openai_compatible_backend.cpp
        src/llm/ollama_backend.cpp
        src/llm/gemini_backend.cpp
        src/llm/minimax_backend.cpp
        src/llm/minimax_backend.hpp
        src/llm/openai_backend.cpp
        src/llm/openai_backend.hpp
        src/llm/openrouter_backend.cpp
        src/llm/openrouter_backend.hpp
        src/llm/scripted_backend.cpp
        src/llm/scripted_backend.hpp
        src/llm/system_prompt.cpp
        src/llm/system_prompt.hpp
        src/app/llm_model.cpp
        src/app/llm_model.hpp
        src/app/verify/scenario_llm.cpp)
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
    # the e2e gate runs on. Applying, undo and scale snapping are verified
    # end to end against the live models by bdd_llm_assistant, the way every
    # GUI-model behavior in this suite is. No server, no network.
    add_executable(blokkily_llm_tests tests/llm_assistant_tests.cpp
        src/llm/trigger_json.cpp
        src/llm/llm_backend.cpp
        src/llm/openai_compatible_backend.cpp
        src/llm/scripted_backend.cpp
        src/llm/system_prompt.cpp
        src/llm/ollama_backend.cpp
        src/llm/gemini_backend.cpp
        src/llm/minimax_backend.cpp
        src/llm/minimax_backend.hpp
        src/llm/openai_backend.cpp
        src/llm/openai_backend.hpp
        src/llm/openrouter_backend.cpp
        src/llm/openrouter_backend.hpp)
    target_include_directories(blokkily_llm_tests PRIVATE src/llm tests)
    target_link_libraries(blokkily_llm_tests PRIVATE blokkily_core
        Qt6::Core Qt6::Network)
    target_compile_options(blokkily_llm_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
    foreach(llm_case
            parse_replace
            parse_add_and_modify
            parse_fenced_and_padded
            parse_rejects_prose
            parse_clamps_and_repairs
            parse_drops_out_of_pattern
            roundtrip_triggers
            scripted_backend_answers
            prompt_documents_schema
            minimax_backend_builds_request
            minimax_backend_parses_reply
            minimax_backend_missing_key_errors
            minimax_backend_network_error_is_readable
            minimax_backend_server_error_is_readable
            minimax_backend_uses_custom_url_and_model
            openai_backend_builds_request
            openai_backend_missing_key_errors
            openai_backend_network_error_is_readable
            openai_backend_server_error_is_readable
            openrouter_backend_builds_request
            openrouter_backend_missing_key_errors
            openrouter_backend_network_error_is_readable
            openrouter_backend_server_error_is_readable)
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
