# Changelog

## 2026-07-20 - 1.1.1
- Corrected the shipped OpenCode template to use OpenCode's native `mcp` local-server schema and added a parser regression check for its permissions and command shape.
- Replaced substring-based path rejection with component-aware normalization and workspace containment checks for mixed separators, traversal, protected journal paths, missing targets, and Windows reparse components.
- Made preview observational: stale claims are reported but not deleted, recovery reports are not persisted, and routine temporary-artifact discovery no longer recursively walks the workspace.
- Throttled claim heartbeats by lease elapsed time, indexed planned files, cached final bytes/hashes/diffs, compacted transaction journals, and reduced MCP text content to a concise summary while retaining complete structuredContent.
- Added path, preview immutability, OpenCode schema, response-shape, and scaling coverage. No atomic writes, flushes, snapshots, commit rechecks, verification, or rollback guarantees were weakened.

## 2026-07-20
- Added a shared `1.1.0` version source, exposed through MCP initialization, a read-only `version` tool, and the CLI. Agents can now check what they are talking to before they start negotiating with a binary.
- Added explicit `create_file` and allowed `rewrite_file` to create missing targets. Transactions journal the creation state, restore removes a newly-created file after a failed commit, and rollback removes it on purpose.
- Expanded guarded-edit diagnostics with raw and normalized hashes. Hash mismatch results now expose expected and actual digests, and can identify `newline_only` drift when callers supply the normalized guard.
- Reworked full-file diff output into bounded, file-local hunks with `diff_summary` counts. Large edits remain valid; only the ceremonial wall of output was asked to leave.
- Added protocol-harness coverage for creation, rollback-to-absence, overwrite refusal, newline-only drift, version reporting, and bounded large diffs.

## 2026-07-13
- Renamed the canonical MCP surface to local tool names (`preview`, `apply`, `rollback`, `hash`, `history`, `recovery_scan`) while keeping host-side prefixes as display noise only. The host can keep its theatre; the wire format stays sane.
- Added tool annotations for read-only, destructive, and idempotent hints, plus compatibility handling for prefixed tool calls so OpenCode does not have to relearn basic manners mid-release.
- Extended recovery scan output with structured temporary-artifact reporting so hosts can confirm `.patchtrack-tmp-*` cleanup without rummaging through directories like they pay rent there.
- Updated docs, examples, smoke tests, and release checklist to use the canonical names and the recommended OpenCode permission split.

## 2026-07-12
- Tightened the MCP frontend schema so the canonical edit vocabulary is explicit, `session` is validated as an object with a `sess-` prefix rule, and the tool descriptions point agents at `replace_exact` and `text` instead of the old guess-your-own-adventure version.
- Bumped the advertised MCP initialize version to `1.0.0` and added a regression check for it. First public release, less mystery meat.
- Added regression coverage for the tools/list schema shape, canonical op enumeration, unsupported-op refusal, and the newline-delimited MCP round trip that Codex actually uses.
- Updated the public docs and release checklist to reflect the natural-language mapping and the current release-readiness checks. Small victory for ordinary nouns.

## 2026-04-29
- Clarified the intended MCP host targets as Codex, OpenCode, and Hermes.
- Cleaned repository ignore rules so generated MCP/test scratch output is ignored while `UPP_GUIDES/` remains visible as living documentation.
- Clarified that the canonical build path is the normal U++ package build with `umk` or TheIDE, with `verify.ps1` kept only as a Windows convenience wrapper.
- Documented the process-local PowerShell execution-policy bypass needed on locked-down Windows systems.
- Added `integration/MCP_HOSTS.md` with initial Codex, OpenCode, and Hermes MCP server setup notes.
- Added concrete Codex, OpenCode, and Hermes host config templates plus raw MCP request/response examples.
- Updated the README to act as the public MCP-first entry point, including CLI usage, MCP tool usage, request examples, failure model, transaction guarantees, diagnostics, build flow, and current verification status.
- Finished the dedicated MCP round-trip integration test so it validates MCP responses, preview/apply/rollback state transitions, transaction id propagation, and recovery scan success.
- Extended transport profiling so the harness now separates CLI, MCP `--oneshot`, and repeated MCP dispatch cost inside one server process.
- Added atomic per-file replacement with flush and temporary-file cleanup, shared request type validation, protected journal-path rejection, and corrected MCP session object schemas.
- Corrected MCP stdio to the standard newline-delimited JSON transport after real Codex validation exposed the non-standard Content-Length framing; the smoke probe now exercises the standard host protocol.
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
