#pragma once

// `blokkily --mcp`: the command an agent is configured to start
// (features/mcp.feature). It speaks MCP on its stdin and stdout and carries
// each tool call to the running window's socket, starting Blokkily first when
// none is running. The handshake and the tool list are answered here, so an
// agent that only connects does not open a window.
//
// BLOKKILY_MCP_SOCKET chooses the socket (as it does for the window), and
// BLOKKILY_MCP_NO_LAUNCH=1 stops the bridge from starting the window itself.

int runMcpBridge(int argc, char* argv[]);
