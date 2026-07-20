/*
PatchTrack CLI frontend.

Copyright (c) 2026 Curtis Edwards (dodobar)
License: MIT; see LICENSE.

This package keeps the local console workflow available, but it is now just one
frontend over patchtrack_core. Request parsing, transaction planning, journaling,
and recovery all live in the shared engine so the CLI and MCP server stay aligned.

Change log:
- 2026-04-28: Reduced patchtrack package to a thin CLI entrypoint.
- 2026-04-28: Removed the legacy core command dispatcher and moved CLI parsing here.
*/

#include <Core/Core.h>
#include <patchtrack_core/patchtrack_core.h>

using namespace Upp;

namespace {

String UsageText()
{
    return
        "patchtrack - transactional source editing for agents\n\n"
        "Commands:\n"
        "  patchtrack preview request.json\n"
        "  patchtrack apply request.json\n"
        "  patchtrack rollback request.json\n"
        "  patchtrack version\n"
        "  patchtrack selftest\n"
        "  patchtrack hash <file>\n"
        "  patchtrack history <workspace-root>\n"
        "  patchtrack recovery_scan <workspace-root>\n\n"
        "Request schema: workspace_root, summary, actor, edits[].\n";
}

int RunJsonRequestCommand(const String& command, const Vector<String>& cmd)
{
    if(cmd.GetCount() < 2) {
        Cout() << UsageText();
        return 2;
    }

    String body;
    String read_error;
    if(!PatchtrackReadRequestFile(cmd[1], body, read_error)) {
        if(read_error.StartsWith("FILE_NOT_FOUND"))
            Cout() << "{\"ok\":false,\"error\":\"REQUEST_READ_FAILED\"}\n";
        else
            Cout() << PatchtrackFormatErrorJson(read_error);
        return 1;
    }

    Value req;
    String error;
    if(!PatchtrackParseRequestJson(body, req, error)) {
        Cout() << "{\"ok\":false,\"error\":\"BAD_JSON\"}\n";
        return 1;
    }

    String result;
    bool ok = false;
    if(command == "preview")
        ok = PatchtrackPreview(req, result, error);
    else if(command == "apply")
        ok = PatchtrackApply(req, result, error);
    else if(command == "rollback")
        ok = PatchtrackRollback(req, result, error);
    else {
        Cout() << UsageText();
        return 2;
    }

    // Preview failures still return a rich preview payload, so only the mutating
    // commands need the generic error wrapper here.
    if(!ok && command != "preview") {
        Cout() << PatchtrackFormatErrorJson(error);
        return 1;
    }

    Cout() << result;
    return ok ? 0 : 1;
}

int RunHashCommand(const Vector<String>& cmd)
{
    if(cmd.GetCount() < 2) {
        Cout() << UsageText();
        return 2;
    }

    String result;
    String error;
    if(!PatchtrackHash(cmd[1], result, error)) {
        Cout() << PatchtrackFormatErrorJson(error);
        return 1;
    }

    Cout() << result;
    return 0;
}

int RunHistoryCommand(const Vector<String>& cmd)
{
    if(cmd.GetCount() < 2) {
        Cout() << UsageText();
        return 2;
    }

    String result;
    String error;
    if(!PatchtrackHistory(cmd[1], result, error)) {
        Cout() << PatchtrackFormatErrorJson(error);
        return 1;
    }

    Cout() << result;
    return 0;
}

int RunRecoveryScanCommand(const Vector<String>& cmd)
{
    if(cmd.GetCount() < 2) {
        Cout() << UsageText();
        return 2;
    }

    String result;
    String error;
    if(!PatchtrackRecoveryScan(cmd[1], result, error)) {
        Cout() << PatchtrackFormatErrorJson(error);
        return 1;
    }

    Cout() << result;
    return 0;
}

int RunSelfTestCommand()
{
    String result;
    int rc = PatchtrackSelfTest(result);
    Cout() << result;
    return rc;
}

int RunVersionCommand()
{
    Cout() << PatchtrackVersion() << "\n";
    return 0;
}

} // namespace

CONSOLE_APP_MAIN
{
    const Vector<String>& cmd = CommandLine();
    if(cmd.IsEmpty()) {
        Cout() << UsageText();
        SetExitCode(0);
        return;
    }

    const String& command = cmd[0];
    int rc = 2;

    if(command == "preview" || command == "apply" || command == "rollback")
        rc = RunJsonRequestCommand(command, cmd);
    else if(command == "selftest")
        rc = RunSelfTestCommand();
    else if(command == "version")
        rc = RunVersionCommand();
    else if(command == "hash")
        rc = RunHashCommand(cmd);
    else if(command == "history")
        rc = RunHistoryCommand(cmd);
    else if(command == "recovery_scan")
        rc = RunRecoveryScanCommand(cmd);
    else {
        Cout() << UsageText();
        rc = 2;
    }

    SetExitCode(rc);
}
