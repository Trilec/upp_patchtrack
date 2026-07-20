/*
PatchTrack MCP frontend.

Copyright (c) 2026 Curtis Edwards (dodobar)
License: MIT; see LICENSE.

This package exposes patchtrack_core as a first-class MCP stdio server instead of
routing everything back through CLI-shaped commands. The server keeps transport
logic here while the transactional engine stays in patchtrack_core.

Change log:
- 2026-04-28: Added the first dedicated MCP frontend package over patchtrack_core.
- 2026-04-28: Tightened MCP tool schemas and added matching runtime argument checks.
*/

#include <Core/Core.h>
#include <patchtrack_core/patchtrack_core.h>
#include <stdio.h>

using namespace Upp;

namespace {

struct SelfTestState : Moveable<SelfTestState> {
    Vector<String> failures;
};

bool Expect(SelfTestState& st, bool condition, const String& message)
{
    if(condition)
        return true;
    st.failures.Add(message);
    return false;
}

String JEscape(const String& s)
{
    String out;
    for(int i = 0; i < s.GetLength(); i++) {
        byte c = (byte)s[i];
        switch(c) {
        case '\\': out << "\\\\"; break;
        case '"':  out << "\\\""; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if(c < 32)
                out << Format("\\u%04x", (int)c);
            else
                out.Cat(c);
        }
    }
    return out;
}

String JString(const String& s)
{
    return "\"" + JEscape(s) + "\"";
}

String JsonScalarLiteral(const Value& v)
{
    if(IsNull(v))
        return "null";
    if(v.Is<String>())
        return JString((String)v);
    if(v.Is<bool>())
        return (bool)v ? "true" : "false";
    if(v.Is<int>())
        return AsString((int)v);
    if(v.Is<int64>())
        return AsString((int64)v);
    if(v.Is<double>())
        return AsString((double)v);
    return JString(AsString(v));
}

String BuildSimpleFailureJson(const String& error, const String& message)
{
    String out;
    out << "{\n"
        << "  \"ok\": false,\n"
        << "  \"error\": " << JString(error);
    if(!message.IsEmpty())
        out << ",\n  \"message\": " << JString(message);
    out << "\n}\n";
    return out;
}

String BuildJsonRpcResult(const String& id_literal, const String& result_json)
{
    String out;
    out << "{\n"
        << "  \"jsonrpc\": \"2.0\",\n"
        << "  \"id\": " << id_literal << ",\n"
        << "  \"result\": " << result_json << "\n"
        << "}\n";
    return out;
}

String BuildJsonRpcError(const String& id_literal, int code, const String& message)
{
    String out;
    out << "{\n"
        << "  \"jsonrpc\": \"2.0\",\n"
        << "  \"id\": " << id_literal << ",\n"
        << "  \"error\": {\n"
        << "    \"code\": " << code << ",\n"
        << "    \"message\": " << JString(message) << "\n"
        << "  }\n"
        << "}\n";
    return out;
}

String BuildToolTextSummary(const String& structured_json, bool is_error)
{
    Value parsed;
    try {
        parsed = ParseJSON(structured_json);
    }
    catch(CParser::Error) {
        return is_error ? "PatchTrack rejected the request." : "PatchTrack returned a result.";
    }

    if(is_error || !(bool)parsed["ok"]) {
        String text = "PatchTrack error: " + (String)parsed["error"];
        String guidance = (String)parsed["resolution_hint"];
        if(!guidance.IsEmpty())
            text << ". " << guidance;
        return text;
    }

    String transaction = (String)parsed["transaction_id"];
    if(transaction.IsEmpty())
        transaction = (String)parsed["rollback_transaction_id"];
    if(!transaction.IsEmpty())
        return "PatchTrack completed transaction " + transaction + ".";

    if(!IsNull(parsed["files"])) {
        int files = parsed["files"].GetCount();
        int truncated = 0;
        for(int i = 0; i < files; i++)
            if((bool)parsed["files"][i]["diff_summary"]["truncated"])
                truncated++;
        String text = Format("PatchTrack checked %d file%s.", files, files == 1 ? "" : "s");
        if(truncated)
            text << Format(" %d diff%s truncated; inspect structuredContent.diff_summary.", truncated, truncated == 1 ? " was" : " were");
        return text;
    }

    if(!((String)parsed["version"]).IsEmpty())
        return "PatchTrack version " + (String)parsed["version"] + ".";
    return "PatchTrack completed successfully.";
}

// MCP clients receive concise text for humans and agents, while structured
// content remains authoritative for hashes, diffs, transaction IDs and errors.
String BuildToolResult(const String& structured_json, bool is_error)
{
    String text_summary = BuildToolTextSummary(structured_json, is_error);
    String out;
    out << "{\n"
        << "  \"content\": [\n"
        << "    {\"type\": \"text\", \"text\": " << JString(text_summary) << "}\n"
        << "  ],\n"
        << "  \"structuredContent\": " << structured_json << ",\n"
        << "  \"isError\": " << (is_error ? "true" : "false") << "\n"
        << "}\n";
    return out;
}

String JsonBool(bool value)
{
    return value ? "true" : "false";
}

String NormalizeToolName(String tool_name)
{
    while(tool_name.StartsWith("patchtrack_"))
        tool_name = tool_name.Mid(11);
    return tool_name;
}

bool ExpectMap(const Value& v, const char *label, String& error_json)
{
    if(v.Is<ValueMap>())
        return true;
    error_json = BuildSimpleFailureJson("BAD_REQUEST", String(label) + " must be a JSON object.");
    return false;
}

bool ExpectArray(const Value& v, const char *label, String& error_json)
{
    if(v.Is<ValueArray>())
        return true;
    error_json = BuildSimpleFailureJson("BAD_REQUEST", String(label) + " must be an array.");
    return false;
}

bool ExpectStringField(const Value& args, const char *key, bool required, String& out, String& error_json)
{
    Value v = args[key];
    if(IsNull(v)) {
        if(required)
            error_json = BuildSimpleFailureJson("BAD_REQUEST", String("Missing string field '") + key + "'.");
        return !required;
    }
    if(!v.Is<String>()) {
        error_json = BuildSimpleFailureJson("BAD_REQUEST", String("Field '") + key + "' must be a string.");
        return false;
    }
    out = (String)v;
    return true;
}

bool ExpectBoolField(const Value& args, const char *key, bool required, String& error_json)
{
    Value v = args[key];
    if(IsNull(v)) {
        if(required)
            error_json = BuildSimpleFailureJson("BAD_REQUEST", String("Missing boolean field '") + key + "'.");
        return !required;
    }
    if(!v.Is<bool>()) {
        error_json = BuildSimpleFailureJson("BAD_REQUEST", String("Field '") + key + "' must be a boolean.");
        return false;
    }
    return true;
}

bool ExpectObjectField(const Value& args, const char *key, bool required, String& error_json)
{
    Value v = args[key];
    if(IsNull(v))
        return !required;
    if(v.Is<ValueMap>())
        return true;
    error_json = BuildSimpleFailureJson("BAD_REQUEST", String("Field '") + key + "' must be an object.");
    return false;
}

bool RejectUnexpectedKeys(const Value& args, const Vector<String>& allowed, String& error_json)
{
    const ValueMap& map = args;
    for(int i = 0; i < map.GetCount(); i++) {
        String key = map.GetKey(i);
        bool ok = false;
        for(int j = 0; j < allowed.GetCount(); j++) {
            if(allowed[j] == key) {
                ok = true;
                break;
            }
        }
        if(!ok) {
            error_json = BuildSimpleFailureJson("BAD_REQUEST", "Unexpected field '" + key + "'.");
            return false;
        }
    }
    return true;
}

bool ValidateSessionObject(const Value& args, String& error_json)
{
    Value session = args["session"];
    if(IsNull(session))
        return true;
    if(!session.Is<ValueMap>()) {
        error_json = BuildSimpleFailureJson("BAD_REQUEST", "Field 'session' must be an object.");
        return false;
    }

    Vector<String> allowed;
    allowed.Add("id");
    allowed.Add("goal");
    if(!RejectUnexpectedKeys(session, allowed, error_json))
        return false;

    String discard;
    if(!ExpectStringField(session, "id", false, discard, error_json))
        return false;
    if(!ExpectStringField(session, "goal", false, discard, error_json))
        return false;
    return true;
}

bool ValidateEditItems(const Value& args, String& error_json)
{
    Value edits = args["edits"];
    if(!ExpectArray(edits, "edits", error_json))
        return false;

    const ValueArray& va = edits;
    for(int i = 0; i < va.GetCount(); i++) {
        if(!va[i].Is<ValueMap>()) {
            error_json = BuildSimpleFailureJson("BAD_REQUEST", Format("Edit %d must be an object.", i));
            return false;
        }
        if(IsNull(va[i]["op"]) || !va[i]["op"].Is<String>()) {
            error_json = BuildSimpleFailureJson("BAD_REQUEST", Format("Edit %d is missing string field 'op'.", i));
            return false;
        }
        if(IsNull(va[i]["file"]) || !va[i]["file"].Is<String>()) {
            error_json = BuildSimpleFailureJson("BAD_REQUEST", Format("Edit %d is missing string field 'file'.", i));
            return false;
        }
    }
    return true;
}

bool ValidatePreviewApplyArgs(const Value& args, String& error_json)
{
    if(!ExpectMap(args, "arguments", error_json))
        return false;

    Vector<String> allowed;
    allowed.Add("workspace_root");
    allowed.Add("summary");
    allowed.Add("actor");
    allowed.Add("session");
    allowed.Add("allow_suspicious");
    allowed.Add("validation");
    allowed.Add("testing");
    allowed.Add("edits");
    if(!RejectUnexpectedKeys(args, allowed, error_json))
        return false;

    String discard;
    if(!ExpectStringField(args, "workspace_root", true, discard, error_json))
        return false;
    if(!ExpectStringField(args, "summary", false, discard, error_json))
        return false;
    if(!ExpectStringField(args, "actor", false, discard, error_json))
        return false;
    if(!ValidateSessionObject(args, error_json))
        return false;
    if(!ExpectBoolField(args, "allow_suspicious", false, error_json))
        return false;
    if(!ExpectObjectField(args, "validation", false, error_json))
        return false;
    if(!ExpectObjectField(args, "testing", false, error_json))
        return false;
    return ValidateEditItems(args, error_json);
}

bool ValidateRollbackArgs(const Value& args, String& error_json)
{
    if(!ExpectMap(args, "arguments", error_json))
        return false;

    Vector<String> allowed;
    allowed.Add("workspace_root");
    allowed.Add("transaction_id");
    allowed.Add("actor");
    allowed.Add("session");
    if(!RejectUnexpectedKeys(args, allowed, error_json))
        return false;

    String discard;
    if(!ExpectStringField(args, "workspace_root", true, discard, error_json))
        return false;
    if(!ExpectStringField(args, "transaction_id", true, discard, error_json))
        return false;
    if(!ExpectStringField(args, "actor", false, discard, error_json))
        return false;
    if(!ValidateSessionObject(args, error_json))
        return false;
    return true;
}

bool ValidateSingleStringArg(const Value& args, const char *field, String& error_json)
{
    if(!ExpectMap(args, "arguments", error_json))
        return false;
    Vector<String> allowed;
    allowed.Add(field);
    if(!RejectUnexpectedKeys(args, allowed, error_json))
        return false;
    String discard;
    return ExpectStringField(args, field, true, discard, error_json);
}

String BuildHashStructuredJson(const String& path, const String& sha256,
                               const String& normalized_sha256, const String& newline)
{
    String out;
    out << "{\n"
        << "  \"ok\": true,\n"
        << "  \"sha256\": " << JString(sha256) << ",\n"
        << "  \"normalized_sha256\": " << JString(normalized_sha256) << ",\n"
        << "  \"newline\": " << JString(newline) << ",\n"
        << "  \"path\": " << JString(path) << "\n"
        << "}\n";
    return out;
}

String BuildHistoryStructuredJson(const String& workspace_root, const String& result_text)
{
    String out;
    out << "{\n"
        << "  \"ok\": true,\n"
        << "  \"workspace_root\": " << JString(workspace_root) << ",\n"
        << "  \"text\": " << JString(result_text) << "\n"
        << "}\n";
    return out;
}

String BuildInitializeResult()
{
    String out;
    out << "{\n"
        << "  \"protocolVersion\": \"2024-11-05\",\n"
        << "  \"serverInfo\": {\n"
        << "    \"name\": \"patchtrack_mcp\",\n"
        << "    \"version\": " << JString(PatchtrackVersion()) << "\n"
        << "  },\n"
        << "  \"capabilities\": {\n"
        << "    \"tools\": {\n"
        << "      \"listChanged\": false\n"
        << "    }\n"
        << "  },\n"
        << "  \"instructions\": \"PatchTrack provides transactional structured source edits. Call version when the active release or supported edit contract matters; preview before apply, and use rollback for reversal.\"\n"
        << "}\n";
    return out;
}

String BuildVersionStructuredJson()
{
    String out;
    out << "{\n"
        << "  \"ok\": true,\n"
        << "  \"name\": \"patchtrack_mcp\",\n"
        << "  \"version\": " << JString(PatchtrackVersion()) << ",\n"
        << "  \"protocol_version\": \"2024-11-05\",\n"
        << "  \"features\": [\"transactional_edits\", \"create_file\", \"rewrite_creates_missing\", \"structured_hash_diagnostics\", \"bounded_diffs\"]\n"
        << "}\n";
    return out;
}

String BuildToolsListResult()
{
    return
        "{\n"
        "  \"tools\": [\n"
        "    {\n"
        "      \"name\": \"version\",\n"
        "      \"annotations\": {\n"
        "        \"readOnlyHint\": true,\n"
        "        \"destructiveHint\": false,\n"
        "        \"idempotentHint\": true\n"
        "      },\n"
        "      \"description\": \"Return the active PatchTrack release, MCP protocol version, and supported feature flags.\",\n"
        "      \"inputSchema\": {\"type\": \"object\", \"additionalProperties\": false}\n"
        "    },\n"
        "    {\n"
        "      \"name\": \"preview\",\n"
        "      \"annotations\": {\n"
        "        \"readOnlyHint\": true,\n"
        "        \"destructiveHint\": false,\n"
        "        \"idempotentHint\": true\n"
        "      },\n"
        "      \"description\": \"Hash every target file first, pass the hash as expected_sha256, use op replace_exact for an ordinary exact replacement, put the original text in find and the replacement content in text, and preview the diff without writing workspace files.\",\n"
        "      \"inputSchema\": {\n"
        "        \"type\": \"object\",\n"
        "        \"required\": [\"workspace_root\", \"edits\"],\n"
        "        \"properties\": {\n"
        "          \"workspace_root\": {\"type\": \"string\"},\n"
        "          \"summary\": {\"type\": \"string\"},\n"
        "          \"actor\": {\"type\": \"string\"},\n"
        "          \"session\": {\"type\": \"object\", \"properties\": {\"id\": {\"type\": \"string\", \"pattern\": \"^sess-.+\"}, \"goal\": {\"type\": \"string\"}}, \"additionalProperties\": false, \"description\": \"Optional session metadata. If supplied, session.id must start with sess-.\"},\n"
        "          \"allow_suspicious\": {\"type\": \"boolean\"},\n"
        "          \"validation\": {\"type\": \"object\"},\n"
        "          \"testing\": {\"type\": \"object\"},\n"
        "          \"edits\": {\n"
        "            \"type\": \"array\",\n"
        "            \"items\": {\n"
        "              \"type\": \"object\",\n"
        "              \"required\": [\"op\", \"file\"],\n"
                "              \"properties\": {\n"
                "                \"op\": {\"type\": \"string\", \"enum\": [\"replace_exact\", \"replace_all_exact\", \"insert_before_exact\", \"insert_after_exact\", \"insert_before_exact_line\", \"insert_after_exact_line\", \"delete_exact\", \"create_file\", \"rewrite_file\", \"replace_between\", \"replace_lines\", \"ensure_include\"], \"description\": \"Use replace_exact for an ordinary single replacement; create_file only creates absent targets, while rewrite_file deliberately replaces or creates a complete file.\"},\n"
        "                \"file\": {\"type\": \"string\", \"description\": \"Workspace-relative file path.\"},\n"
        "                \"find\": {\"type\": \"string\", \"description\": \"Original text to match exactly.\"},\n"
        "                \"text\": {\"type\": \"string\", \"description\": \"Replacement content to write.\"},\n"
        "                \"new_lines\": {\"type\": \"array\", \"items\": {\"type\": \"string\"}, \"description\": \"Replacement lines for replace_lines.\"},\n"
        "                \"anchor\": {\"type\": \"string\", \"description\": \"Exact anchor text for insert operations.\"},\n"
        "                \"start\": {\"type\": \"string\", \"description\": \"Start anchor for replace_between.\"},\n"
        "                \"end\": {\"type\": \"string\", \"description\": \"End anchor for replace_between.\"},\n"
        "                \"include\": {\"type\": \"string\", \"description\": \"Line to ensure is present in a file.\"},\n"
                "                \"expected_sha256\": {\"type\": \"string\", \"description\": \"Pre-edit SHA-256 hash of the target file.\"},\n"
        "                \"expected_normalized_sha256\": {\"type\": \"string\", \"description\": \"Optional normalized-content hash returned by hash; distinguishes newline-only drift from content drift.\"},\n"
        "                \"start_line\": {\"type\": \"integer\", \"minimum\": 1, \"description\": \"1-based inclusive start line for replace_lines.\"},\n"
        "                \"end_line\": {\"type\": \"integer\", \"minimum\": 1, \"description\": \"1-based inclusive end line for replace_lines.\"},\n"
        "                \"expected_contains\": {\"type\": \"array\", \"items\": {\"type\": \"string\"}, \"description\": \"Validation substrings required in the selected line range.\"}\n"
                "              },\n"
                "              \"additionalProperties\": false\n"
        "            }\n"
        "          }\n"
        "        },\n"
        "        \"additionalProperties\": false\n"
        "      }\n"
        "    },\n"
        "    {\n"
        "      \"name\": \"apply\",\n"
        "      \"annotations\": {\n"
        "        \"readOnlyHint\": false,\n"
        "        \"destructiveHint\": true,\n"
        "        \"idempotentHint\": false\n"
        "      },\n"
        "      \"description\": \"Hash every target file first, pass the hash as expected_sha256, use op replace_exact for an ordinary exact replacement, put the original text in find and the replacement content in text, preview first, then apply the reviewed transaction and use the returned transaction_id for rollback.\",\n"
        "      \"inputSchema\": {\n"
        "        \"type\": \"object\",\n"
        "        \"required\": [\"workspace_root\", \"edits\"],\n"
        "        \"properties\": {\n"
        "          \"workspace_root\": {\"type\": \"string\"},\n"
        "          \"summary\": {\"type\": \"string\"},\n"
        "          \"actor\": {\"type\": \"string\"},\n"
        "          \"session\": {\"type\": \"object\", \"properties\": {\"id\": {\"type\": \"string\", \"pattern\": \"^sess-.+\"}, \"goal\": {\"type\": \"string\"}}, \"additionalProperties\": false, \"description\": \"Optional session metadata. If supplied, session.id must start with sess-.\"},\n"
        "          \"allow_suspicious\": {\"type\": \"boolean\"},\n"
        "          \"validation\": {\"type\": \"object\"},\n"
        "          \"testing\": {\"type\": \"object\"},\n"
        "          \"edits\": {\n"
        "            \"type\": \"array\",\n"
        "            \"items\": {\n"
        "              \"type\": \"object\",\n"
        "              \"required\": [\"op\", \"file\"],\n"
                "              \"properties\": {\n"
                "                \"op\": {\"type\": \"string\", \"enum\": [\"replace_exact\", \"replace_all_exact\", \"insert_before_exact\", \"insert_after_exact\", \"insert_before_exact_line\", \"insert_after_exact_line\", \"delete_exact\", \"create_file\", \"rewrite_file\", \"replace_between\", \"replace_lines\", \"ensure_include\"], \"description\": \"Use replace_exact for an ordinary single replacement; create_file only creates absent targets, while rewrite_file deliberately replaces or creates a complete file.\"},\n"
        "                \"file\": {\"type\": \"string\", \"description\": \"Workspace-relative file path.\"},\n"
        "                \"find\": {\"type\": \"string\", \"description\": \"Original text to match exactly.\"},\n"
        "                \"text\": {\"type\": \"string\", \"description\": \"Replacement content to write.\"},\n"
        "                \"new_lines\": {\"type\": \"array\", \"items\": {\"type\": \"string\"}, \"description\": \"Replacement lines for replace_lines.\"},\n"
        "                \"anchor\": {\"type\": \"string\", \"description\": \"Exact anchor text for insert operations.\"},\n"
        "                \"start\": {\"type\": \"string\", \"description\": \"Start anchor for replace_between.\"},\n"
        "                \"end\": {\"type\": \"string\", \"description\": \"End anchor for replace_between.\"},\n"
        "                \"include\": {\"type\": \"string\", \"description\": \"Line to ensure is present in a file.\"},\n"
                "                \"expected_sha256\": {\"type\": \"string\", \"description\": \"Pre-edit SHA-256 hash of the target file.\"},\n"
        "                \"expected_normalized_sha256\": {\"type\": \"string\", \"description\": \"Optional normalized-content hash returned by hash; distinguishes newline-only drift from content drift.\"},\n"
        "                \"start_line\": {\"type\": \"integer\", \"minimum\": 1, \"description\": \"1-based inclusive start line for replace_lines.\"},\n"
        "                \"end_line\": {\"type\": \"integer\", \"minimum\": 1, \"description\": \"1-based inclusive end line for replace_lines.\"},\n"
        "                \"expected_contains\": {\"type\": \"array\", \"items\": {\"type\": \"string\"}, \"description\": \"Validation substrings required in the selected line range.\"}\n"
                "              },\n"
                "              \"additionalProperties\": false\n"
        "            }\n"
        "          }\n"
        "        },\n"
        "        \"additionalProperties\": false\n"
        "      }\n"
        "    },\n"
        "    {\n"
        "      \"name\": \"rollback\",\n"
        "      \"annotations\": {\n"
        "        \"readOnlyHint\": false,\n"
        "        \"destructiveHint\": true,\n"
        "        \"idempotentHint\": false\n"
        "      },\n"
        "      \"description\": \"Roll back a recorded PatchTrack transaction when rollback preconditions still hold.\",\n"
        "      \"inputSchema\": {\n"
        "        \"type\": \"object\",\n"
        "        \"required\": [\"workspace_root\", \"transaction_id\"],\n"
        "        \"properties\": {\n"
        "          \"workspace_root\": {\"type\": \"string\"},\n"
        "          \"transaction_id\": {\"type\": \"string\"},\n"
        "          \"actor\": {\"type\": \"string\"},\n"
        "          \"session\": {\"type\": \"object\", \"properties\": {\"id\": {\"type\": \"string\", \"pattern\": \"^sess-.+\"}, \"goal\": {\"type\": \"string\"}}, \"additionalProperties\": false, \"description\": \"Optional session metadata. If supplied, session.id must start with sess-.\"}\n"
        "        },\n"
        "        \"additionalProperties\": false\n"
        "      }\n"
        "    },\n"
        "    {\n"
        "      \"name\": \"hash\",\n"
        "      \"annotations\": {\n"
        "        \"readOnlyHint\": true,\n"
        "        \"destructiveHint\": false,\n"
        "        \"idempotentHint\": true\n"
        "      },\n"
        "      \"description\": \"Return the SHA-256 of a file.\",\n"
        "      \"inputSchema\": {\n"
        "        \"type\": \"object\",\n"
        "        \"required\": [\"path\"],\n"
        "        \"properties\": {\n"
        "          \"path\": {\"type\": \"string\"}\n"
        "        },\n"
        "        \"additionalProperties\": false\n"
        "      }\n"
        "    },\n"
        "    {\n"
        "      \"name\": \"history\",\n"
        "      \"annotations\": {\n"
        "        \"readOnlyHint\": true,\n"
        "        \"destructiveHint\": false,\n"
        "        \"idempotentHint\": true\n"
        "      },\n"
        "      \"description\": \"List stored transaction history for a workspace.\",\n"
        "      \"inputSchema\": {\n"
        "        \"type\": \"object\",\n"
        "        \"required\": [\"workspace_root\"],\n"
        "        \"properties\": {\n"
        "          \"workspace_root\": {\"type\": \"string\"}\n"
        "        },\n"
        "        \"additionalProperties\": false\n"
        "      }\n"
        "    },\n"
        "    {\n"
        "      \"name\": \"recovery_scan\",\n"
        "      \"annotations\": {\n"
        "        \"readOnlyHint\": false,\n"
        "        \"destructiveHint\": false,\n"
        "        \"idempotentHint\": true\n"
        "      },\n"
        "      \"description\": \"Run the startup recovery scan for a workspace and report stale claims or pending transactions.\",\n"
        "      \"inputSchema\": {\n"
        "        \"type\": \"object\",\n"
        "        \"required\": [\"workspace_root\"],\n"
        "        \"properties\": {\n"
        "          \"workspace_root\": {\"type\": \"string\"}\n"
        "        },\n"
        "        \"additionalProperties\": false\n"
        "      }\n"
        "    }\n"
        "  ]\n"
        "}\n";
}

bool ValidateToolCall(const String& tool_name, const Value& args, String& error_json)
{
    String name = NormalizeToolName(tool_name);

    if(name == "preview" || name == "apply")
        return ValidatePreviewApplyArgs(args, error_json);
    if(name == "rollback")
        return ValidateRollbackArgs(args, error_json);
    if(name == "hash")
        return ValidateSingleStringArg(args, "path", error_json);
    if(name == "history" || name == "recovery_scan")
        return ValidateSingleStringArg(args, "workspace_root", error_json);
    if(name == "version") {
        if(!ExpectMap(args, "arguments", error_json))
            return false;
        Vector<String> allowed;
        return RejectUnexpectedKeys(args, allowed, error_json);
    }
    return true;
}

bool RunCoreRequestTool(const String& tool_name, const Value& args, String& result_json, bool& is_error)
{
    String name = NormalizeToolName(tool_name);
    String error;
    bool ok = false;

    if(name == "preview")
        ok = PatchtrackPreview(args, result_json, error);
    else if(name == "apply")
        ok = PatchtrackApply(args, result_json, error);
    else if(name == "rollback")
        ok = PatchtrackRollback(args, result_json, error);
    else if(name == "recovery_scan") {
        String workspace_root;
        if(!ExpectStringField(args, "workspace_root", true, workspace_root, result_json)) {
            is_error = true;
            return true;
        }
        ok = PatchtrackRecoveryScan(workspace_root, result_json, error);
    }
    else
        return false;

    if(!ok && name != "preview") {
        result_json = PatchtrackFormatErrorJson(error);
        is_error = true;
    }
    else
        is_error = !ok;
    return true;
}

bool RunHashOrHistoryTool(const String& tool_name, const Value& args, String& result_json, bool& is_error)
{
    String name = NormalizeToolName(tool_name);
    String text;
    String error;

    if(name == "version") {
        result_json = BuildVersionStructuredJson();
        is_error = false;
        return true;
    }

    if(name == "hash") {
        String path;
        if(!ExpectStringField(args, "path", true, path, result_json)) {
            is_error = true;
            return true;
        }
        String normalized_sha256;
        String newline;
        if(!PatchtrackHashDetails(path, text, normalized_sha256, newline, error)) {
            result_json = PatchtrackFormatErrorJson(error);
            is_error = true;
            return true;
        }
        result_json = BuildHashStructuredJson(path, text, normalized_sha256, newline);
        is_error = false;
        return true;
    }

    if(name == "history") {
        String workspace_root;
        if(!ExpectStringField(args, "workspace_root", true, workspace_root, result_json)) {
            is_error = true;
            return true;
        }
        if(!PatchtrackHistory(workspace_root, text, error)) {
            result_json = PatchtrackFormatErrorJson(error);
            is_error = true;
            return true;
        }
        result_json = BuildHistoryStructuredJson(workspace_root, text);
        is_error = false;
        return true;
    }

    return false;
}

bool HandleToolCall(const Value& params, String& result_json)
{
    Value tool_name_value = params["name"];
    if(IsNull(tool_name_value) || !tool_name_value.Is<String>()) {
        result_json = BuildToolResult(BuildSimpleFailureJson("BAD_REQUEST", "Tool call is missing a string 'name'."), true);
        return true;
    }

    String tool_name = (String)tool_name_value;
    String normalized_tool_name = NormalizeToolName(tool_name);
    Value args = params["arguments"];
    if(IsNull(args))
        args = Value(ValueMap());

    String validation_error;
    if(!ValidateToolCall(tool_name, args, validation_error)) {
        result_json = BuildToolResult(validation_error, true);
        return true;
    }

    String tool_json;
    bool is_error = false;
    if(RunCoreRequestTool(normalized_tool_name, args, tool_json, is_error) ||
       RunHashOrHistoryTool(normalized_tool_name, args, tool_json, is_error)) {
        result_json = BuildToolResult(tool_json, is_error);
        return true;
    }

    result_json = BuildToolResult(BuildSimpleFailureJson("UNKNOWN_TOOL", "Unknown PatchTrack MCP tool: " + normalized_tool_name), true);
    return true;
}

bool HandleJsonRpcRequest(const Value& request, String& response_json, bool& has_response)
{
    has_response = false;

    Value method_value = request["method"];
    if(IsNull(method_value) || !method_value.Is<String>()) {
        response_json = BuildJsonRpcError(JsonScalarLiteral(request["id"]), -32600, "Invalid Request");
        has_response = true;
        return false;
    }

    String method = (String)method_value;
    Value id_value = request["id"];
    String id_literal = JsonScalarLiteral(id_value);
    bool has_id = !IsNull(id_value);

    if(method == "notifications/initialized")
        return true;

    if(method == "initialize") {
        if(has_id) {
            response_json = BuildJsonRpcResult(id_literal, BuildInitializeResult());
            has_response = true;
        }
        return true;
    }

    if(method == "ping") {
        if(has_id) {
            response_json = BuildJsonRpcResult(id_literal, "{}\n");
            has_response = true;
        }
        return true;
    }

    if(method == "tools/list") {
        if(has_id) {
            response_json = BuildJsonRpcResult(id_literal, BuildToolsListResult());
            has_response = true;
        }
        return true;
    }

    if(method == "tools/call") {
        if(!has_id)
            return true;

        Value params = request["params"];
        if(IsNull(params))
            params = Value(ValueMap());
        if(!params.Is<ValueMap>()) {
            response_json = BuildJsonRpcError(id_literal, -32602, "Invalid params");
            has_response = true;
            return false;
        }

        String tool_result;
        HandleToolCall(params, tool_result);
        response_json = BuildJsonRpcResult(id_literal, tool_result);
        has_response = true;
        return true;
    }

    if(has_id) {
        response_json = BuildJsonRpcError(id_literal, -32601, "Method not found");
        has_response = true;
    }
    return false;
}

bool ReadMcpMessage(String& body, String& error, bool& eof)
{
    body.Clear();
    error.Clear();
    eof = false;

    for(;;) {
        int ch = fgetc(stdin);
        if(ch == EOF) {
            if(body.IsEmpty()) {
                eof = true;
                return false;
            }
            error = "Unexpected EOF while reading MCP message.";
            return false;
        }
        if(ch == '\n')
            break;
        if(ch != '\r')
            body.Cat((char)ch);
        if(body.GetCount() > 16 * 1024 * 1024) {
            error = "MCP message exceeded 16 MB.";
            return false;
        }
    }
    return true;
}

void WriteMcpMessage(const String& body)
{
    // MCP stdio requires one compact JSON-RPC object per line. Internal builders
    // stay readable, so compact the object only at the transport boundary.
    String message = body;
    try {
        message = AsJSON(ParseJSON(body), false);
    }
    catch(CParser::Error) {
        // Keep the original body so a transport error is still observable.
    }
    fwrite(~message, 1, message.GetLength(), stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

bool ProcessJsonRpcBody(const String& message, String& response_json, bool& has_response)
{
    Value parsed;
    try {
        parsed = ParseJSON(message);
    }
    catch(CParser::Error) {
        response_json = BuildJsonRpcError("null", -32700, "Invalid JSON");
        has_response = true;
        return false;
    }

    if(!parsed.Is<ValueMap>()) {
        response_json = BuildJsonRpcError("null", -32600, "Invalid Request");
        has_response = true;
        return false;
    }

    return HandleJsonRpcRequest(parsed, response_json, has_response);
}

int RunOneShot(const String& request_file)
{
    String body = LoadFile(request_file);
    if(IsNull(body)) {
        WriteMcpMessage(BuildJsonRpcError("null", -32700, "Unable to read request file"));
        return 1;
    }

    String response_json;
    bool has_response = false;
    ProcessJsonRpcBody(body, response_json, has_response);
    if(has_response)
        WriteMcpMessage(response_json);
    return has_response ? 0 : 1;
}

int RunBench(const String& request_file, int iterations)
{
    String body = LoadFile(request_file);
    if(IsNull(body)) {
        Cout() << "mcp-bench: failed to read request file\n";
        return 1;
    }
    if(iterations < 1)
        iterations = 1;

    String response_json;
    bool has_response = false;
    if(!ProcessJsonRpcBody(body, response_json, has_response) || !has_response) {
        Cout() << "mcp-bench: request did not produce a valid response\n";
        return 1;
    }

    TimeStop timer;
    for(int i = 0; i < iterations; i++) {
        response_json.Clear();
        has_response = false;
        if(!ProcessJsonRpcBody(body, response_json, has_response) || !has_response) {
            Cout() << "mcp-bench: dispatch failed at iteration " << i << "\n";
            return 1;
        }
    }

    double avg_ms = timer.Elapsed() / 1000.0 / iterations;
    Cout() << Format("mcp-bench: iterations=%d avg_ms=%.3f\n", iterations, avg_ms);
    return 0;
}

int RunServerLoop()
{
    for(;;) {
        String message;
        String error;
        bool eof = false;
        if(!ReadMcpMessage(message, error, eof)) {
            if(eof)
                return 0;
            WriteMcpMessage(BuildJsonRpcError("null", -32700, error));
            return 1;
        }

        Value parsed;
        try {
            parsed = ParseJSON(message);
        }
        catch(CParser::Error) {
            WriteMcpMessage(BuildJsonRpcError("null", -32700, "Invalid JSON"));
            return 1;
        }

        if(!parsed.Is<ValueMap>()) {
            WriteMcpMessage(BuildJsonRpcError("null", -32600, "Invalid Request"));
            return 1;
        }

        String response_json;
        bool has_response = false;
        HandleJsonRpcRequest(parsed, response_json, has_response);
        if(has_response)
            WriteMcpMessage(response_json);
    }
}

int RunSelfTest()
{
    SelfTestState st;
    String response;
    bool has_response = false;

    Value init = ParseJSON("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}\n");
    Expect(st, HandleJsonRpcRequest(init, response, has_response), "initialize dispatch failed");
    Expect(st, has_response, "initialize should respond");
    Expect(st, response.Find("patchtrack_mcp") >= 0, "initialize response missing server name");
    Expect(st, response.Find(PatchtrackVersion()) >= 0, "initialize response missing PatchTrack version");

    Value list = ParseJSON("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}\n");
    Expect(st, HandleJsonRpcRequest(list, response, has_response), "tools/list dispatch failed");
    Expect(st, has_response, "tools/list should respond");
    Expect(st, response.Find("\"name\": \"apply\"") >= 0, "tools/list missing apply");
    Expect(st, response.Find("patchtrack_apply") < 0, "tools/list should not advertise host-prefixed names");
    Expect(st, response.Find("\"readOnlyHint\": false") >= 0, "tools/list missing annotations");
    Expect(st, response.Find("transaction_id") >= 0, "tools/list missing tighter rollback schema");
    Expect(st, response.Find("\"name\": \"version\"") >= 0, "tools/list missing version tool");

    String root = AppendFileName(GetCurrentDirectory(), "_mcp_selftest");
    RealizeDirectory(root);
    String path = AppendFileName(root, "hash.txt");
    SaveFile(path, "alpha\nbeta\n");

    String hash_call;
    hash_call << "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":{"
              << "\"name\":\"hash\",\"arguments\":{\"path\":" << JString(path) << "}}}";
    Value hash_req = ParseJSON(hash_call);
    Expect(st, HandleJsonRpcRequest(hash_req, response, has_response), "hash tool dispatch failed");
    Expect(st, has_response, "hash tool should respond");
    Expect(st, response.Find("sha256") >= 0, "hash tool response missing sha256 field");

    Value version_req = ParseJSON("{\"jsonrpc\":\"2.0\",\"id\":30,\"method\":\"tools/call\",\"params\":{\"name\":\"version\",\"arguments\":{}}}");
    Expect(st, HandleJsonRpcRequest(version_req, response, has_response), "version tool dispatch failed");
    Expect(st, response.Find(PatchtrackVersion()) >= 0, "version tool response missing active version");

    String preview_call;
    preview_call << "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"tools/call\",\"params\":{"
                 << "\"name\":\"preview\",\"arguments\":{"
                 << "\"workspace_root\":" << JString(root) << ","
                 << "\"summary\":\"preview smoke\","
                 << "\"actor\":\"mcp-selftest\","
                 << "\"edits\":[{\"op\":\"replace_exact\",\"file\":\"hash.txt\",\"find\":\"beta\\n\",\"text\":\"gamma\\n\"}]"
                 << "}}}";
    Value preview_req = ParseJSON(preview_call);
    Expect(st, HandleJsonRpcRequest(preview_req, response, has_response), "preview tool dispatch failed");
    Expect(st, has_response, "preview tool should respond");
    Expect(st, response.Find("structuredContent") >= 0, "preview tool response missing structuredContent");
    Expect(st, response.Find("\"ok\": true") >= 0 || response.Find("\"ok\":true") >= 0,
           "preview tool response missing ok flag");

    String compat_call;
    compat_call << "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"tools/call\",\"params\":{"
                << "\"name\":\"patchtrack_preview\",\"arguments\":{"
                << "\"workspace_root\":" << JString(root) << ","
                << "\"summary\":\"preview smoke\","
                << "\"actor\":\"mcp-selftest\","
                << "\"edits\":[{\"op\":\"replace_exact\",\"file\":\"hash.txt\",\"find\":\"beta\\n\",\"text\":\"gamma\\n\"}]"
                << "}}}";
    Value compat_req = ParseJSON(compat_call);
    Expect(st, HandleJsonRpcRequest(compat_req, response, has_response), "compat preview tool dispatch failed");
    Expect(st, has_response, "compat preview tool should respond");

    String bad_hash_call = "{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"tools/call\",\"params\":{\"name\":\"hash\",\"arguments\":{\"path\":\"x\",\"extra\":1}}}";
    Value bad_hash_req = ParseJSON(bad_hash_call);
    Expect(st, HandleJsonRpcRequest(bad_hash_req, response, has_response), "bad hash tool dispatch failed");
    Expect(st, response.Find("BAD_REQUEST") >= 0, "bad hash call did not reject unexpected field");

    if(st.failures.IsEmpty()) {
        Cout() << "mcp-selftest: ok\n";
        return 0;
    }

    Cout() << "mcp-selftest: failed\n";
    for(int i = 0; i < st.failures.GetCount(); i++)
        Cout() << " - " << st.failures[i] << "\n";
    return 1;
}

} // namespace

CONSOLE_APP_MAIN
{
    const Vector<String>& cmd = CommandLine();
    if(cmd.GetCount() == 1 && cmd[0] == "--selftest") {
        SetExitCode(RunSelfTest());
        return;
    }

    if(cmd.GetCount() == 2 && cmd[0] == "--oneshot") {
        SetExitCode(RunOneShot(cmd[1]));
        return;
    }

    if(cmd.GetCount() >= 2 && cmd[0] == "--bench") {
        int iterations = cmd.GetCount() >= 3 ? ScanInt(cmd[2]) : 100;
        SetExitCode(RunBench(cmd[1], iterations));
        return;
    }

    if(!cmd.IsEmpty()) {
        Cout() << "patchtrack_mcp [--selftest] [--oneshot request.json] [--bench request.json iterations]\n";
        SetExitCode(2);
        return;
    }

    SetExitCode(RunServerLoop());
}

