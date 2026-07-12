# MCP Host Setup

PatchTrack's MCP server is `patchtrack_mcp`. It is a stdio MCP server that calls the shared `patchtrack_core` engine directly.

Current intended hosts:
- Codex
- OpenCode
- Hermes

## Build First

Build the U++ MCP package first. The Windows example is:

```powershell
E:\upp-18468\umk.exe "E:\apps\github\upp_patchtrack,E:\upp-18468\uppsrc" patchtrack_mcp CLANGx64 --out-dir "E:\apps\github\upp_patchtrack\out" -br +CONSOLE "E:\apps\github\upp_patchtrack\build\patchtrack_mcp"
```

For a fuller local check, also build `patchtrack` and `patchtrack_tests` with the same U++ assembly and run the tests from the repo `build` directory.

On Linux and macOS, use the same U++ package name (`patchtrack_mcp`) with that platform's `umk` binary and output path.

`verify.ps1` is only a Windows convenience wrapper. It builds the same U++ packages and runs smoke checks:

```powershell
.\verify.ps1 -SkipProtocolTests
```

The MCP binary will be:

```text
E:\apps\github\upp_patchtrack\build\patchtrack_mcp.exe
```

Run a quick MCP smoke check:

```powershell
E:\apps\github\upp_patchtrack\build\patchtrack_mcp.exe --selftest
```

Expected output:

```text
mcp-selftest: ok
```

Run the real Windows stdio host probe after building:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\integration\mcp_stdio_smoke.ps1
```

It starts one long-lived server and sends both `initialize` and `tools/list` through the actual newline-delimited stdin/stdout protocol.

## Server Command

Use this as the server command in MCP host configs:

```text
E:\apps\github\upp_patchtrack\build\patchtrack_mcp.exe
```

No arguments are required for normal MCP stdio operation.

Do not configure `--oneshot` for normal host use. `--oneshot` is only for test harness calls.

## Generic MCP Config Shape

Use this shape when a host accepts JSON-style MCP server config:

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

Use this shape when a host accepts TOML-style MCP server config:

```toml
[mcp_servers.patchtrack]
command = "E:\\apps\\github\\upp_patchtrack\\build\\patchtrack_mcp.exe"
args = []
```

Host-specific file names and config locations differ. Keep the server name `patchtrack` unless the host requires a different key.

Ready-to-copy config templates live under `integration/host_configs/`:
- `codex.example.toml`
- `opencode.example.json`
- `hermes.example.json`

Raw JSON-RPC request and response examples live in `integration/MCP_REQUEST_RESPONSE_EXAMPLES.md`.

## Codex

Codex should launch `patchtrack_mcp.exe` as a stdio MCP server.

Recommended server name:

```text
patchtrack
```

Recommended command:

```text
E:\apps\github\upp_patchtrack\build\patchtrack_mcp.exe
```

Keep args empty.

## OpenCode

OpenCode should launch the same stdio MCP server.

Recommended server name:

```text
patchtrack
```

Recommended command:

```text
E:\apps\github\upp_patchtrack\build\patchtrack_mcp.exe
```

Keep args empty.

The older `integration/opencode_patchtrack_command.md` file describes a command-prompt workflow. Prefer the MCP server once OpenCode is configured for MCP.

## Hermes

Hermes should also launch `patchtrack_mcp.exe` as a stdio MCP server.

Recommended server name:

```text
patchtrack
```

Recommended command:

```text
E:\apps\github\upp_patchtrack\build\patchtrack_mcp.exe
```

Keep args empty.

## Exposed Tools

The MCP server currently exposes:
- `patchtrack_preview`
- `patchtrack_apply`
- `patchtrack_rollback`
- `patchtrack_hash`
- `patchtrack_history`
- `patchtrack_recovery_scan`

## Agent Usage Rule

Agents should prefer this flow:

1. Call `patchtrack_hash` for every target file.
2. Call `patchtrack_preview` with structured edits.
3. Inspect `structuredContent.ok`, `files`, and diff content.
4. Call `patchtrack_apply` only after preview is correct.
5. Use `patchtrack_rollback` for undo instead of writing reverse edits manually.
6. Call `patchtrack_recovery_scan` if startup reports pending or recovery-required transactions.

## Troubleshooting

If the host cannot start the server:
- confirm `patchtrack_mcp.exe --selftest` works from PowerShell;
- confirm the configured path is absolute;
- confirm no arguments are configured for normal stdio use;
- rebuild the `patchtrack_mcp` U++ package;
- check whether the host expects JSON or TOML config format.

If a tool call returns an engine error:
- `HASH_MISMATCH`: re-read the file and update `expected_sha256`;
- `NO_MATCH`: re-read the file and adjust the edit anchor or text;
- `FILE_BUSY`: another PatchTrack session has an active claim;
- `ROLLBACK_BLOCKED`: the workspace diverged after the transaction;
- filesystem errors include `stage`, `path`, `operation`, `owner_hint`, and `resolution_hint`.
