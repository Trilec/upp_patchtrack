/*
PatchTrack MCP frontend.

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

String BuildToolResult(const String& structured_json, bool is_error)
{
    String out;
    out << "{\n"
        << "  \"content\": [\n"
        << "    {\"type\": \"text\", \"text\": " << JString(structured_json) << "}\n"
        << "  ],\n"
        << "  \"structuredContent\": " << structured_json << ",\n"
        << "  \"isError\": " << (is_error ? "true" : "false") << "\n"
        << "}\n";
    return out;
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
    if(!ExpectObjectField(args, "session", false, error_json))
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
    if(!ExpectStringField(args, "session", false, discard, error_json))
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

String BuildHashStructuredJson(const String& path, const String& result_text)
{
    int split = result_text.Find("  ");
    String sha = split >= 0 ? result_text.Left(split) : TrimBoth(result_text);

    String out;
    out << "{\n"
        << "  \"ok\": true,\n"
        << "  \"sha256\": " << JString(sha) << ",\n"
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
    return
        "{\n"
        "  \"protocolVersion\": \"2024-11-05\",\n"
        "  \"serverInfo\": {\n"
        "    \"name\": \"patchtrack_mcp\",\n"
        "    \"version\": \"0.1.0\"\n"
        "  },\n"
        "  \"capabilities\": {\n"
        "    \"tools\": {\n"
        "      \"listChanged\": false\n"
        "    }\n"
        "  }\n"
        "}\n";
}

String BuildToolsListResult()
{
    return
        "{\n"
        "  \"tools\": [\n"
        "    {\n"
        "      \"name\": \"patchtrack_preview\",\n"
        "      \"description\": \"Validate and diff a PatchTrack request without writing workspace files.\",\n"
        "      \"inputSchema\": {\n"
        "        \"type\": \"object\",\n"
        "        \"required\": [\"workspace_root\", \"edits\"],\n"
        "        \"properties\": {\n"
        "          \"workspace_root\": {\"type\": \"string\"},\n"
        "          \"summary\": {\"type\": \"string\"},\n"
        "          \"actor\": {\"type\": \"string\"},\n"
        "          \"session\": {\"type\": \"object\", \"properties\": {\"id\": {\"type\": \"string\"}, \"goal\": {\"type\": \"string\"}}, \"additionalProperties\": false},\n"
        "          \"allow_suspicious\": {\"type\": \"boolean\"},\n"
        "          \"validation\": {\"type\": \"object\"},\n"
        "          \"testing\": {\"type\": \"object\"},\n"
        "          \"edits\": {\n"
        "            \"type\": \"array\",\n"
        "            \"items\": {\n"
        "              \"type\": \"object\",\n"
        "              \"required\": [\"op\", \"file\"],\n"
                "              \"properties\": {\n"
                "                \"op\": {\"type\": \"string\"},\n"
        "                \"file\": {\"type\": \"string\"},\n"
        "                \"find\": {\"type\": \"string\"},\n"
        "                \"text\": {\"type\": \"string\"},\n"
        "                \"replace\": {\"type\": \"string\"},\n"
        "                \"new_text\": {\"type\": \"string\"},\n"
        "                \"new_lines\": {\"type\": \"array\", \"items\": {\"type\": \"string\"}},\n"
        "                \"anchor\": {\"type\": \"string\"},\n"
        "                \"start\": {\"type\": \"string\"},\n"
        "                \"end\": {\"type\": \"string\"},\n"
        "                \"include\": {\"type\": \"string\"},\n"
        "                \"expected_sha256\": {\"type\": \"string\"},\n"
        "                \"expected_hash\": {\"type\": \"string\"},\n"
        "                \"start_line\": {\"type\": \"integer\"},\n"
        "                \"end_line\": {\"type\": \"integer\"},\n"
        "                \"expected_contains\": {\"type\": \"array\", \"items\": {\"type\": \"string\"}}\n"
                "              },\n"
                "              \"additionalProperties\": false\n"
        "            }\n"
        "          }\n"
        "        },\n"
        "        \"additionalProperties\": false\n"
        "      }\n"
        "    },\n"
        "    {\n"
        "      \"name\": \"patchtrack_apply\",\n"
        "      \"description\": \"Apply a PatchTrack request transactionally with journaling and recovery data.\",\n"
        "      \"inputSchema\": {\n"
        "        \"type\": \"object\",\n"
        "        \"required\": [\"workspace_root\", \"edits\"],\n"
        "        \"properties\": {\n"
        "          \"workspace_root\": {\"type\": \"string\"},\n"
        "          \"summary\": {\"type\": \"string\"},\n"
        "          \"actor\": {\"type\": \"string\"},\n"
        "          \"session\": {\"type\": \"object\", \"properties\": {\"id\": {\"type\": \"string\"}, \"goal\": {\"type\": \"string\"}}, \"additionalProperties\": false},\n"
        "          \"allow_suspicious\": {\"type\": \"boolean\"},\n"
        "          \"validation\": {\"type\": \"object\"},\n"
        "          \"testing\": {\"type\": \"object\"},\n"
        "          \"edits\": {\n"
        "            \"type\": \"array\",\n"
        "            \"items\": {\n"
        "              \"type\": \"object\",\n"
        "              \"required\": [\"op\", \"file\"],\n"
                "              \"properties\": {\n"
                "                \"op\": {\"type\": \"string\"},\n"
        "                \"file\": {\"type\": \"string\"},\n"
        "                \"find\": {\"type\": \"string\"},\n"
        "                \"text\": {\"type\": \"string\"},\n"
        "                \"replace\": {\"type\": \"string\"},\n"
        "                \"new_text\": {\"type\": \"string\"},\n"
        "                \"new_lines\": {\"type\": \"array\", \"items\": {\"type\": \"string\"}},\n"
        "                \"anchor\": {\"type\": \"string\"},\n"
        "                \"start\": {\"type\": \"string\"},\n"
        "                \"end\": {\"type\": \"string\"},\n"
        "                \"include\": {\"type\": \"string\"},\n"
        "                \"expected_sha256\": {\"type\": \"string\"},\n"
        "                \"expected_hash\": {\"type\": \"string\"},\n"
        "                \"start_line\": {\"type\": \"integer\"},\n"
        "                \"end_line\": {\"type\": \"integer\"},\n"
        "                \"expected_contains\": {\"type\": \"array\", \"items\": {\"type\": \"string\"}}\n"
                "              },\n"
                "              \"additionalProperties\": false\n"
        "            }\n"
        "          }\n"
        "        },\n"
        "        \"additionalProperties\": false\n"
        "      }\n"
        "    },\n"
        "    {\n"
        "      \"name\": \"patchtrack_rollback\",\n"
        "      \"description\": \"Roll back a recorded PatchTrack transaction when rollback preconditions still hold.\",\n"
        "      \"inputSchema\": {\n"
        "        \"type\": \"object\",\n"
        "        \"required\": [\"workspace_root\", \"transaction_id\"],\n"
        "        \"properties\": {\n"
        "          \"workspace_root\": {\"type\": \"string\"},\n"
        "          \"transaction_id\": {\"type\": \"string\"},\n"
        "          \"actor\": {\"type\": \"string\"},\n"
        "          \"session\": {\"type\": \"object\", \"properties\": {\"id\": {\"type\": \"string\"}, \"goal\": {\"type\": \"string\"}}, \"additionalProperties\": false}\n"
        "        },\n"
        "        \"additionalProperties\": false\n"
        "      }\n"
        "    },\n"
        "    {\n"
        "      \"name\": \"patchtrack_hash\",\n"
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
        "      \"name\": \"patchtrack_history\",\n"
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
        "      \"name\": \"patchtrack_recovery_scan\",\n"
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
    if(tool_name == "patchtrack_preview" || tool_name == "patchtrack_apply")
        return ValidatePreviewApplyArgs(args, error_json);
    if(tool_name == "patchtrack_rollback")
        return ValidateRollbackArgs(args, error_json);
    if(tool_name == "patchtrack_hash")
        return ValidateSingleStringArg(args, "path", error_json);
    if(tool_name == "patchtrack_history" || tool_name == "patchtrack_recovery_scan")
        return ValidateSingleStringArg(args, "workspace_root", error_json);
    return true;
}

bool RunCoreRequestTool(const String& tool_name, const Value& args, String& result_json, bool& is_error)
{
    String error;
    bool ok = false;

    if(tool_name == "patchtrack_preview")
        ok = PatchtrackPreview(args, result_json, error);
    else if(tool_name == "patchtrack_apply")
        ok = PatchtrackApply(args, result_json, error);
    else if(tool_name == "patchtrack_rollback")
        ok = PatchtrackRollback(args, result_json, error);
    else if(tool_name == "patchtrack_recovery_scan") {
        String workspace_root;
        if(!ExpectStringField(args, "workspace_root", true, workspace_root, result_json)) {
            is_error = true;
            return true;
        }
        ok = PatchtrackRecoveryScan(workspace_root, result_json, error);
    }
    else
        return false;

    if(!ok && tool_name != "patchtrack_preview") {
        result_json = PatchtrackFormatErrorJson(error);
        is_error = true;
    }
    else
        is_error = !ok;
    return true;
}

bool RunHashOrHistoryTool(const String& tool_name, const Value& args, String& result_json, bool& is_error)
{
    String text;
    String error;

    if(tool_name == "patchtrack_hash") {
        String path;
        if(!ExpectStringField(args, "path", true, path, result_json)) {
            is_error = true;
            return true;
        }
        if(!PatchtrackHash(path, text, error)) {
            result_json = PatchtrackFormatErrorJson(error);
            is_error = true;
            return true;
        }
        result_json = BuildHashStructuredJson(path, text);
        is_error = false;
        return true;
    }

    if(tool_name == "patchtrack_history") {
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
    if(RunCoreRequestTool(tool_name, args, tool_json, is_error) ||
       RunHashOrHistoryTool(tool_name, args, tool_json, is_error)) {
        result_json = BuildToolResult(tool_json, is_error);
        return true;
    }

    result_json = BuildToolResult(BuildSimpleFailureJson("UNKNOWN_TOOL", "Unknown PatchTrack MCP tool: " + tool_name), true);
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

bool ReadFramedMessage(String& body, String& error, bool& eof)
{
    body.Clear();
    error.Clear();
    eof = false;

    String header;
    for(;;) {
        int ch = fgetc(stdin);
        if(ch == EOF) {
            if(header.IsEmpty()) {
                eof = true;
                return false;
            }
            error = "Unexpected EOF while reading MCP headers.";
            return false;
        }
        header.Cat((char)ch);
        // Windows text-mode stdin may translate CRLF to LF before the parser sees it.
        // MCP peers normally send CRLF, but accepting LF-only framing keeps the
        // server interoperable with redirected and host-managed standard streams.
        if(header.EndsWith("\r\n\r\n") || header.EndsWith("\n\n"))
            break;
        if(header.GetCount() > 8192) {
            error = "MCP header exceeded 8 KB.";
            return false;
        }
    }

    int content_length = -1;
    Vector<String> lines = Split(header, "\n");
    for(int i = 0; i < lines.GetCount(); i++) {
        String line = TrimBoth(lines[i]);
        if(line.IsEmpty())
            continue;
        int colon = line.Find(':');
        if(colon < 0)
            continue;
        String key = ToLower(TrimBoth(line.Left(colon)));
        String value = TrimBoth(line.Mid(colon + 1));
        if(key == "content-length")
            content_length = ScanInt(value);
    }

    if(content_length < 0) {
        error = "Missing Content-Length header.";
        return false;
    }

    String data;
    data.Reserve(content_length);
    while(data.GetCount() < content_length) {
        char buffer[1024];
        int need = min<int>(content_length - data.GetCount(), (int)sizeof(buffer));

        // Large tool responses can come back in chunks, so keep reading until the
        // declared byte count is satisfied instead of assuming a single fread wins.
        int read = (int)fread(buffer, 1, need, stdin);
        if(read <= 0) {
            error = "Unexpected EOF while reading MCP body.";
            return false;
        }
        data.Cat(buffer, read);
    }

    body = data;
    return true;
}

void WriteFramedMessage(const String& body)
{
    String header;
    header << "Content-Length: " << body.GetLength() << "\r\n\r\n";

    fwrite(~header, 1, header.GetLength(), stdout);
    fwrite(~body, 1, body.GetLength(), stdout);
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
        WriteFramedMessage(BuildJsonRpcError("null", -32700, "Unable to read request file"));
        return 1;
    }

    String response_json;
    bool has_response = false;
    ProcessJsonRpcBody(body, response_json, has_response);
    if(has_response)
        WriteFramedMessage(response_json);
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
        if(!ReadFramedMessage(message, error, eof)) {
            if(eof)
                return 0;
            WriteFramedMessage(BuildJsonRpcError("null", -32700, error));
            return 1;
        }

        Value parsed;
        try {
            parsed = ParseJSON(message);
        }
        catch(CParser::Error) {
            WriteFramedMessage(BuildJsonRpcError("null", -32700, "Invalid JSON"));
            return 1;
        }

        if(!parsed.Is<ValueMap>()) {
            WriteFramedMessage(BuildJsonRpcError("null", -32600, "Invalid Request"));
            return 1;
        }

        String response_json;
        bool has_response = false;
        HandleJsonRpcRequest(parsed, response_json, has_response);
        if(has_response)
            WriteFramedMessage(response_json);
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

    Value list = ParseJSON("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}\n");
    Expect(st, HandleJsonRpcRequest(list, response, has_response), "tools/list dispatch failed");
    Expect(st, has_response, "tools/list should respond");
    Expect(st, response.Find("patchtrack_apply") >= 0, "tools/list missing patchtrack_apply");
    Expect(st, response.Find("transaction_id") >= 0, "tools/list missing tighter rollback schema");

    String root = AppendFileName(GetCurrentDirectory(), "_mcp_selftest");
    RealizeDirectory(root);
    String path = AppendFileName(root, "hash.txt");
    SaveFile(path, "alpha\nbeta\n");

    String hash_call;
    hash_call << "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":{"
              << "\"name\":\"patchtrack_hash\",\"arguments\":{\"path\":" << JString(path) << "}}}";
    Value hash_req = ParseJSON(hash_call);
    Expect(st, HandleJsonRpcRequest(hash_req, response, has_response), "hash tool dispatch failed");
    Expect(st, has_response, "hash tool should respond");
    Expect(st, response.Find("sha256") >= 0, "hash tool response missing sha256 field");

    String preview_call;
    preview_call << "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"tools/call\",\"params\":{"
                 << "\"name\":\"patchtrack_preview\",\"arguments\":{"
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

    String bad_hash_call = "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"tools/call\",\"params\":{\"name\":\"patchtrack_hash\",\"arguments\":{\"path\":\"x\",\"extra\":1}}}";
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

