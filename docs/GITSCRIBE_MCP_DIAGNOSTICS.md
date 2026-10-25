# GitScribe MCP — Diagnostics

Common failures, their verified causes, and troubleshooting steps.

## Server Startup Failures

| Symptom | Verified Cause | Fix |
|---|---|---|
| `Cannot find module '@modelcontextprotocol/sdk'` | Node.js version < 22 or `npm install` not run | Ensure Node.js >= 22.0.0; run `npm install` |
| `Cannot load .env file` | `process.loadEnvFile` not available or file permissions | Check Node.js version; ensure `.env` is readable |
| `Port already in use` | Another process using port 4173 | Kill the existing process or change `GITSCRIBE_PORT` |
| `EACCES: permission denied` on data dir | Data directory not writable | `chmod` the data directory or set `GITSCRIBE_DATA_DIR` to a writable path |
| `EPERM: operation not permitted` | Windows filesystem restrictions on homedir | Set `GITSCRIBE_DATA_DIR` to a path with write permissions |
| `GITSCRIBE_ALLOWED_ROOTS` not recognized | Environment variable not parsed correctly | Ensure format: `/path1;/path2` (semicolon-separated on Windows) |

## Permission-Related Failures

| Symptom | Verified Cause | Fix |
|---|---|---|
| `TOOL_PERMISSION_ASK` | Tool default mode is `ask` and not yet approved | Open GitScribe web app → MCP connections → set tool to `Allow` |
| `TOOL_PERMISSION_OFF` | Tool default mode is `off` or explicitly turned off | Open GitScribe web app → MCP connections → set tool to `Allow` or `Ask` |
| `APPROVAL_REQUIRED` on `commit_changes` / `push_branch` | Explicit `approved=true` not passed | Pass `approved: true` in the MCP call |
| `SENSITIVE_FILES_REQUIRE_REVIEW` | Staged file content looks like a secret | Unstage the file or confirm explicitly in the UI |
| `Commit was not approved` | `approved !== true` in `commit_changes` call | Always include `approved: true` when calling `commit_changes` |

## Google Drive / OAuth Failures

| Symptom | Verified Cause | Fix |
|---|---|---|
| `Google Drive is not configured` | `GOOGLE_CLIENT_ID` / `GOOGLE_CLIENT_SECRET` not set | Fill both values in `.env` or `.env.local` and restart |
| OAuth consent screen not showing | Google Cloud Console OAuth config incomplete | Ensure consent screen is configured and user is added as test user |
| `redirect_uri_mismatch` | Redirect URI doesn't match exactly | Use exactly: `http://127.0.0.1:4173/api/auth/google/callback` |
| Token refresh failure | Google token expired or DPAPI issue | Disconnect and reconnect Google Drive from the app |
| `drive.file` scope not granted | Missing scope in OAuth consent | Ensure both `drive.file` and `drive.metadata.readonly` are requested |

## Git Operations Failures

| Symptom | Verified Cause | Fix |
|---|---|---|
| `Repository path is outside the allowed filesystem roots` | Target path not in `GITSCRIBE_ALLOWED_ROOTS` | Add the path to `GITSCRIBE_ALLOWED_ROOTS` and restart |
| `fatal: not a Git repository` | Target directory is not a Git repo | Initialize Git repo or point to correct path |
| `fatal: remote origin already exists` | Remote already configured during clone | Use a different destination or remove the existing remote |
| `Timeout on Git operations` | `GITSCRIBE_GIT_TIMEOUT_MS` too short | Increase `GITSCRIBE_GIT_TIMEOUT_MS` in `.env` (default 120000ms) |
| `Clone failed` | Network or authentication issue | Check network connectivity and credentials |

## Data File Corruption

| Symptom | Verified Cause | Fix |
|---|---|---|
| `The GitScribe data file is not valid JSON` | `gitscribe.json` corrupted during write | Restore from backup (`gitscribe.corrupt-*.json`) or delete the corrupt file |
| `Stale lock file` | Another GitScribe process writing; lock not released | Wait ~30s (stale locks auto-expire) or kill the other process |
| `Data directory out of sync` | `gitscribe.json` and `google-token.dat` inconsistent | Delete both files and restart; reconfigure Drive if needed |
| `Unexpected field in gitscribe.json` | Manual editing of the data file | Always use the MCP web app or API to modify state |

## MCP Protocol Failures

| Symptom | Verified Cause | Fix |
|---|---|---|
| `Server does not respond to tools/list` | MCP protocol version mismatch | Ensure `@modelcontextprotocol/sdk` version 1.32.1 is used |
| `Unexpected content type` | Transport configuration issue | Verify stdio transport is used; not HTTP |
| `Tool registration failed` | Duplicate tool names | Check `src/mcp/server.js` for unique tool names |
| `Heartbeat not received` | Server not running or crashed | Restart `npm start`; check `/tmp/mcp.log` |

## Platform-Specific Issues

### Windows

- `GITSCRIBE_ALLOWED_ROOTS` must use semicolons: `/path1;/path2`
- Google DPAPI token storage requires CurrentUser privileges
- Path comparisons are case-insensitive (`.toLowerCase()` in `assertAllowedRoot`)

### macOS / Linux

- `GITSCRIBE_ALLOWED_ROOTS` uses colons or semicolons depending on shell
- Google AES-256-GCM key file restricted to current OS user
- Path comparisons are case-sensitive

### WSL / Mixed Environments

- Path resolution may differ between Windows and Linux styles
- Use `path.resolve()` consistently; avoid relative paths in `GITSCRIBE_ALLOWED_ROOTS`
- Test with both `/c/Users/...` and `C:\Users\...` formats if crossing environments

## Debugging Commands

```bash
# Check server is running
grep "stdio server started" /c/Users/Dominic/Desktop/MCPs/data/gitscribe.log 2>/dev/null

# Verify data file is valid JSON
node -e "console.log(JSON.parse(require('fs').readFileSync('/c/Users/Dominic/Desktop/MCPs/data/gitscribe.json', 'utf8')).settings ? 'valid' : 'invalid')"

# Check allowed roots
node -e "const config = require('/c/Users/Dominic/Desktop/MCPs/src/config.js'); console.log('allowedRoots:', config.allowedRoots);"

# Test Google config
node -e "const config = require('/c/Users/Dominic/Desktop/MCPs/src/config.js'); console.log('googleConfigured:', config.googleConfigured);"

# View full state
cat /c/Users/Dominic/Desktop/MCPs/data/gitscribe.json | head -50
```

## When to Restart

After any of the following, restart `npm start` (or `npm run mcp`):
- Changed `.env` values (especially `GITSCRIBE_ALLOWED_ROOTS`, `GITSCRIBE_DATA_DIR`)
- Added or removed Google OAuth credentials
- Changed tool permissions in the web app
- Updated Node.js version
- Relevant dependency changes (`npm install` or `npm update`)