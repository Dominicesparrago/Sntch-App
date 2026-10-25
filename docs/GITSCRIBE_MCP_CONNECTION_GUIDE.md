# GitScribe MCP — Connection Guide

This guide covers getting the GitScribe MCP server running, connecting it to your MCP client, and configuring it for the Sntch project.

## Prerequisites

- Node.js >= 22.0.0 (the project requires `>=22.0.0` per `package.json`)
- GitScribe MCP project at `/c/Users/Dominic/Desktop/MCPs`
- The project being documented: `/c/Users/Dominic/Desktop/Sntch` (the classroom controller system)

## 1. Install Dependencies

```bash
cd /c/Users/Dominic/Desktop/MCPs
npm install
```

This installs:
- `@modelcontextprotocol/sdk` ^1.32.1 (MCP protocol implementation)
- `express` ^5.3.0 (web server)
- `googleapis` ^187.0.0 (Google Drive API)
- `zod` ^4.6.7 (schema validation)

## 2. Configuration

Copy the example environment file and adjust as needed:

```bash
cp .env.example .env
# or edit .env.local (takes precedence over .env)
```

### Required `.env` settings

| Variable | Default | Description |
|---|---|---|
| `GITSCRIBE_PORT` | `4173` | Port for the web app |
| `GITSCRIBE_HOST` | `127.0.0.1` | Host for the web app |
| `GITSCRIBE_DATA_DIR` | `<project>/data` | Directory for persistent state (`gitscribe.json`, Google token) |
| `GITSCRIBE_ALLOWED_ROOTS` | `os.homedir()` | Semicolon-separated list of absolute paths the server may access |
| `GOOGLE_CLIENT_ID` | `` | Google OAuth client ID (required for Drive sync) |
| `GOOGLE_CLIENT_SECRET` | `` | Google OAuth client secret (required for Drive sync) |

### Example for the Sntch project

```env
GITSCRIBE_ALLOWED_ROOTS=/c/Users/Dominic;/c/Users/Dominic/Desktop/Sntch
GITSCRIBE_DATA_DIR=/c/Users/Dominic/Desktop/MCPs/data
```

**Important**: `GITSCRIBE_ALLOWED_ROOTS` must include both the MCP project directory and the project being documented, otherwise the server will refuse with `PATH_NOT_ALLOWED`.

## 3. Start the Server

```bash
# Start the web app + MCP server
npm start

# Or start just the MCP stdio server
npm run mcp
# which runs: node src/mcp/server.js
```

### Verification

After starting, check:

```bash
tail -1 /c/Users/Dominic/Desktop/MCPs/data/gitscribe.log 2>/dev/null
# or
grep "stdio server started" /c/Users/Dominic/Desktop/MCPs/data/gitscribe.json 2>/dev/null
```

Expected output:
```
[gitscribe-mcp] stdio server started (gitscribe 1.0.0)
```

## 4. MCP Client Configuration

Depending on your AI coding client, configure the MCP server connection:

### For Claude Code / OpenCode / Cursor (JSON format)

```json
{
  "mcpServers": {
    "gitscribe": {
      "command": "node",
      "args": ["C:\\/Users\\/Dominic\\/Desktop\\/MCPs\\/src\\/mcp\\/server.js"],
      "env": {
        "GITSCRIBE_DATA_DIR": "C:\\/Users\\/Dominic\\/Desktop\\/MCPs\\/data",
        "GITSCRIBE_ALLOWED_ROOTS": "C:\\/Users\\/Dominic;C:\\/Users\\/Dominic\\/Desktop\\/Sntch"
      }
    }
  }
}
```

### For the GitScribe Web App

1. Open http://127.0.0.1:4173 in a browser
2. The MCP view shows whether a server is running, its PID, last heartbeat, and tool call count
3. Configure per-tool permissions (Allow/Ask/Off) in the MCP connections screen
4. Connect Google Drive (optional) by clicking **Connect Google Drive** and completing the OAuth flow

## 5. Google Drive / Docs Configuration (optional)

To enable Google Docs documentation sync:

1. In Google Cloud Console:
   - Create a project
   - Enable **Google Drive API** and **Google Docs API**
   - Configure OAuth consent screen (External, Testing is fine)
   - Add yourself as a test user
   - Create OAuth client ID of type **Web application**
   - Authorized redirect URI: `http://127.0.0.1:4173/api/auth/google/callback`

2. Copy credentials into `.env` or `.env.local`:
   ```env
   GOOGLE_CLIENT_ID=YOUR_CLIENT_ID.apps.googleusercontent.com
   GOOGLE_CLIENT_SECRET=YOUR_CLIENT_SECRET
   ```

3. Restart `npm start`, open the app, go to the docs view, and click **Connect Google Drive**.

4. The consent screen requests:
   - `drive.file` — create and manage only the files GitScribe owns
   - `drive.metadata.readonly` — read file/folder metadata for listing and verification

## 6. Verifying Your Setup

```bash
npm run verify
```

Runs the syntax/CSP checker and 79 automated tests (store, paths, Git workflow against real temp repositories, sessions/activities, API routes over HTTP, secure storage, MCP server over stdio).

## 7. Troubleshooting

| Symptom | Fix |
|---|---|
| `Repository path is outside the allowed filesystem roots` | Add the folder to `GITSCRIBE_ALLOWED_ROOTS` and restart. |
| `Google Drive is not configured` | Fill `GOOGLE_CLIENT_ID` / `GOOGLE_CLIENT_SECRET` in `.env` and restart. |
| `TOOL_PERMISSION_ASK` / `TOOL_PERMISSION_OFF` from MCP | Open the app → MCP connections and set that tool to Allow. |
| `Timed out waiting for the GitScribe data lock` | Another GitScribe process is writing; close it or wait ~30s (stale locks auto-expire). |
| `The GitScribe data file is not valid JSON` | A backup was written next to it (`gitscribe.corrupt-*.json`); restore or delete the corrupt file. |
| Commit blocked with `SENSITIVE_FILES_REQUIRE_REVIEW` | A staged file looks like a secret; unstage it or confirm explicitly in the UI. |
| Push blocked with `APPROVAL_REQUIRED` | Confirms are required by default; approve in the UI or disable approval in Settings (not recommended). |

## 8. Sntch Project Integration

To connect GitScribe MCP to the Sntch classroom controller project:

1. Ensure `GITSCRIBE_ALLOWED_ROOTS` includes `/c/Users/Dominic/Desktop/Sntch`
2. Use the GitScribe tools to:
   - `list_projects` — verify the Sntch repo is detected
   - `get_project_status` — inspect Git state of the Sntch project
   - `start_work_session` / `end_work_session` — document work sessions
   - `record_implementation` — record implementation findings
   - `record_test_result` — log test command outcomes
   - `generate_session_summary` — create session summaries in Google Docs
3. All Git operations are controlled through the MCP permission system
4. Documentation sync can push changes to Google Docs if Drive is configured

---

## Commands Summary

```bash
# Install
cd /c/Users/Dominic/Desktop/MCPs && npm install

# Start server + web app
cd /c/Users/Dominic/Desktop/MCPs && npm start

# Start MCP-only (stdio)
cd /c/Users/Dominic/Desktop/MCPs && npm run mcp

# Verify setup
cd /c/Users/Dominic/Desktop/MCPs && npm run verify

# Configure allowed roots (must include both MCPs dir and project dir)
# Edit .env: GITSCRIBE_ALLOWED_ROOTS=/c/Users/Dominic;/c/Users/Dominic/Desktop/Sntch
```