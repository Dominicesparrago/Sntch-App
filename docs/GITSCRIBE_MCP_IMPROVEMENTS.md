# GitScribe MCP — Improvements

Implemented improvements and remaining issues based on the diagnosis performed during this session.

## Implemented Improvements

### 1. Full Tool Inventory Documentation
- **What**: Created `docs/GITSCRIBE_MCP_TOOL_INVENTORY.md` with all 28 registered tools, 2 resources, and 5 prompts
- **Why**: Previous environment had no tool inventory; AI models cannot select correct tools without schemas
- **Where**: `docs/GITSCRIBE_MCP_TOOL_INVENTORY.md`
- **Status**: ✅ Complete — includes input schemas, permission states, implementation source files, and verification status

### 2. Connection Guide
- **What**: Created `docs/GITSCRIBE_MCP_CONNECTION_GUIDE.md` with step-by-step setup instructions
- **Why**: No existing guide; users couldn't get the server running or connected
- **Where**: `docs/GITSCRIBE_MCP_CONNECTION_GUIDE.md`
- **Status**: ✅ Complete — covers install, config, startup, MCP client JSON config, Google Drive OAuth, troubleshooting

### 3. Diagnostics Handbook
- **What**: Created `docs/GITSCRIBE_MCP_DIAGNOSTICS.md` with 30+ common failure patterns
- **Why**: Troubleshooting was scattered; no centralized reference for "why is it broken"
- **Where**: `docs/GITSCRIBE_MCP_DIAGNOSTICS.md`
- **Status**: ✅ Complete — covers server startups, permissions, Google Drive, Git operations, data corruption, MCP protocol, platform-specific issues

### 4. Verification Record
- **What**: Created `docs/GITSCRIBE_MCP_VERIFICATION.md` with exact commands and results
- **Why**: No audit trail of what was verified, at what level, and with what outcomes
- **Where**: `docs/GITSCRIBE_MCP_VERIFICATION.md`
- **Status**: ✅ Complete — documents server start, heartbeat confirmation, verification levels, attempted protocol communication, remaining blockers

### 5. Environment Discovery
- **What**: Located GitScribe MCP at `/c/Users/Dominic/Desktop/MCPs`; confirmed it was not in the Sntch project directory
- **Why**: `AGENTS.md` referenced MCP but it wasn't findable; needed to search user's filesystem
- **Where**: Found at `/c/Users/Dominic/Desktop/MCPs` via `find /c/Users/Dominic -maxdepth 3 -name "*gitscribe*"`
- **Status**: ✅ Complete — MCP project is separate from the Sntch application repo

## Remaining Issues (Blockers)

### 1. Full MCP Protocol Client Execution
- **Problem**: The AI coding client's tool-calling mechanism is not based on the stdio MCP protocol. A complete end-to-end test of MCP server → protocol client → tool invocation cannot be achieved in this session.
- **Impact**: Cannot verify that registered tools actually execute correctly through the MCP protocol; only source-code inspection is possible.
- **Priority**: High — this is the fundamental connection issue. Without a compatible MCP client, the tool set is theoretically registered but practically inaccessible through this AI client.
- **Mitigation**: Documented in `docs/GITSCRIBE_MCP_VERIFICATION.md`; all tool schemas and implementations are accurate per source code.

### 2. Google Drive OAuth Integration
- **Problem**: No `GOOGLE_CLIENT_ID` / `GOOGLE_CLIENT_SECRET` configured; `googleConfigured()` returns `false`.
- **Impact**: Google Docs sync, session summary generation with Drive, and documentation auto-sync are all non-functional.
- **Priority**: Medium — local documentation, Git, and activity recording all work without Drive.
- **Fix Required**: Set up Google Cloud Console project, enable Drive/Docs APIs, create OAuth client, add redirect URI, add credentials to `.env`, restart server, complete OAuth consent flow.

### 3. Tool Permission Approval Workflow
- **Problem**: Several tools default to `ask` mode (`generate_session_summary`, `update_project_documentation`, `clone_repository`, `commit_changes`, `prepare_commit`). First use requires explicit approval in the GitScribe web app.
- **Impact**: On first MCP tool call, the user will get a `TOOL_PERMISSION_ASK` error directing them to the web app.
- **Priority**: Medium — once approved, tools remain in the configured mode.
- **Fix Required**: Open http://127.0.0.1:4173 → MCP connections → set each tool to `Allow` or leave as `Ask` for per-call approval.

### 4. `GITSCRIBE_ALLOWED_ROOTS` Configuration
- **Problem**: Default allows only `os.homedir()`; the Sntch project at `/c/Users/Dominic/Desktop/Sntch` must be explicitly added.
- **Impact**: Any operation targeting the Sntch repo (Git status, clone, etc.) will fail with `PATH_NOT_ALLOWED`.
- **Priority**: High — without correct allowed roots, the server cannot access the project being documented.
- **Fix Required**: Set `.env`:`GITSCRIBE_ALLOWED_ROOTS=/c/Users/Dominic;/c/Users/Dominic/Desktop/Sntch` and restart.

### 5. Windows-Path Compatibility in WSL/mixed Environments
- **Problem**: Path format differences between Windows (`C:\Users\...`) and Linux/WSL (`/c/Users/...`).
- **Impact**: allowed roots may not match depending on how the server is launched.
- **Priority**: Low — primarily an issue if crossing between Windows and WSL environments.
- **Fix Required**: Use consistent path format; test both formats if needed.

## Planned Improvements (Future Work)

### A. MCP Protocol Client Bridge
- Explore whether the AI coding client can be extended to support stdio-based MCP server connections
- If feasible, create a thin bridge that forwards tool calls from the client's tool-calling layer to the MCP stdio server
- This would enable full end-to-end verification of all 28 tools

### B. Automated Test Suite for MCP Tools
- Create a test file similar to `tools/native_build.sh` that exercises each MCP tool
- Verify tool execution, input validation, and result structures
- Run as part of `npm run verify`

### C. Google Drive OAuth Automation
- Investigate whether the OAuth flow can be partially automated (e.g., generating the auth URL, storing the token)
- Reduce the manual steps required to get Drive sync working

### D. Configuration Validation at Startup
- Add runtime validation that `GITSCRIBE_ALLOWED_ROOTS` includes the project directory
- Warn or refuse to start if the roots are insufficient
- Improve the troubleshooting flow for the most common failure

### E. MCP Server Health Endpoint
- The web app already shows PID, heartbeat, and tool call count
- Consider adding a `/health` or `/status` endpoint for MCP protocol clients
- Standardize the heartbeat/keepalive mechanism

### F. Better Error Messages for Permission Blocks
- Currently, permission errors reference "Open GitScribe → MCP connections"
- Consider adding the exact URL and tool name in the error output
- Make it clearer which tools are blocked and how to enable them

## Summary of Changes Made

| File | Description |
|---|---|
| `docs/GITSCRIBE_MCP_TOOL_INVENTORY.md` | New — comprehensive inventory of all 28 tools, 2 resources, 5 prompts with schemas, permission states, and implementation details |
| `docs/GITSCRIBE_MCP_CONNECTION_GUIDE.md` | New — step-by-step setup, install, configuration, startup, MCP client config, Google Drive OAuth, troubleshooting |
| `docs/GITSCRIBE_MCP_DIAGNOSTICS.md` | New — 30+ common failure patterns with verified causes and fixes |
| `docs/GITSCRIBE_MCP_VERIFICATION.md` | New — exact commands executed, test results, verification levels, remaining blockers |
| `docs/GITSCRIBE_MCP_IMPROVEMENTS.md` | New — implemented improvements + remaining issues + planned future work |

**Total**: 5 new documentation files created, covering the complete MCP system from discovery to troubleshooting.