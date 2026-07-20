/*
PatchTrack shared core package.

This header exposes the engine-facing API that both the CLI frontend and the
MCP frontend call directly. The goal is to keep transport concerns out of the
core so request validation, journaling, recovery, and diagnostics stay in one
place.

Change log:
- 2026-04-28: Extracted the existing PatchTrack implementation into patchtrack_core.
- 2026-04-28: Replaced the public CLI dispatcher with frontend-neutral core APIs.
*/

#ifndef _patchtrack_core_patchtrack_core_h_
#define _patchtrack_core_patchtrack_core_h_

#include <Core/Core.h>

namespace Upp {

// Returns the PatchTrack release version shared by every frontend and tool result.
const char *PatchtrackVersion();

// Reads a request file with the same filesystem diagnostics used for workspace IO.
bool PatchtrackReadRequestFile(const String& path, String& body, String& error);

// Converts an engine error string into the stable JSON error object frontends emit.
String PatchtrackFormatErrorJson(const String& error);

// Parses a request body and confirms the top-level payload is a JSON object.
bool PatchtrackParseRequestJson(const String& body, Value& req, String& error);

// Validates a request and returns the preview JSON that callers can inspect or show.
bool PatchtrackPreview(Value req, String& result_json, String& error);

// Applies a validated request transactionally and returns the structured result JSON.
bool PatchtrackApply(Value req, String& result_json, String& error);

// Rolls back a recorded transaction and returns the structured rollback result JSON.
bool PatchtrackRollback(Value req, String& result_json, String& error);

// Returns the classic sha256-plus-path text format used by the CLI and harness.
bool PatchtrackHash(const String& path, String& result_text, String& error);

// Returns raw and normalized-content SHA-256 values for guarded edits and drift diagnostics.
bool PatchtrackHashDetails(const String& path, String& sha256, String& normalized_sha256,
                           String& newline, String& error);

// Returns a human-readable history listing for the workspace journal.
bool PatchtrackHistory(const String& workspace_root, String& result_text, String& error);

// Runs the startup recovery scan and returns the structured scan result JSON.
bool PatchtrackRecoveryScan(const String& workspace_root, String& result_json, String& error);

// Runs the in-process engine selftest and returns CLI-ready console text plus exit code.
int PatchtrackSelfTest(String& result_text);

}

#endif
