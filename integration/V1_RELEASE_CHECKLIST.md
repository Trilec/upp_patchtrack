# PatchTrack v1 Release Checklist

This is the acceptance checklist for the first public MCP release.

## Automated Windows Baseline

From the repository root, with U++ installed at `E:\upp-18468`:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\verify.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\integration\mcp_stdio_smoke.ps1
```

Expected results:

- `selftest: ok`
- `mcp-selftest: ok`
- `protocol-tests: ok`
- `Passed: 21`, `Failed: 0`
- `mcp-stdio-smoke: ok`

The protocol harness covers preview no-write behavior, exact and anchor edits, validation failures, transaction snapshots, commit precondition re-checks, rollback blocking and recovery, injected write and journal failures, permission and disk-full diagnostics, stale claims, startup recovery, abrupt host termination, 1,000-edit batches, 1,000 sequential transactions, MCP round trips, and transport cost.

## Real MCP Host Checks

For each host, configure the binary from `integration/host_configs/` and keep the normal server command argument-free:

1. Start a new host session.
2. Confirm `patchtrack` appears in the available MCP servers/tools.
3. Call `patchtrack_hash` on a disposable file.
4. Call `patchtrack_preview` and confirm the file is unchanged.
5. Call `patchtrack_apply` and confirm the file changed and a `transaction_id` was returned.
6. Call `patchtrack_rollback` and confirm the original content returned.
7. Call `patchtrack_recovery_scan` and confirm it reports no pending recovery work.

Repeat with a multi-file request and with an intentionally stale `expected_sha256` to confirm the host displays structured failure information instead of treating a refusal as success.

## Manual Failure Checks

Use a disposable workspace, never a production checkout, for these checks:

- remove write permission from a target file and confirm `PERMISSION_DENIED_WRITE` includes `stage`, `path`, `owner_hint`, and `resolution_hint`;
- remove write permission from `.patchtrack` and confirm transaction-log diagnostics identify the journal owner and repair action;
- create an active claim from another session and confirm `FILE_BUSY` is reported;
- leave an expired claim and confirm recovery scan removes it and permits takeover;
- terminate the host after an injected or simulated mid-apply failure and inspect the pending transaction before retrying;
- edit the workspace after apply and confirm rollback returns `ROLLBACK_BLOCKED` rather than overwriting the new edit;
- verify no `.patchtrack-tmp-*` files remain after successful or failed writes.

## Release Boundary

PatchTrack v1 is a transactional safety tool for trusted local AI coding workspaces. It is not a sandbox or a security boundary. Before using it against hostile workspaces, add canonical symlink/reparse-point containment and race-resistant no-follow file opens.

The current Windows filesystem layer uses ANSI Win32 file APIs, so non-ASCII Windows paths require a separate Unicode-path validation pass before being treated as fully supported.
