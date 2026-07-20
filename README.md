# upp_patchtrack
Cross-platform transactional patching engine for MCP-driven AI editing on Linux, macOS, and Windows.

PatchTrack is being built first as an MCP system tool for AI agents. The repo currently ships three packages:
- `patchtrack_core`: shared engine
- `patchtrack`: console frontend
- `patchtrack_mcp`: stdio MCP frontend

The first intended MCP hosts are Codex, OpenCode, and Hermes. The console frontend remains useful for debugging and verification, but the product surface is the MCP server.

The purpose is to fill the gap between brittle one-shot patch/apply tools and a trustworthy transactional edit layer. In practice that means structured edits, staged validation, explicit journal state, recovery data, soft concurrency claims, and actionable diagnostics when something goes wrong.

## What PatchTrack Is Trying To Solve
PatchTrack exists to reduce the brittleness of ad hoc AI file editing.

The problems it is trying to address are:
- partial or malformed text replacements that leave code syntactically damaged;
- transport-layer failures where an edit never actually reached the engine;
- patch attempts that succeed on one file and fail half-way through a multi-file change;
- weak rollback stories that depend on the caller improvising reverse edits;
- poor diagnostics that only say `WRITE_FAILED` or `READ_FAILED` without enough context to recover confidently;
- overlapping AI writes that silently race each other.

The core design idea is simple: the caller describes edits structurally, the engine validates and stages them, and only then commits them with transaction tracking and rollback metadata.

## Current Architecture
Current layout:
- `patchtrack_core` owns the engine, journaling, validation, recovery scan, claim handling, and platform filesystem code.
- `patchtrack` is a thin CLI frontend over the exported core APIs.
- `patchtrack_mcp` is a dedicated stdio MCP frontend over the same core APIs.

This is the point of the refactor: MCP is no longer planned as a wrapper around a CLI-shaped public surface. Both frontends now call the same core entry points directly.

## Console Surface
Current CLI commands:
- `preview`
- `apply`
- `rollback`
- `hash`
- `history`
- `recovery_scan`
- `selftest`

Typical commands:

```powershell
patchtrack preview request.json
patchtrack apply request.json
patchtrack rollback rollback.json
patchtrack hash src\main.cpp
patchtrack history E:\my-workspace
patchtrack recovery_scan E:\my-workspace
patchtrack selftest
```

Typical CLI workflow:
1. Read the target file and compute its current hash.
2. Build a request JSON with `workspace_root`, `summary`, `actor`, and `edits`.
3. Run `patchtrack preview request.json`.
4. Inspect the diff and validation result.
5. Run `patchtrack apply request.json`.
6. If needed, run `patchtrack rollback rollback.json` using the stored transaction id.

## MCP Surface
`patchtrack_mcp` is the first-class MCP stdio frontend over `patchtrack_core`.
PatchTrack `1.1.0` advertises its version in `initialize.serverInfo` and through
the read-only `version` tool. This gives hosts and agents a reliable way to
inspect the active edit contract before they start moving files around.

Target host integrations:
- Codex
- OpenCode
- Hermes

Host setup notes live in `integration/MCP_HOSTS.md`.

### Install In Codex Or OpenCode

Build `patchtrack_mcp` first, then register one argument-free stdio server named
`patchtrack`. Do not use `--oneshot` outside test harnesses.

Codex (`%USERPROFILE%\\.codex\\config.toml`):

```toml
[mcp_servers.patchtrack]
command = "E:\\apps\\github\\upp_patchtrack\\build\\patchtrack_mcp.exe"
args = []
```

OpenCode (`C:\\Users\\<user>\\.config\\opencode\\opencode.json` by default,
or `$XDG_CONFIG_HOME\\opencode\\opencode.json` when `XDG_CONFIG_HOME` is set):

```json
{
  "mcpServers": {
    "patchtrack": {
      "command": "E:\\apps\\github\\upp_patchtrack\\build\\patchtrack_mcp.exe",
      "args": [],
      "env": {}
    }
  }
}
```

Restart the host, inspect its MCP tool list, then call `version`. Full host
notes, templates, permissions, and a stdio smoke command are in
`integration/MCP_HOSTS.md` and `integration/host_configs/`.

Raw request/response examples live in `integration/MCP_REQUEST_RESPONSE_EXAMPLES.md`.

The v1 acceptance procedure lives in `integration/V1_RELEASE_CHECKLIST.md`.

Current MCP tools:
- `version`
- `preview`
- `apply`
- `rollback`
- `hash`
- `history`
- `recovery_scan`

Hosts may display these with a server prefix such as `patchtrack_hash`; that prefix is host-side decoration, not part of the canonical MCP tool name.

The MCP server speaks JSON-RPC 2.0 and returns tool results with both plain text content and `structuredContent`, so agents can read a human-friendly result while still consuming structured engine data.

Current helper modes:
- `patchtrack_mcp --selftest`
- `patchtrack_mcp --oneshot request.json`

`--oneshot` exists mainly for harness-driven integration testing.

## MCP Tool Usage
### `preview` and `apply`
Required arguments:
- `workspace_root`
- `edits`

Optional arguments:
- `summary`
- `actor`
- `session`
- `validation`
- `testing`

Minimal example:

```json
{
  "workspace_root": "E:\\repo",
  "summary": "rename greeting",
  "actor": "agent",
  "edits": [
    {
      "op": "replace_exact",
      "file": "src/sample.txt",
      "find": "hello\n",
      "text": "hi\n"
    }
  ]
}
```

Natural-language mapping:
- `Replace beta with BETA in one file` becomes `op: replace_exact`, `find: beta`, `text: BETA`.
- Hash the file first and pass the returned digest as `expected_sha256` when you want a guarded edit.
- Use `session` as an object when you need stable host metadata or rollback tracing, and give `session.id` a `sess-` prefix.

### `rollback`
Required arguments:
- `workspace_root`
- `transaction_id`

Optional arguments:
- `actor`
- `session`

Example:

```json
{
  "workspace_root": "E:\\repo",
  "transaction_id": "tran-abc123",
  "actor": "agent"
}
```

### `hash`
Required arguments:
- `path`

The structured MCP result includes the raw `sha256`, a
`normalized_sha256`, and the observed newline style. Pass both hash values to
an edit when possible so PatchTrack can identify newline-only drift separately
from a real content change.

Example:

```json
{
  "path": "E:\\repo\\src\\sample.txt"
}
```

### `history` and `recovery_scan`
Required arguments:
- `workspace_root`

Example:

```json
{
  "workspace_root": "E:\\repo"
}
```

## Supported Edit Operations
Current canonical edit operations:
- `replace_exact`
- `replace_all_exact`
- `insert_before_exact`
- `insert_after_exact`
- `insert_before_exact_line`
- `insert_after_exact_line`
- `delete_exact`
- `create_file`
- `rewrite_file`
- `replace_between`
- `replace_lines`
- `ensure_include`

PatchTrack keeps `replace_exact` as the canonical single-replacement operation name and uses `text` for the replacement content field. The MCP schema and examples intentionally avoid `replace` and `replace_text` so agents do not have to guess.

`create_file` requires `text` and refuses to overwrite an existing file.
`rewrite_file` is the deliberate full-file operation: it replaces an existing
file or creates a missing one. Both record whether a file was created, so a
rollback removes a newly-created file instead of leaving an empty souvenir.

For large rewrites, `diff` is bounded to keep MCP responses useful. Read
`diff_summary` for added, removed, changed, and truncation counts; this avoids
turning a perfectly valid large edit into a transport-sized incident.

## Why Structured Edits Instead Of Freeform Patches
PatchTrack deliberately prefers structured edit intents over raw patch text.

That is meant to help avoid classic brittle-edit problems such as:
- a mechanical replacement touching the wrong region;
- a patch partially applying and leaving a malformed block behind;
- search/replace edits accidentally matching multiple unrelated regions;
- failed retries piling more damage on top of an already half-edited file.

This does not eliminate every editing risk, but it narrows the failure surface and makes validation and recovery more explicit.

## Failure Model
PatchTrack distinguishes between two broad failure classes.

### 1. Front-door or transport failure
These happen before a valid request reaches the engine.

Examples:
- MCP host or sandbox failure;
- wrapper tool failure before any request is sent;
- request file cannot be read;
- malformed JSON request.

Typical outcomes:
- no workspace files are changed;
- no PatchTrack transaction is created;
- caller should fix transport, regenerate the request, or retry safely.

### 2. Engine failure
These happen after the request reaches PatchTrack, but the engine refuses or cannot complete it safely.

Examples:
- `HASH_MISMATCH`
- `NO_MATCH`
- `AMBIGUOUS_MATCH`
- `VALIDATION_FAILED`
- `WRITE_FAILED`
- `ROLLBACK_BLOCKED`
- `FILE_BUSY`

Typical outcomes:
- preview failures do not mutate workspace files;
- apply failures do not report success;
- rollback failures do not restore files if rollback preconditions fail;
- recovery records remain available when commit or rollback fails after mutation has begun.

`HASH_MISMATCH` now returns `expected_sha256`, `actual_sha256`, and
`difference_kind`. Supplying `expected_normalized_sha256` from `hash` lets the
engine report `newline_only` drift rather than treating line-ending conversion
as a particularly mysterious code change.

## Current Transaction Guarantees
Current implemented guarantees:
- `preview` is safe to retry and must not mutate workspace files;
- `apply` records a `pending` transaction before commit;
- every changed file gets a before-snapshot before commit proceeds;
- file hashes are re-checked immediately before write;
- each individual file write is staged to a sibling temporary file, flushed, and atomically renamed into place;
- if a write-stage failure happens after one or more files were written, earlier writes are restored;
- rollback performs full preflight before changing any file;
- if rollback itself fails part-way through, rollback writes are restored back to their pre-rollback state;
- transaction journals distinguish `pending`, `applied`, `failed_restored`, and `failed_recovery_required` under `.patchtrack/`;
- mutating operations use short-lived session claim files with heartbeat/expiry instead of long-held OS file locks;
- startup recovery scans clean stale claims, report pending transactions, and persist a workspace recovery summary.

## Validation And Hygiene
Current built-in validation includes:
- hash guards;
- ambiguous-match rejection;
- unsafe path rejection;
- required/forbidden text checks;
- merge-marker rejection;
- literal escaped PowerShell newline sequence rejection;
- NUL-byte rejection;
- protected `.patchtrack` journal paths cannot be used as edit targets;
- newline style, BOM, and EOF newline preservation.

## Soft Concurrency Claims
PatchTrack now has a first soft coordination layer for multi-agent editing.

Current behavior:
- `apply` and `rollback` create short-lived claim files under `.patchtrack/claims`;
- claims carry `session_id`, intent, file list, heartbeat, and expiry;
- overlapping active claims from another session return structured `FILE_BUSY` errors instead of silently racing;
- stale claims are removed during startup recovery scan and do not block takeover;
- this is intentionally metadata-based coordination, not a long-lived OS file lock on the source file.

## Filesystem Diagnostics
PatchTrack emits structured filesystem and journal errors instead of collapsing everything into generic read/write failures.

Current structured fields include:
- `error`
- `message`
- `stage`
- `path`
- `operation`
- `owner_hint`
- `resolution_hint`
- `os_code`

Current classified filesystem error families include:
- `PERMISSION_DENIED_READ`
- `PERMISSION_DENIED_WRITE`
- `PERMISSION_DENIED_CREATE_DIR`
- `PERMISSION_DENIED_TRANSACTION_LOG`
- `PERMISSION_DENIED_ROLLBACK`
- `NO_SPACE_LEFT`
- fallback `READ_FAILED` and `WRITE_FAILED`

## Testing
There are three verification layers.

### `patchtrack selftest`
Internal in-process engine checks for:
- plan/apply/rollback basics;
- hash mismatch;
- commit precondition re-checks;
- rollback blocking;
- EOF-newline preservation.

### `patchtrack_mcp --selftest`
A focused MCP smoke test that checks:
- `initialize`;
- `tools/list`;
- tool routing and schema validation for the MCP frontend.

### Real stdio host probe
On Windows, `integration/mcp_stdio_smoke.ps1` starts one long-lived MCP server and sends newline-delimited `initialize` and `tools/list` requests through real redirected stdin/stdout. This catches host-stream problems that in-process tests cannot see.

### `patchtrack_tests`
A black-box protocol harness that:
- generates disposable workspaces;
- writes request JSON files;
- runs the compiled `patchtrack.exe` binary;
- runs the compiled `patchtrack_mcp.exe` frontend;
- parses machine-readable output;
- verifies file content, journal state, rollback behavior, diagnostics, and stress behavior.

Current harness coverage includes:
- command surface: `hash`, `history`, `preview`, `apply`, `rollback`;
- exact and anchor edits;
- preview no-write guarantees;
- staged transaction journaling and restore-on-failed-commit behavior;
- rollback success and rollback blocked on divergence;
- validation failures and transport failures;
- injected partial-write failure;
- injected transaction-log failure after file writes;
- structured permission-denied and disk-full classification checks;
- abrupt host-termination simulation with pending journal inspection;
- rollback failure after first restore with recovery;
- 1000-edit single-transaction batch stress;
- 1000 sequential transaction stress;
- MCP frontend smoke coverage;
- MCP oneshot and repeated in-process dispatch benchmark coverage.

## Current Verification Status
Latest verified commands in this workspace:

```powershell
E:\apps\github\upp_patchtrack\build\patchtrack.exe selftest
E:\apps\github\upp_patchtrack\build\patchtrack_mcp.exe --selftest
E:\apps\github\upp_patchtrack\build\patchtrack_tests.exe
```

Current status:
- `patchtrack selftest`: passing
- `patchtrack_mcp --selftest`: passing
- protocol harness: `21 / 21` passing
- real Windows MCP stdio probe: passing

Latest observed transport benchmark on this machine:
- core preview: about `0.09 ms`
- CLI preview: about `25.7 ms`
- MCP oneshot preview: about `25.2 ms`
- repeated MCP dispatch inside one server process: about `0.14 ms`

That suggests the main cost is process startup and outer transport, not the edit engine or MCP dispatch itself.

## Build And Run
PatchTrack is a normal U++ workspace. The canonical build path is to build the U++ packages directly with `umk` or TheIDE:

- `patchtrack` builds the CLI frontend.
- `patchtrack_mcp` builds the MCP stdio server for Codex, OpenCode, Hermes, and other MCP hosts.
- `patchtrack_tests` builds the protocol and stress-test harness.

Windows example using `umk`:

```powershell
E:\upp-18468\umk.exe "E:\apps\github\upp_patchtrack,E:\upp-18468\uppsrc" patchtrack CLANGx64 --out-dir "E:\apps\github\upp_patchtrack\out" -br +CONSOLE "E:\apps\github\upp_patchtrack\build\patchtrack"
E:\upp-18468\umk.exe "E:\apps\github\upp_patchtrack,E:\upp-18468\uppsrc" patchtrack_mcp CLANGx64 --out-dir "E:\apps\github\upp_patchtrack\out" -br +CONSOLE "E:\apps\github\upp_patchtrack\build\patchtrack_mcp"
E:\upp-18468\umk.exe "E:\apps\github\upp_patchtrack,E:\upp-18468\uppsrc" patchtrack_tests CLANGx64 --out-dir "E:\apps\github\upp_patchtrack\out" -br +CONSOLE "E:\apps\github\upp_patchtrack\build\patchtrack_tests"
```

On Linux and macOS, use the same U++ package names and assembly path with the platform's `umk` binary and output paths.

After building, run:

```powershell
E:\apps\github\upp_patchtrack\build\patchtrack.exe selftest
E:\apps\github\upp_patchtrack\build\patchtrack_mcp.exe --selftest
E:\apps\github\upp_patchtrack\build\patchtrack_tests.exe
```

`verify.ps1` is only a Windows convenience wrapper for local development. It runs the same U++ package builds and then executes the checks:

```powershell
.\verify.ps1
```

For a quicker smoke pass without the full protocol harness:

```powershell
.\verify.ps1 -SkipProtocolTests
```

If PowerShell script execution is disabled:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\verify.ps1 -SkipProtocolTests
```

## What Is Still Not Solved
Current important gaps:
- canonical symlink/reparse-point containment and race-resistant no-follow opens for hostile workspaces;
- full Unicode-path validation for Windows filesystem calls;
- deeper permission diagnostics that can distinguish more specific causes such as read-only attributes, sandbox policy, or another process holding a lock;
- external OS-failure reproduction beyond explicit fault hooks, such as real disk-full and managed-directory policy failures in automated runs;
- full power-loss durability across a multi-file transaction, beyond per-file atomic replacement and journal inspection;
- stronger multi-session coordination policies beyond the current soft claim layer;
- very large multi-file stress beyond the current transaction shapes;
- richer semantic edits such as symbol-aware C++ transforms.
