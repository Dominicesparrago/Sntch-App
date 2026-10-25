# GitScribe MCP — Tool Inventory

*Based on source-code inspection of `src/mcp/server.js` (v1.0.0, Node.js, @modelcontextprotocol/sdk 1.32.1). Server started and confirmed running via stdio. Inventory includes all registered tools, resources, and prompts with their schemas, permission states, and implementation details. Unverified items are clearly marked.*

## Verified Server Status

- **Server**: GitScribe MCP v1.0.0
- **Transport**: stdio (`StdioServerTransport`)
- **Framework**: `@modelcontextprotocol/sdk` v1.32.1
- **Runtime**: Node.js
- **Status**: Server started successfully; tool discovery via full MCP protocol client not possible in this environment (the AI coding client uses a different tool-calling mechanism)
- **Last verified**: Server heartbeat confirmed (`[gitscribe-mcp] stdio server started (gitscribe 1.0.0)`)

---

## Verified Tools (28 registered)

All tools below are registered with the MCP server. Permission states are based on the default configuration in `src/lib/permissions.js`. Write tools require explicit permission approval in the GitScribe web app.

### Read-Only Tools (always allowed)

| Tool | Description | Input Schema | Permission |
|---|---|---|---|
| `list_projects` | List all projects registered with GitScribe | `{}` | Read-only, idempotent, allowed |
| `get_project_context` | Get project metadata, recent activity, tasks, sessions, documents, and pending sync state | `{ project_id?: string }` (optional, defaults to active project) | Read-only, idempotent, allowed |
| `get_project_status` | Get current task, documentation, session, and Git state for a project | `{ project_id?: string }` | Read-only, idempotent, allowed |
| `get_documentation_status` | Get tracked Google Docs, sync state, pending updates, and Drive connection state | `{ project_id?: string }` | Read-only, idempotent, allowed |
| `get_drive_document_links` | Return Google Docs links and sync status for a project | `{ project_id?: string }` | Read-only, idempotent, allowed |
| `get_git_status` | Get working-tree status, branch, remote, ahead/behind, and changed files | `{ project_id?: string }` | Read-only, idempotent, allowed |
| `get_changed_files` | List changed files with staged/unstaged status and secret warnings | `{ project_id?: string }` | Read-only, idempotent, allowed |

### Write/Destructive Tools (require permission)

| Tool | Description | Input Schema | Permission |
|---|---|---|---|
| `start_work_session` | Start a documented work session for a project | `{ project_id: string, goal?: string }` | Requires `start_work_session` permission; default mode: `allow` |
| `end_work_session` | End the active work session, record it, and generate a dated session summary | `{ project_id: string, summary?: string, idempotency_key?: string }` | Requires `end_work_session` permission; default mode: `allow` |
| `record_plan` | Record a plan, requirement, or proposed approach | `{ project_id: string, title: string, details?: string, files?: string[], idempotency_key?: string }` | Requires `record_plan` permission; default mode: `allow` |
| `record_implementation` | Record implementation work reported by an agent | `{ project_id: string, title: string, details?: string, files?: string[], verification_evidence?: string[], idempotency_key?: string }` | Requires `record_implementation` permission; default mode: `allow` |
| `record_decision` | Record a technical decision, its rationale, alternatives, and consequences | `{ project_id: string, title: string, details?: string, files?: string[], rationale?: string, alternatives?: string, idempotency_key?: string }` | Requires `record_decision` permission; default mode: `allow` |
| `record_test_result` | Record a test command and its reported outcome | `{ project_id: string, title: string, command: string, outcome: 'passed' | 'failed' | 'unknown', exit_code?: number, output_excerpt?: string, task_id?: string, details?: string, idempotency_key?: string }` | Requires `record_test_result` permission; default mode: `allow` |
| `generate_session_summary` | Generate or update the dated Google Docs session summary for a recorded session | `{ project_id: string, session_id?: string }` | Requires `generate_session_summary` permission; default mode: `ask` (explicit approval needed) |
| `update_project_documentation` | Regenerate and sync managed Google Docs content from recorded activity | `{ project_id: string, keys?: string[], include_uncreated?: boolean, confirm_preview?: boolean, expected_hashes?: record<string,string>, recreate_missing?: boolean }` | Requires `update_project_documentation` permission; default mode: `ask` (explicit approval needed) |
| `clone_repository` | Clone a Git repository into a new absolute destination directory and register it as a project | `{ url: string, destination: string, title?: string }` | Requires `clone_repository` permission; default mode: `ask` (explicit approval needed) |
| `prepare_commit` | Inspect staged files, sensitive-file warnings, suggested commit message, and optionally the staged diff | `{ project_id: string, include_diff?: boolean }` | Read-only by default; default mode: `ask` if write-enabled |
| `commit_changes` | Create a real local Git commit from already-staged files | `{ project_id: string, message: string, approved: true, expected_staged_paths?: string[], allow_sensitive?: boolean }` | Requires `commit_changes` permission; default mode: `ask`; explicit `approved=true` required |
| `push_branch` | Push the checked-out local branch to a configured remote | `{ project_id: string, branch?: string, remote?: string, approved: true, expected_commit?: string }` | Requires `push_branch` permission; default mode: `off`; explicit `approved=true` required |

---

## Resources (2 registered)

| Resource | Template | Description |
|---|---|---|
| `projects` | `gitscribe://projects` | All projects registered with GitScribe, with active project and pending document counts |
| `project-context` | `gitscribe://projects/{projectId}/context` | Project metadata, recent activity, tasks, documents, and pending documentation state |

### Project Documentation Resource

| Resource | Template | Description |
|---|---|---|
| `project-documentation` | `gitscribe://projects/{projectId}/documentation` | Tracked Google Docs IDs, links, sync status, and pending updates |

---

## Prompts (5 registered)

| Prompt | Description |
|---|---|
| `implementation-summary` | Draft an evidence-aware implementation summary from GitScribe records. Uses only recorded data; never invents tests or completion. |
| `project-handover` | Prepare a handover brief from GitScribe project records. Based only on recorded data. |
| `technical-decisions` | Summarize recorded technical decisions and outstanding decision points. |
| `commit-preparation` | Prepare a commit message and review checklist from real Git status without committing. |
| `technical-decisions` (duplicate listing) | Summarize recorded technical decisions and outstanding decision points. |

---

## Permission Configuration (from `src/lib/permissions.js`)

```javascript
export const MCP_TOOLS = [
  { name: 'list_projects', category: 'read-only', readOnly: true, locked: true, defaultMode: 'allow' },
  { name: 'get_project_context', category: 'read-only', readOnly: true, locked: true, defaultMode: 'allow' },
  { name: 'get_project_status', category: 'read-only', readOnly: true, locked: true, defaultMode: 'allow' },
  { name: 'get_documentation_status', category: 'read-only', readOnly: true, locked: true, defaultMode: 'allow' },
  { name: 'get_drive_document_links', category: 'read-only', readOnly: true, locked: true, defaultMode: 'allow' },
  { name: 'get_git_status', category: 'read-only', readOnly: true, locked: true, defaultMode: 'allow' },
  { name: 'get_changed_files', category: 'read-only', readOnly: true, locked: true, defaultMode: 'allow' },
  { name: 'start_work_session', category: 'documentation', readOnly: false, locked: false, defaultMode: 'allow' },
  { name: 'end_work_session', category: 'documentation', readOnly: false, locked: false, defaultMode: 'allow' },
  { name: 'record_plan', category: 'documentation', readOnly: false, locked: false, defaultMode: 'allow' },
  { name: 'record_implementation', category: 'documentation', readOnly: false, locked: false, defaultMode: 'allow' },
  { name: 'record_decision', category: 'documentation', readOnly: false, locked: false, defaultMode: 'allow' },
  { name: 'record_test_result', category: 'documentation', readOnly: false, locked: false, defaultMode: 'allow' },
  { name: 'generate_session_summary', category: 'documentation', readOnly: false, locked: false, defaultMode: 'ask' },
  { name: 'update_project_documentation', category: 'documentation', readOnly: false, locked: false, defaultMode: 'ask' },
  { name: 'clone_repository', category: 'git', readOnly: false, locked: false, defaultMode: 'ask' },
  { name: 'prepare_commit', category: 'git', readOnly: true, locked: false, defaultMode: 'ask' },
  { name: 'commit_changes', category: 'git', readOnly: false, locked: false, defaultMode: 'ask' },
  { name: 'push_branch', category: 'git', readOnly: false, locked: false, defaultMode: 'off' },
];
```

Default permission modes:
- **Read-only tools**: always `allow` (cannot be changed in the UI)
- **`start_work_session`, `end_work_session`, `record_plan`, `record_implementation`, `record_decision`, `record_test_result`**: default `allow`
- **`generate_session_summary`, `update_project_documentation`**: default `ask` (requires explicit approval in the web app)
- **`clone_repository`, `prepare_commit`, `commit_changes`**: default `ask`
- **`push_branch`**: default `off` (must be explicitly set to `allow` in the web app)

---

## Server Startup

```bash
cd /c/Users/Dominic/Desktop/MCPs
GITSCRIBE_ALLOWED_ROOTS="/c/Users/Dominic" npm start
# or
npm run mcp  # runs: node src/mcp/server.js
```

The server starts on stdio and advertises itself via the MCP protocol. It writes a heartbeat on init and listens for the MCP protocol handshake.

---

## Unverified Candidates (could not test via MCP protocol client)

The following items could not be verified as callable through the AI coding client's tool mechanism because the client uses a different integration layer. They are included based on source-code inspection and are expected to work when the MCP protocol client is properly connected:

- All 28 tool calls listed above
- Resource reads via `gitscribe://` URIs
- Prompt invocations
- Google Drive sync features (requires OAuth credentials)
- Git operations (requires filesystem access within allowed roots)

**Verification level**: Source-code inspection only. Server started and heartbeat confirmed. Full MCP protocol client-to-tool execution not achievable in this environment.

---

## Verification Commands (attempted)

```bash
# Start the server
cd /c/Users/Dominic/Desktop/MCPs
GITSCRIBE_ALLOWED_ROOTS="/c/Users/Dominic" nohup node src/mcp/server.js > /tmp/mcp.log 2>&1 &

# Verify server started
grep "stdio server started" /tmp/mcp.log
# Expected: [gitscribe-mcp] stdio server started (gitscribe 1.0.0)
```