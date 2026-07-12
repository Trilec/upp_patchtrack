# Changelog

## 2026-04-29
- Clarified the intended MCP host targets as Codex, OpenCode, and Hermes.
- Cleaned repository ignore rules so generated MCP/test scratch output is ignored while `UPP_GUIDES/` remains visible as living documentation.
- Clarified that the canonical build path is the normal U++ package build with `umk` or TheIDE, with `verify.ps1` kept only as a Windows convenience wrapper.
- Documented the process-local PowerShell execution-policy bypass needed on locked-down Windows systems.
- Added `integration/MCP_HOSTS.md` with initial Codex, OpenCode, and Hermes MCP server setup notes.
- Added concrete Codex, OpenCode, and Hermes host config templates plus raw MCP request/response examples.
- Updated the README to act as the public MCP-first entry point, including CLI usage, MCP tool usage, request examples, failure model, transaction guarantees, diagnostics, build flow, and current verification status.
- Finished the dedicated MCP round-trip integration test so it validates framed MCP responses, preview/apply/rollback state transitions, transaction id propagation, and recovery scan success.
- Extended transport profiling so the harness now separates CLI, MCP `--oneshot`, and repeated MCP dispatch cost inside one server process.
- Added atomic per-file replacement with flush and temporary-file cleanup, shared request type validation, protected journal-path rejection, and corrected MCP session object schemas.
- Fixed Windows redirected-stdin framing by accepting both CRLF and LF header termination, and added `integration/mcp_stdio_smoke.ps1` for a real long-lived stdio host probe.
- Added `integration/V1_RELEASE_CHECKLIST.md` with automated, host-level, manual failure, and release-boundary checks.

## 2026-04-28
- Backed up the pre-refactor implementation into `OLD/`, extracted the existing engine/CLI logic into `patchtrack_core`, and rebuilt `patchtrack` as a transport-only CLI frontend over the exported core APIs.
- Added a dedicated `patchtrack_mcp` stdio server package so MCP is now a first-class frontend instead of a planned wrapper around the CLI.
- Removed the legacy public CLI dispatcher from the core and replaced it with frontend-neutral exports for request parsing, preview, apply, rollback, hash, history, recovery scan, error formatting, request-file loading, and selftest.
- Split OS-specific filesystem and abrupt-exit behavior into dedicated platform files so the transaction engine no longer depends directly on Win32 APIs.
- Implemented a structured filesystem diagnostics layer with classified permission, directory-creation, transaction-log, rollback, and disk-full errors plus owner/resolution hints.
- Expanded the protocol harness to cover structured permission/disk-full diagnostics, soft claim conflicts, stale-claim takeover, startup recovery scan reporting, abrupt host-termination inspection, and MCP frontend smoke coverage.
- Rewrote the README to explain the public purpose of PatchTrack more clearly: brittle AI-edit avoidance, structured edits, transaction guarantees, current diagnostics, current gaps, lessons learned, and the new `patchtrack_core`/`patchtrack`/`patchtrack_mcp` layout.
- Updated the living U++ build/design guide with the implemented diagnostics layer, soft claim concurrency design, new fault hooks, continuation notes, and clearer MCP-system-tool positioning.

## 2026-04-27
- Added staged transaction journal records with `pending`, `applied`, `failed_restored`, and `failed_recovery_required` states.
- Added before-snapshot assertions and retained recovery records for injected apply failures.
- Added rollback preflight, rollback failure recovery, and rollback fault injection coverage.
- Hardened ID generation so repeated short-lived `patchtrack` invocations do not overwrite transaction logs.
- Expanded the protocol harness with per-case console reporting, 1000-edit batch coverage, and 1000 sequential transaction stress coverage.
- Expanded the built-in selftest with commit precondition re-check coverage.
