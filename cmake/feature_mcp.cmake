# Feature fragment: mcp. See features/mcp.feature.
#
# The running window serves the Model Context Protocol on a local socket
# (src/app/mcp_server.cpp, tools in mcp_tools.cpp), and `blokkily --mcp`
# (mcp_bridge.cpp) is the stdio server an agent starts, carrying each call to
# it. The protocol itself (mcp_protocol.cpp) needs Qt Core alone.

if(BLOKKILY_BUILD_GUI)
    target_sources(blokkily PRIVATE
        src/app/mcp_protocol.cpp
        src/app/mcp_protocol.hpp
        src/app/mcp_tools.cpp
        src/app/mcp_tools.hpp
        src/app/mcp_server.cpp
        src/app/mcp_server.hpp
        src/app/mcp_bridge.cpp
        src/app/mcp_bridge.hpp
        src/app/verify/scenario_mcp.cpp)
    target_link_libraries(blokkily PRIVATE Qt6::Network)
    set_source_files_properties(src/app/mcp_protocol.cpp PROPERTIES
        COMPILE_DEFINITIONS "BLOKKILY_VERSION=\"${PROJECT_VERSION}\"")
endif()

if(BLOKKILY_BUILD_GUI AND BLOKKILY_BUILD_TESTS)
    # An agent's whole conversation through the real `blokkily --mcp` bridge:
    # the handshake, the tool list, notes written while the song plays and
    # heard from the production callback, the window's methods called and
    # undone, refusals, the agent's screenshot (mcp.png), and the bridge with
    # no window to reach.
    add_test(NAME bdd_mcp
        COMMAND blokkily --verify --scenario mcp
            --clap-fixture $<TARGET_FILE:blokkily_test_clap>
            --soundfont-fixture ${BLOKKILY_TEST_SF2}
            --mcp-socket blokkily-mcp-gate
            --screenshot ${CMAKE_BINARY_DIR}/artifacts/mcp.png)
    set_tests_properties(bdd_mcp PROPERTIES
        LABELS "bdd;e2e;integration;screenshot;clap;soundfont;mcp"
        ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}"
        TIMEOUT 120)
endif()
