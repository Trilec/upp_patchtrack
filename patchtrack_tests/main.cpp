/*
PatchTrack protocol test harness.

Runs the compiled patchtrack executable end-to-end against disposable workspaces.
The focus is protocol behavior, journal integrity, rollback safety, and stress coverage.

Change log:
- 2026-04-27: Added journal-state assertions, rollback recovery tests, sequential
  transaction stress, and richer console reporting.
*/

#include <Core/Core.h>
#include <patchtrack_core/patchtrack_core.h>

using namespace Upp;

namespace {

struct CommandResult : Moveable<CommandResult> {
    bool started = false;
    int exit_code = -999;
    String out;
};

struct Harness : Moveable<Harness> {
    String repo_root;
    String patchtrack_exe;
    String patchtrack_mcp_exe;
    Vector<String> failures;
    int test_count = 0;
    int passed_count = 0;
};

struct CaseLog : NoCopy {
    Harness& h;
    String name;
    String description;
    int start_failures;
    TimeStop timer;

    CaseLog(Harness& harness, const char *label, const char *detail)
        : h(harness), name(label), description(detail), start_failures(harness.failures.GetCount())
    {
        h.test_count++;
        Cout() << Format("[%02d] %s\n", h.test_count, ~name);
        Cout() << "     " << description << "\n";
    }

    ~CaseLog()
    {
        int elapsed_ms = timer.Elapsed() / 1000;
        if(h.failures.GetCount() == start_failures) {
            h.passed_count++;
            Cout() << Format("     PASS (%d ms)\n", elapsed_ms);
            return;
        }
        Cout() << Format("     FAIL (%d ms)\n", elapsed_ms);
        for(int i = start_failures; i < h.failures.GetCount(); i++)
            Cout() << "       - " << h.failures[i] << "\n";
    }
};

template <class T, int N>
constexpr int CountOf(const T (&)[N])
{
    return N;
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

bool IsFolderPath(const String& path)
{
    FindFile ff(path);
    return ff && ff.IsFolder();
}

bool Expect(Harness& h, bool condition, const String& message)
{
    if(condition)
        return true;
    h.failures.Add(message);
    return false;
}

bool ExpectStarts(Harness& h, const String& actual, const String& prefix, const String& label)
{
    return Expect(h, actual.StartsWith(prefix), label + ": expected prefix " + prefix + " actual=" + actual);
}

String MakeCaseRoot(Harness& h, const String& name)
{
    String base = AppendFileName(h.repo_root, "_protocol_tests");
    RealizeDirectory(base);
    String root = AppendFileName(base, name + "-" + AsString(GetSysTime().year) + Format("-%08x", (int)Random()));
    RealizeDirectory(root);
    return root;
}

bool WriteFileText(const String& path, const String& text)
{
    String dir = GetFileFolder(path);
    if(!RealizeDirectory(dir))
        return false;
    return SaveFile(path, text);
}

CommandResult RunTool(const String& exe, const Vector<String>& args, const String& cd)
{
    CommandResult r;
    LocalProcess p;
    p.NoConvertCharset();
    r.started = p.Start(~exe, args, NULL, ~cd);
    if(!r.started)
        return r;
    r.exit_code = p.Finish(r.out);
    return r;
}

bool ParseMcpResponseBody(const String& framed, String& body)
{
    String trimmed = TrimBoth(framed);
    if(trimmed.StartsWith("{")) {
        body = trimmed;
        return true;
    }

    int split = framed.Find("\r\n\r\n");
    int header_len = 4;
    if(split < 0) {
        split = framed.Find("\n\n");
        header_len = 2;
    }
    if(split < 0)
        return false;
    body = framed.Mid(split + header_len);
    return true;
}

CommandResult RunMcpRequest(Harness& h, const String& root, const String& json_body)
{
    String io_root = AppendFileName(root, "_mcp_io");
    RealizeDirectory(io_root);
    String tag = Format("%08x", (int)Random());
    String input = AppendFileName(io_root, "request-" + tag + ".json");
    SaveFile(input, json_body);

    Vector<String> args;
    args.Add("--oneshot");
    args.Add(input);
    return RunTool(h.patchtrack_mcp_exe, args, h.repo_root);
}

String BuildMcpToolCall(int id, const String& tool_name, const String& arguments_json)
{
    return Format("{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"tools/call\",\"params\":{\"name\":%s,\"arguments\":%s}}",
                  id,
                  ~JString(tool_name),
                  ~arguments_json);
}

bool ParseMcpJsonResult(const CommandResult& r, Value& parsed, String& framed_body)
{
    if(!r.started || r.exit_code != 0)
        return false;
    if(!ParseMcpResponseBody(r.out, framed_body))
        return false;
    if(TrimBoth(framed_body).IsEmpty())
        return false;
    try {
        parsed = ParseJSON(framed_body);
        return true;
    }
    catch(CParser::Error) {
        return false;
    }
}

String ExtractJsonStringField(const String& json, const char *field)
{
    String marker = JString(field) + ":";
    int pos = json.ReverseFind(marker);
    if(pos < 0)
        return String();
    pos += marker.GetCount();
    while(pos < json.GetCount() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n'))
        pos++;
    if(pos >= json.GetCount() || json[pos] != '"')
        return String();
    pos++;

    String out;
    bool escape = false;
    for(; pos < json.GetCount(); pos++) {
        char ch = json[pos];
        if(escape) {
            switch(ch) {
            case '"': out.Cat('"'); break;
            case '\\': out.Cat('\\'); break;
            case '/': out.Cat('/'); break;
            case 'b': out.Cat('\b'); break;
            case 'f': out.Cat('\f'); break;
            case 'n': out.Cat('\n'); break;
            case 'r': out.Cat('\r'); break;
            case 't': out.Cat('\t'); break;
            default: out.Cat(ch); break;
            }
            escape = false;
            continue;
        }
        if(ch == '\\') {
            escape = true;
            continue;
        }
        if(ch == '"')
            return out;
        out.Cat(ch);
    }
    return String();
}

double ExtractNamedMetricMs(const String& text, const String& marker)
{
    int pos = text.Find(marker);
    if(pos < 0)
        return -1;
    pos += marker.GetCount();
    return ScanDouble(text.Mid(pos));
}

String FindFirstSha256Hex(const String& text)
{
    for(int i = 0; i + 64 <= text.GetCount(); i++) {
        bool ok = true;
        for(int j = 0; j < 64; j++) {
            char c = text[i + j];
            if(!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
                ok = false;
                break;
            }
        }
        if(ok)
            return text.Mid(i, 64);
    }
    return String();
}

bool ParseJsonOutput(const String& text, Value& out)
{
    if(TrimBoth(text).IsEmpty())
        return false;
    try {
        out = ParseJSON(text);
        return true;
    }
    catch(CParser::Error) {
        return false;
    }
}

String WriteRequestFile(const String& root, const String& name, const String& body)
{
    String path = AppendFileName(root, name);
    SaveFile(path, body);
    return path;
}

CommandResult RunJsonCommand(Harness& h, const String& command, const String& request_file)
{
    Vector<String> args;
    args.Add(command);
    args.Add(request_file);
    return RunTool(h.patchtrack_exe, args, h.repo_root);
}

bool ExpectJsonResult(Harness& h, const CommandResult& r, bool expected_ok, Value& parsed, const String& label)
{
    if(!Expect(h, r.started, label + ": process failed to start"))
        return false;
    if(!Expect(h, ParseJsonOutput(r.out, parsed), label + ": invalid json output: " + r.out))
        return false;
    bool ok = !IsNull(parsed["ok"]) && (bool)parsed["ok"];
    if(expected_ok) {
        Expect(h, r.exit_code == 0, label + ": expected exit 0, got " + AsString(r.exit_code));
        return Expect(h, ok, label + ": expected ok=true output=" + r.out);
    }
    Expect(h, r.exit_code != 0, label + ": expected non-zero exit code");
    return Expect(h, !ok, label + ": expected ok=false output=" + r.out);
}

bool ExpectErrorCode(Harness& h, const Value& parsed, const String& code, const String& label)
{
    return ExpectStarts(h, (String)parsed["error"], code, label);
}

bool ExpectField(Harness& h, const Value& parsed, const String& field, const String& expected, const String& label)
{
    return Expect(h, (String)parsed[field] == expected,
                  label + ": expected " + field + "=" + expected + " actual=" + (String)parsed[field]);
}

bool ExpectFieldContains(Harness& h, const Value& parsed, const String& field, const String& expected_substring, const String& label)
{
    return Expect(h, ((String)parsed[field]).Find(expected_substring) >= 0,
                  label + ": expected " + field + " to contain " + expected_substring + " actual=" + (String)parsed[field]);
}

String HashFileViaTool(Harness& h, const String& path)
{
    Vector<String> args;
    args.Add("hash");
    args.Add(path);
    CommandResult r = RunTool(h.patchtrack_exe, args, h.repo_root);
    if(!Expect(h, r.started, "hash command failed to start for " + path))
        return String();
    if(!Expect(h, r.exit_code == 0, "hash command failed for " + path + ": " + r.out))
        return String();
    int p = r.out.Find("  ");
    if(!Expect(h, p == 64, "hash output format unexpected for " + path + ": " + r.out))
        return String();
    return r.out.Left(p);
}

void TestCommandSurface(Harness& h)
{
    CaseLog case_log(h, "command-surface", "Confirms hash and history entry points are reachable and return recognizable output.");
    String root = MakeCaseRoot(h, "command-surface");
    String rel = "src/hash.txt";
    String abs = AppendFileName(root, rel);
    String body = "alpha\nbeta\n";
    Expect(h, WriteFileText(abs, body), "command-surface: failed to write hash seed file");

    Vector<String> hargs;
    hargs.Add("hash");
    hargs.Add(abs);
    CommandResult hash = RunTool(h.patchtrack_exe, hargs, h.repo_root);
    Expect(h, hash.started, "command-surface: hash process failed to start");
    Expect(h, hash.exit_code == 0, "command-surface: hash process failed");
    Expect(h, hash.out.Find(abs) >= 0, "command-surface: hash output missing path");
    Expect(h, hash.out.Find("  ") == 64, "command-surface: hash output missing digest prefix");

    Vector<String> histargs;
    histargs.Add("history");
    histargs.Add(root);
    CommandResult history = RunTool(h.patchtrack_exe, histargs, h.repo_root);
    Expect(h, history.started, "command-surface: history process failed to start");
    Expect(h, history.exit_code == 0, "command-surface: history process failed");
    Expect(h, history.out.Find("Journal: ") >= 0, "command-surface: history output missing journal header");
}

void TestPreviewNoWrite(Harness& h)
{
    CaseLog case_log(h, "preview-no-write", "Checks that preview produces diffs without mutating files or creating journal state.");
    String root = MakeCaseRoot(h, "preview-no-write");
    String rel = "src/sample.txt";
    String abs = AppendFileName(root, rel);
    String before = "alpha\nbeta\n";
    Expect(h, WriteFileText(abs, before), "preview-no-write: failed to write seed file");
    String hash = HashFileViaTool(h, abs);
    if(hash.IsEmpty())
        return;

    String req;
    req << "{\n"
        << "  \"workspace_root\": " << JString(root) << ",\n"
        << "  \"summary\": \"preview only\",\n"
        << "  \"actor\": \"harness\",\n"
        << "  \"edits\": [\n"
        << "    {\n"
        << "      \"op\": \"replace_exact\",\n"
        << "      \"file\": " << JString(rel) << ",\n"
        << "      \"find\": \"beta\\n\",\n"
        << "      \"text\": \"gamma\\n\",\n"
        << "      \"expected_sha256\": " << JString(hash) << "\n"
        << "    }\n"
        << "  ]\n"
        << "}\n";

    Value out;
    if(!ExpectJsonResult(h, RunJsonCommand(h, "preview", WriteRequestFile(root, "preview.json", req)), true, out, "preview-no-write"))
        return;
    Expect(h, LoadFile(abs) == before, "preview-no-write: preview mutated file contents");
    Expect(h, !IsFolderPath(AppendFileName(root, ".patchtrack")), "preview-no-write: preview created journal");
    Value files = out["files"];
    Expect(h, files.GetCount() == 1, "preview-no-write: expected one file in preview output");
    if(files.GetCount() == 1)
        Expect(h, ((String)files[0]["diff"]).Find("+gamma") >= 0, "preview-no-write: diff missing expected text");
}

void TestBatchPrimitivesAndRollback(Harness& h)
{
    CaseLog case_log(h, "exact-anchor-edits", "Exercises exact replacement, anchor insertion, rewrite, include, line-range, apply, and rollback.");
    String root = MakeCaseRoot(h, "batch-primitives");

    struct Seed { const char *rel; const char *text; };
    const Seed seed[] = {
        { "src/exact.txt", "hello\nworld\n" },
        { "src/all.txt", "cat\ncat\n" },
        { "src/before.txt", "body\n" },
        { "src/after.txt", "body\n" },
        { "src/delete.txt", "keep\nremove\nkeep2\n" },
        { "src/between.txt", "a\nBEGIN\nold\nEND\nz\n" },
        { "src/include.cpp", "#include <A>\n\nint main() {}\n" },
        { "src/lines.txt", "zero\none\ntwo\nthree\n" },
        { "src/rewrite.txt", "old\n" },
    };

    Vector<String> hashes;
    for(int i = 0; i < CountOf(seed); i++) {
        String path = AppendFileName(root, seed[i].rel);
        Expect(h, WriteFileText(path, seed[i].text), "batch-primitives: failed to write seed " + String(seed[i].rel));
        hashes.Add(HashFileViaTool(h, path));
        if(hashes.Top().IsEmpty())
            return;
    }

    String req;
    req << "{\n"
        << "  \"workspace_root\": " << JString(root) << ",\n"
        << "  \"summary\": \"batch primitives\",\n"
        << "  \"actor\": \"harness\",\n"
        << "  \"session\": {\"goal\": \"exercise primitives\"},\n"
        << "  \"edits\": [\n"
        << "    {\"op\":\"replace_exact\",\"file\":" << JString(seed[0].rel) << ",\"find\":\"world\\n\",\"text\":\"there\\n\",\"expected_sha256\":" << JString(hashes[0]) << "},\n"
        << "    {\"op\":\"replace_all_exact\",\"file\":" << JString(seed[1].rel) << ",\"find\":\"cat\",\"text\":\"dog\",\"expected_sha256\":" << JString(hashes[1]) << "},\n"
        << "    {\"op\":\"insert_before_exact\",\"file\":" << JString(seed[2].rel) << ",\"anchor\":\"body\\n\",\"text\":\"head\\n\",\"expected_sha256\":" << JString(hashes[2]) << "},\n"
        << "    {\"op\":\"insert_after_exact\",\"file\":" << JString(seed[3].rel) << ",\"anchor\":\"body\\n\",\"text\":\"tail\\n\",\"expected_sha256\":" << JString(hashes[3]) << "},\n"
        << "    {\"op\":\"delete_exact\",\"file\":" << JString(seed[4].rel) << ",\"find\":\"remove\\n\",\"expected_sha256\":" << JString(hashes[4]) << "},\n"
        << "    {\"op\":\"replace_between\",\"file\":" << JString(seed[5].rel) << ",\"start\":\"BEGIN\\n\",\"end\":\"END\\n\",\"text\":\"new\\n\",\"expected_sha256\":" << JString(hashes[5]) << "},\n"
        << "    {\"op\":\"ensure_include\",\"file\":" << JString(seed[6].rel) << ",\"include\":\"#include <B>\",\"expected_sha256\":" << JString(hashes[6]) << "},\n"
        << "    {\"op\":\"replace_lines\",\"file\":" << JString(seed[7].rel) << ",\"start_line\":2,\"end_line\":3,\"expected_contains\":[\"one\",\"two\"],\"new_lines\":[\"ONE\",\"TWO\"],\"expected_sha256\":" << JString(hashes[7]) << "},\n"
        << "    {\"op\":\"rewrite_file\",\"file\":" << JString(seed[8].rel) << ",\"text\":\"fresh\\nblock\\n\",\"expected_sha256\":" << JString(hashes[8]) << "}\n"
        << "  ]\n"
        << "}\n";

    String request = WriteRequestFile(root, "batch.json", req);
    Value preview;
    if(!ExpectJsonResult(h, RunJsonCommand(h, "preview", request), true, preview, "batch-primitives preview"))
        return;
    Expect(h, !IsFolderPath(AppendFileName(root, ".patchtrack")), "batch-primitives: preview created journal unexpectedly");

    Value apply;
    if(!ExpectJsonResult(h, RunJsonCommand(h, "apply", request), true, apply, "batch-primitives apply"))
        return;

    Expect(h, LoadFile(AppendFileName(root, seed[0].rel)) == "hello\nthere\n", "batch-primitives: replace_exact mismatch");
    Expect(h, LoadFile(AppendFileName(root, seed[1].rel)) == "dog\ndog\n", "batch-primitives: replace_all_exact mismatch");
    Expect(h, LoadFile(AppendFileName(root, seed[2].rel)) == "head\nbody\n", "batch-primitives: insert_before mismatch");
    Expect(h, LoadFile(AppendFileName(root, seed[3].rel)) == "body\ntail\n", "batch-primitives: insert_after mismatch");
    Expect(h, LoadFile(AppendFileName(root, seed[4].rel)) == "keep\nkeep2\n", "batch-primitives: delete_exact mismatch");
    Expect(h, LoadFile(AppendFileName(root, seed[5].rel)) == "a\nBEGIN\nnew\nEND\nz\n", "batch-primitives: replace_between mismatch");
    Expect(h, LoadFile(AppendFileName(root, seed[6].rel)) == "#include <A>\n#include <B>\n\nint main() {}\n", "batch-primitives: ensure_include mismatch");
    Expect(h, LoadFile(AppendFileName(root, seed[7].rel)) == "zero\nONE\nTWO\nthree\n", "batch-primitives: replace_lines mismatch");
    Expect(h, LoadFile(AppendFileName(root, seed[8].rel)) == "fresh\nblock\n", "batch-primitives: rewrite_file mismatch");
    Expect(h, IsFolderPath(AppendFileName(root, ".patchtrack")), "batch-primitives: apply did not create journal");
    for(int i = 0; i < CountOf(seed); i++) {
        String temp_pattern = AppendFileName(root, String(seed[i].rel) + ".patchtrack-tmp-*");
        Expect(h, !FindFile(temp_pattern), "batch-primitives: atomic write temporary file was left behind: " + temp_pattern);
    }

    String tran_id = (String)apply["transaction_id"];
    String session_id = (String)apply["session_id"];
    Expect(h, !tran_id.IsEmpty(), "batch-primitives: missing transaction_id");
    Expect(h, !session_id.IsEmpty(), "batch-primitives: missing session_id");

    Vector<String> histargs;
    histargs.Add("history");
    histargs.Add(root);
    CommandResult history = RunTool(h.patchtrack_exe, histargs, h.repo_root);
    Expect(h, history.started && history.exit_code == 0, "batch-primitives: history command failed");
    Expect(h, history.out.Find(session_id) >= 0, "batch-primitives: history missing session id");
    Expect(h, history.out.Find(tran_id) >= 0, "batch-primitives: history missing transaction id");

    String rb;
    rb << "{\n"
       << "  \"workspace_root\": " << JString(root) << ",\n"
       << "  \"transaction_id\": " << JString(tran_id) << ",\n"
       << "  \"actor\": \"harness\"\n"
       << "}\n";

    Value rollback;
    if(!ExpectJsonResult(h, RunJsonCommand(h, "rollback", WriteRequestFile(root, "rollback.json", rb)), true, rollback, "batch-primitives rollback"))
        return;

    for(int i = 0; i < CountOf(seed); i++)
        Expect(h, LoadFile(AppendFileName(root, seed[i].rel)) == seed[i].text, "batch-primitives: rollback failed for " + String(seed[i].rel));
}

void TestAliasesAndNoOpPreview(Harness& h)
{
    CaseLog case_log(h, "aliases-and-noop-preview", "Covers alias op names and confirms no-op preview reports changed=false without mutating files.");
    String root = MakeCaseRoot(h, "aliases");

    Expect(h, WriteFileText(AppendFileName(root, "src/before.txt"), "body\n"), "aliases: failed to write before seed");
    Expect(h, WriteFileText(AppendFileName(root, "src/after.txt"), "body\n"), "aliases: failed to write after seed");
    Expect(h, WriteFileText(AppendFileName(root, "src/include.cpp"), "#include <A>\n#include <B>\n"), "aliases: failed to write include seed");

    String h1 = HashFileViaTool(h, AppendFileName(root, "src/before.txt"));
    String h2 = HashFileViaTool(h, AppendFileName(root, "src/after.txt"));
    String h3 = HashFileViaTool(h, AppendFileName(root, "src/include.cpp"));
    if(h1.IsEmpty() || h2.IsEmpty() || h3.IsEmpty())
        return;

    String req;
    req << "{\n"
        << "  \"workspace_root\": " << JString(root) << ",\n"
        << "  \"summary\": \"alias preview\",\n"
        << "  \"actor\": \"harness\",\n"
        << "  \"edits\": [\n"
        << "    {\"op\":\"insert_before_exact_line\",\"file\":\"src/before.txt\",\"anchor\":\"body\\n\",\"text\":\"head\\n\",\"expected_sha256\":" << JString(h1) << "},\n"
        << "    {\"op\":\"insert_after_exact_line\",\"file\":\"src/after.txt\",\"anchor\":\"body\\n\",\"text\":\"tail\\n\",\"expected_sha256\":" << JString(h2) << "},\n"
        << "    {\"op\":\"ensure_include\",\"file\":\"src/include.cpp\",\"include\":\"#include <B>\",\"expected_sha256\":" << JString(h3) << "}\n"
        << "  ]\n"
        << "}\n";

    Value out;
    if(!ExpectJsonResult(h, RunJsonCommand(h, "preview", WriteRequestFile(root, "alias.json", req)), true, out, "aliases preview"))
        return;
    Value files = out["files"];
    Expect(h, files.GetCount() == 3, "aliases: expected three preview file results");
    if(files.GetCount() == 3) {
        Expect(h, (bool)files[0]["changed"], "aliases: before alias should report changed=true");
        Expect(h, (bool)files[1]["changed"], "aliases: after alias should report changed=true");
        Expect(h, !(bool)files[2]["changed"], "aliases: ensure_include no-op should report changed=false");
    }
    Expect(h, LoadFile(AppendFileName(root, "src/before.txt")) == "body\n", "aliases: preview mutated before file");
    Expect(h, LoadFile(AppendFileName(root, "src/after.txt")) == "body\n", "aliases: preview mutated after file");
}

void TestEngineFailures(Harness& h)
{
    CaseLog case_log(h, "validation-failures", "Confirms engine refusals stay side-effect free across malformed, ambiguous, unsafe, and invalid edit requests.");
    String root = MakeCaseRoot(h, "engine-failures");

    String hash_path = AppendFileName(root, "src/hash.txt");
    String amb_path = AppendFileName(root, "src/amb.txt");
    String safe_path = AppendFileName(root, "src/safe.txt");
    Expect(h, WriteFileText(hash_path, "one\ntwo\n"), "engine-failures: failed to write hash seed");
    Expect(h, WriteFileText(amb_path, "dup\ndup\n"), "engine-failures: failed to write ambiguous seed");
    Expect(h, WriteFileText(safe_path, "safe\ntext\n"), "engine-failures: failed to write validation seed");

    String good_hash = HashFileViaTool(h, hash_path);
    String amb_hash = HashFileViaTool(h, amb_path);
    String safe_hash = HashFileViaTool(h, safe_path);
    if(good_hash.IsEmpty() || amb_hash.IsEmpty() || safe_hash.IsEmpty())
        return;

    Value out;

    String hash_req;
    hash_req << "{\n"
             << "  \"workspace_root\": " << JString(root) << ",\n"
             << "  \"summary\": \"bad hash\",\n"
             << "  \"actor\": \"harness\",\n"
             << "  \"edits\": [{\"op\":\"replace_exact\",\"file\":\"src/hash.txt\",\"find\":\"two\\n\",\"text\":\"three\\n\",\"expected_sha256\":\"deadbeef\"}]\n"
             << "}\n";
    if(ExpectJsonResult(h, RunJsonCommand(h, "preview", WriteRequestFile(root, "bad-hash.json", hash_req)), false, out, "engine-failures hash mismatch"))
        ExpectErrorCode(h, out, "HASH_MISMATCH", "engine-failures hash mismatch");
    Expect(h, LoadFile(hash_path) == "one\ntwo\n", "engine-failures: hash mismatch mutated file");

    String no_match_req;
    no_match_req << "{\n"
                 << "  \"workspace_root\": " << JString(root) << ",\n"
                 << "  \"summary\": \"no match\",\n"
                 << "  \"actor\": \"harness\",\n"
                 << "  \"edits\": [{\"op\":\"replace_exact\",\"file\":\"src/hash.txt\",\"find\":\"missing\\n\",\"text\":\"three\\n\",\"expected_sha256\":" << JString(good_hash) << "}]\n"
                 << "}\n";
    if(ExpectJsonResult(h, RunJsonCommand(h, "preview", WriteRequestFile(root, "no-match.json", no_match_req)), false, out, "engine-failures no-match"))
        ExpectErrorCode(h, out, "NO_MATCH", "engine-failures no-match");

    String amb_req;
    amb_req << "{\n"
            << "  \"workspace_root\": " << JString(root) << ",\n"
            << "  \"summary\": \"ambiguous\",\n"
            << "  \"actor\": \"harness\",\n"
            << "  \"edits\": [{\"op\":\"replace_exact\",\"file\":\"src/amb.txt\",\"find\":\"dup\\n\",\"text\":\"x\\n\",\"expected_sha256\":" << JString(amb_hash) << "}]\n"
            << "}\n";
    if(ExpectJsonResult(h, RunJsonCommand(h, "preview", WriteRequestFile(root, "amb.json", amb_req)), false, out, "engine-failures ambiguous"))
        ExpectErrorCode(h, out, "AMBIGUOUS_MATCH", "engine-failures ambiguous");

    String val_req;
    val_req << "{\n"
            << "  \"workspace_root\": " << JString(root) << ",\n"
            << "  \"summary\": \"validation fail\",\n"
            << "  \"actor\": \"harness\",\n"
            << "  \"edits\": [{\"op\":\"replace_exact\",\"file\":\"src/safe.txt\",\"find\":\"text\\n\",\"text\":\"<<<<<<<\\n\",\"expected_sha256\":" << JString(safe_hash) << "}]\n"
            << "}\n";
    if(ExpectJsonResult(h, RunJsonCommand(h, "preview", WriteRequestFile(root, "validation.json", val_req)), false, out, "engine-failures validation"))
        ExpectErrorCode(h, out, "VALIDATION_FAILED", "engine-failures validation");
    Expect(h, LoadFile(safe_path) == "safe\ntext\n", "engine-failures: validation failure mutated file");

    String must_req;
    must_req << "{\n"
             << "  \"workspace_root\": " << JString(root) << ",\n"
             << "  \"summary\": \"must contain fail\",\n"
             << "  \"actor\": \"harness\",\n"
             << "  \"edits\": [{\"op\":\"replace_exact\",\"file\":\"src/safe.txt\",\"find\":\"text\\n\",\"text\":\"done\\n\",\"expected_sha256\":" << JString(safe_hash) << "}],\n"
             << "  \"validation\": {\"must_contain\":[\"missing token\"]}\n"
             << "}\n";
    if(ExpectJsonResult(h, RunJsonCommand(h, "preview", WriteRequestFile(root, "must.json", must_req)), false, out, "engine-failures must-contain"))
        ExpectErrorCode(h, out, "VALIDATION_FAILED", "engine-failures must-contain");

    String file_req;
    file_req << "{\n"
             << "  \"workspace_root\": " << JString(root) << ",\n"
             << "  \"summary\": \"missing file\",\n"
             << "  \"actor\": \"harness\",\n"
             << "  \"edits\": [{\"op\":\"replace_exact\",\"file\":\"src/missing.txt\",\"find\":\"x\\n\",\"text\":\"y\\n\"}]\n"
             << "}\n";
    if(ExpectJsonResult(h, RunJsonCommand(h, "preview", WriteRequestFile(root, "missing-file.json", file_req)), false, out, "engine-failures file-not-found"))
        ExpectErrorCode(h, out, "FILE_NOT_FOUND", "engine-failures file-not-found");

    String path_req;
    path_req << "{\n"
             << "  \"workspace_root\": " << JString(root) << ",\n"
             << "  \"summary\": \"unsafe path\",\n"
             << "  \"actor\": \"harness\",\n"
             << "  \"edits\": [{\"op\":\"rewrite_file\",\"file\":\"..\\evil.txt\",\"text\":\"bad\\n\"}]\n"
             << "}\n";
    if(ExpectJsonResult(h, RunJsonCommand(h, "preview", WriteRequestFile(root, "unsafe.json", path_req)), false, out, "engine-failures unsafe-path"))
        ExpectErrorCode(h, out, "UNSAFE_PATH", "engine-failures unsafe-path");

    String bad_req;
    bad_req << "{\n"
            << "  \"workspace_root\": " << JString(root) << ",\n"
            << "  \"summary\": \"no edits\",\n"
            << "  \"actor\": \"harness\",\n"
            << "  \"edits\": []\n"
            << "}\n";
    if(ExpectJsonResult(h, RunJsonCommand(h, "preview", WriteRequestFile(root, "no-edits.json", bad_req)), false, out, "engine-failures bad-request"))
        ExpectErrorCode(h, out, "BAD_REQUEST", "engine-failures bad-request");

    String unsupported_req;
    unsupported_req << "{\n"
                    << "  \"workspace_root\": " << JString(root) << ",\n"
                    << "  \"summary\": \"unsupported\",\n"
                    << "  \"actor\": \"harness\",\n"
                    << "  \"edits\": [{\"op\":\"replace_method\",\"file\":\"src/hash.txt\",\"text\":\"x\\n\",\"expected_sha256\":" << JString(good_hash) << "}]\n"
                    << "}\n";
    if(ExpectJsonResult(h, RunJsonCommand(h, "preview", WriteRequestFile(root, "unsupported.json", unsupported_req)), false, out, "engine-failures unsupported-op"))
        ExpectErrorCode(h, out, "UNSUPPORTED_OP", "engine-failures unsupported-op");

    String range_req;
    range_req << "{\n"
              << "  \"workspace_root\": " << JString(root) << ",\n"
              << "  \"summary\": \"range error\",\n"
              << "  \"actor\": \"harness\",\n"
              << "  \"edits\": [{\"op\":\"replace_lines\",\"file\":\"src/hash.txt\",\"start_line\":8,\"end_line\":9,\"new_lines\":[\"x\"],\"expected_sha256\":" << JString(good_hash) << "}]\n"
              << "}\n";
    if(ExpectJsonResult(h, RunJsonCommand(h, "preview", WriteRequestFile(root, "range.json", range_req)), false, out, "engine-failures range-error"))
        ExpectErrorCode(h, out, "RANGE_ERROR", "engine-failures range-error");

    String malformed_req;
    malformed_req << "{\n"
                  << "  \"workspace_root\": " << JString(root) << ",\n"
                  << "  \"summary\": \"malformed types\",\n"
                  << "  \"actor\": \"harness\",\n"
                  << "  \"edits\": [{\"op\":\"replace_lines\",\"file\":\"src/hash.txt\",\"start_line\":\"2\",\"end_line\":3,\"new_lines\":[\"x\"]}]\n"
                  << "}\n";
    if(ExpectJsonResult(h, RunJsonCommand(h, "preview", WriteRequestFile(root, "malformed-types.json", malformed_req)), false, out, "engine-failures malformed-types"))
        ExpectErrorCode(h, out, "BAD_REQUEST", "engine-failures malformed-types");

    String journal_req;
    journal_req << "{\n"
                << "  \"workspace_root\": " << JString(root) << ",\n"
                << "  \"summary\": \"journal escape\",\n"
                << "  \"actor\": \"harness\",\n"
                << "  \"edits\": [{\"op\":\"rewrite_file\",\"file\":\".patchtrack/workspace.json\",\"text\":\"tamper\"}]\n"
                << "}\n";
    if(ExpectJsonResult(h, RunJsonCommand(h, "preview", WriteRequestFile(root, "journal-escape.json", journal_req)), false, out, "engine-failures journal-path"))
        ExpectErrorCode(h, out, "UNSAFE_PATH", "engine-failures journal-path");
}

void TestCreateAndDriftDiagnostics(Harness& h)
{
    CaseLog case_log(h, "create-and-drift", "Checks explicit and implicit file creation, exact rollback removal, newline-only drift diagnostics, and bounded large diffs.");
    String root = MakeCaseRoot(h, "create-and-drift");
    String rel = "generated/new.txt";
    String abs = AppendFileName(root, rel);

    String create_req;
    create_req << "{\n"
               << "  \"workspace_root\": " << JString(root) << ",\n"
               << "  \"summary\": \"create generated file\",\n"
               << "  \"actor\": \"harness\",\n"
               << "  \"edits\": [{\"op\":\"create_file\",\"file\":" << JString(rel) << ",\"text\":\"created\\n\"}]\n"
               << "}\n";
    Value out;
    if(!ExpectJsonResult(h, RunJsonCommand(h, "preview", WriteRequestFile(root, "create-preview.json", create_req)), true, out, "create-and-drift preview"))
        return;
    Expect(h, !FileExists(abs), "create-and-drift: preview created a workspace file");
    Expect(h, (bool)out["files"][0]["created"], "create-and-drift: preview did not mark new file as created");

    Value apply_out;
    if(!ExpectJsonResult(h, RunJsonCommand(h, "apply", WriteRequestFile(root, "create-apply.json", create_req)), true, apply_out, "create-and-drift apply"))
        return;
    Expect(h, LoadFile(abs) == "created\n", "create-and-drift: create_file content mismatch");
    Expect(h, (bool)apply_out["files"][0]["created"], "create-and-drift: journal result did not mark created file");

    String rollback_req;
    rollback_req << "{\"workspace_root\":" << JString(root) << ",\"transaction_id\":"
                 << JString((String)apply_out["transaction_id"]) << ",\"actor\":\"harness\"}";
    if(!ExpectJsonResult(h, RunJsonCommand(h, "rollback", WriteRequestFile(root, "create-rollback.json", rollback_req)), true, out, "create-and-drift rollback"))
        return;
    Expect(h, !FileExists(abs), "create-and-drift: rollback left a created file behind");

    Expect(h, WriteFileText(abs, "already here\n"), "create-and-drift: failed to seed existing file");
    if(ExpectJsonResult(h, RunJsonCommand(h, "preview", WriteRequestFile(root, "create-existing.json", create_req)), false, out, "create-and-drift existing create"))
        ExpectErrorCode(h, out, "FILE_ALREADY_EXISTS", "create-and-drift existing create");
    Expect(h, LoadFile(abs) == "already here\n", "create-and-drift: rejected create changed existing file");

    String rewrite_rel = "generated/rewrite.txt";
    String rewrite_abs = AppendFileName(root, rewrite_rel);
    String rewrite_req = String("{\"workspace_root\":") + JString(root)
                       + ",\"summary\":\"rewrite missing\",\"actor\":\"harness\",\"edits\":[{\"op\":\"rewrite_file\",\"file\":"
                       + JString(rewrite_rel) + ",\"text\":\"rewritten\\n\"}]}";
    if(!ExpectJsonResult(h, RunJsonCommand(h, "apply", WriteRequestFile(root, "rewrite-apply.json", rewrite_req)), true, apply_out, "create-and-drift rewrite missing"))
        return;
    Expect(h, LoadFile(rewrite_abs) == "rewritten\n", "create-and-drift: rewrite_file did not create missing target");

    String drift_rel = "src/drift.txt";
    String drift_abs = AppendFileName(root, drift_rel);
    Expect(h, WriteFileText(drift_abs, "one\ntwo\n"), "create-and-drift: failed to seed drift file");
    String raw_hash, normalized_hash, newline, error;
    if(!Expect(h, PatchtrackHashDetails(drift_abs, raw_hash, normalized_hash, newline, error), "create-and-drift: hash details failed: " + error))
        return;
    Expect(h, WriteFileText(drift_abs, "one\r\ntwo\r\n"), "create-and-drift: failed to introduce newline-only drift");
    String drift_req = String("{\"workspace_root\":") + JString(root)
                     + ",\"summary\":\"newline drift\",\"actor\":\"harness\",\"edits\":[{\"op\":\"replace_exact\",\"file\":"
                     + JString(drift_rel) + ",\"find\":\"two\\n\",\"text\":\"three\\n\",\"expected_sha256\":"
                     + JString(raw_hash) + ",\"expected_normalized_sha256\":" + JString(normalized_hash) + "}]}";
    if(ExpectJsonResult(h, RunJsonCommand(h, "preview", WriteRequestFile(root, "newline-drift.json", drift_req)), false, out, "create-and-drift newline drift"))
        ExpectField(h, out, "difference_kind", "newline_only", "create-and-drift newline drift");
    Expect(h, LoadFile(drift_abs) == "one\r\ntwo\r\n", "create-and-drift: newline drift request changed file");

    String large_before, large_after;
    for(int i = 0; i < 100; i++) {
        large_before << Format("old-%03d\n", i);
        large_after << Format("new-%03d\n", i);
    }
    String large_rel = "generated/large.txt";
    String large_abs = AppendFileName(root, large_rel);
    Expect(h, WriteFileText(large_abs, large_before), "create-and-drift: failed to seed large file");
    String large_hash = HashFileViaTool(h, large_abs);
    if(large_hash.IsEmpty())
        return;
    String large_req = String("{\"workspace_root\":") + JString(root)
                     + ",\"summary\":\"large rewrite\",\"actor\":\"harness\",\"edits\":[{\"op\":\"rewrite_file\",\"file\":"
                     + JString(large_rel) + ",\"text\":" + JString(large_after) + ",\"expected_sha256\":" + JString(large_hash) + "}]}";
    if(ExpectJsonResult(h, RunJsonCommand(h, "preview", WriteRequestFile(root, "large-preview.json", large_req)), true, out, "create-and-drift large preview")) {
        Expect(h, (bool)out["files"][0]["diff_summary"]["truncated"], "create-and-drift: large diff should be bounded");
        Expect(h, ((String)out["files"][0]["diff"]).Find("omitted") >= 0, "create-and-drift: large diff omitted marker missing");
    }
}

void TestTransportFailures(Harness& h)
{
    CaseLog case_log(h, "transport-failures", "Checks missing request files and malformed JSON fail before any journal or workspace mutation.");
    String root = MakeCaseRoot(h, "transport-failures");

    Vector<String> args;
    args.Add("preview");
    args.Add(AppendFileName(root, "missing.json"));
    CommandResult missing = RunTool(h.patchtrack_exe, args, h.repo_root);
    Expect(h, missing.started, "transport-failures: missing request process failed to start");
    Expect(h, missing.exit_code != 0, "transport-failures: missing request should fail");
    Expect(h, missing.out.Find("REQUEST_READ_FAILED") >= 0, "transport-failures: expected REQUEST_READ_FAILED");
    Expect(h, !IsFolderPath(AppendFileName(root, ".patchtrack")), "transport-failures: missing request created journal");

    String bad_json = AppendFileName(root, "bad.json");
    Expect(h, WriteFileText(bad_json, "{\"broken\":"), "transport-failures: failed to write bad json request");
    args[1] = bad_json;
    CommandResult bad = RunTool(h.patchtrack_exe, args, h.repo_root);
    Expect(h, bad.started, "transport-failures: bad json process failed to start");
    Expect(h, bad.exit_code != 0, "transport-failures: bad json should fail");
    Expect(h, bad.out.Find("BAD_JSON") >= 0, "transport-failures: expected BAD_JSON");
    Expect(h, !IsFolderPath(AppendFileName(root, ".patchtrack")), "transport-failures: bad json created journal");
}

void TestRollbackFailures(Harness& h)
{
    CaseLog case_log(h, "rollback-success-blocked", "Verifies rollback succeeds on untouched files and is blocked once workspace contents diverge.");
    String root = MakeCaseRoot(h, "rollback-failures");
    String rel = "src/file.txt";
    String abs = AppendFileName(root, rel);
    Expect(h, WriteFileText(abs, "red\nblue\n"), "rollback-failures: failed to write seed file");

    String missing_rb;
    missing_rb << "{\n"
               << "  \"workspace_root\": " << JString(root) << ",\n"
               << "  \"transaction_id\": \"tran-doesnotexist\",\n"
               << "  \"actor\": \"harness\"\n"
               << "}\n";
    Value out;
    if(ExpectJsonResult(h, RunJsonCommand(h, "rollback", WriteRequestFile(root, "missing-rb.json", missing_rb)), false, out, "rollback-failures missing-transaction"))
        ExpectErrorCode(h, out, "TRANSACTION_NOT_FOUND", "rollback-failures missing-transaction");

    String hash = HashFileViaTool(h, abs);
    if(hash.IsEmpty())
        return;

    String apply_req;
    apply_req << "{\n"
              << "  \"workspace_root\": " << JString(root) << ",\n"
              << "  \"summary\": \"rollback blocked\",\n"
              << "  \"actor\": \"harness\",\n"
              << "  \"edits\": [{\"op\":\"replace_exact\",\"file\":" << JString(rel) << ",\"find\":\"blue\\n\",\"text\":\"green\\n\",\"expected_sha256\":" << JString(hash) << "}]\n"
              << "}\n";
    Value apply_out;
    if(!ExpectJsonResult(h, RunJsonCommand(h, "apply", WriteRequestFile(root, "apply.json", apply_req)), true, apply_out, "rollback-failures apply"))
        return;
    Expect(h, LoadFile(abs) == "red\ngreen\n", "rollback-failures: apply content mismatch");

    Expect(h, SaveFile(abs, "red\nyellow\n"), "rollback-failures: failed to diverge file");
    String rb;
    rb << "{\n"
       << "  \"workspace_root\": " << JString(root) << ",\n"
       << "  \"transaction_id\": " << JString((String)apply_out["transaction_id"]) << ",\n"
       << "  \"actor\": \"harness\"\n"
       << "}\n";
    if(ExpectJsonResult(h, RunJsonCommand(h, "rollback", WriteRequestFile(root, "rollback.json", rb)), false, out, "rollback-failures blocked"))
        ExpectErrorCode(h, out, "ROLLBACK_BLOCKED", "rollback-failures blocked");
    Expect(h, LoadFile(abs) == "red\nyellow\n", "rollback-failures: blocked rollback modified file");
}

Vector<String> ListTransactionFiles(const String& root)
{
    Vector<String> out;
    String journal = AppendFileName(root, ".patchtrack");
    FindFile sess(AppendFileName(journal, "sess-*"));
    while(sess) {
        if(sess.IsFolder()) {
            String sd = AppendFileName(journal, sess.GetName());
            FindFile tr(AppendFileName(sd, "tran-*.json"));
            while(tr) {
                if(tr.IsFile())
                    out.Add(AppendFileName(sd, tr.GetName()));
                tr.Next();
            }
        }
        sess.Next();
    }
    Sort(out);
    return out;
}

int CountTransactionLogs(const String& root)
{
    return ListTransactionFiles(root).GetCount();
}

bool LoadJsonFileValue(const String& path, Value& out)
{
    if(!FileExists(path))
        return false;
    return ParseJsonOutput(LoadFile(path), out);
}

String ClaimsRoot(const String& root)
{
    return AppendFileName(AppendFileName(root, ".patchtrack"), "claims");
}

String BuildStringArrayJson(const Vector<String>& items)
{
    String out;
    out << "[";
    for(int i = 0; i < items.GetCount(); i++) {
        if(i)
            out << ", ";
        out << JString(items[i]);
    }
    out << "]";
    return out;
}
bool WriteClaimFile(const String& root,
                    const String& session_id,
                    const String& intent,
                    const Vector<String>& files,
                    int heartbeat_age_sec,
                    int expires_in_sec,
                    const String& summary = "test claim",
                    const String& actor = "other-ai")
{
    String claims_root = ClaimsRoot(root);
    if(!RealizeDirectory(claims_root))
        return false;

    int now = (int)time(NULL);
    String body;
    body << "{\n"
         << "  \"session_id\": " << JString(session_id) << ",\n"
         << "  \"actor\": " << JString(actor) << ",\n"
         << "  \"summary\": " << JString(summary) << ",\n"
         << "  \"intent\": " << JString(intent) << ",\n"
         << "  \"started_epoch\": " << AsString(now - heartbeat_age_sec) << ",\n"
         << "  \"last_heartbeat_epoch\": " << AsString(now - heartbeat_age_sec) << ",\n"
         << "  \"expires_epoch\": " << AsString(now + expires_in_sec) << ",\n"
         << "  \"lease_seconds\": 120,\n"
         << "  \"files\": " << BuildStringArrayJson(files) << "\n"
         << "}\n";
    return SaveFile(AppendFileName(claims_root, session_id + ".json"), body);
}

bool LoadRecoveryScan(Harness& h, const String& root, Value& out, const String& label)
{
    String path = AppendFileName(AppendFileName(root, ".patchtrack"), "recovery_scan.json");
    return Expect(h, LoadJsonFileValue(path, out), label + ": failed to load recovery scan " + path);
}
bool ExpectSingleTransaction(Harness& h, const String& root, const String& label, String& path, Value& json)
{
    Vector<String> files = ListTransactionFiles(root);
    if(!Expect(h, files.GetCount() == 1, label + ": expected exactly one transaction log, got " + AsString(files.GetCount())))
        return false;
    path = files[0];
    return Expect(h, LoadJsonFileValue(path, json), label + ": failed to load transaction json " + path);
}

bool ExpectSnapshotsExist(Harness& h, const String& transaction_file, const Value& files, const String& label)
{
    String session_dir = GetFileFolder(transaction_file);
    for(int i = 0; i < files.GetCount(); i++) {
        String snap_rel = (String)files[i]["snapshot_before"];
        String snap_path = AppendFileName(session_dir, snap_rel);
        if(!Expect(h, FileExists(snap_path), label + ": missing snapshot " + snap_path))
            return false;
    }
    return true;
}

void TestSelfTestSmoke(Harness& h)
{
    CaseLog case_log(h, "selftest-smoke", "Runs the in-process engine selftest so timing-sensitive checks stay covered.");
    Vector<String> args;
    args.Add("selftest");
    CommandResult r = RunTool(h.patchtrack_exe, args, h.repo_root);
    Expect(h, r.started, "selftest-smoke: process failed to start");
    Expect(h, r.exit_code == 0, "selftest-smoke: expected exit 0 output=" + r.out);
    Expect(h, r.out.Find("selftest: ok") >= 0, "selftest-smoke: expected selftest: ok output=" + r.out);
}

void TestMcpSelfTestSmoke(Harness& h)
{
    CaseLog case_log(h, "mcp-selftest-smoke", "Runs the MCP frontend selftest so JSON-RPC dispatch stays wired to the shared core.");
    Vector<String> args;
    args.Add("--selftest");
    CommandResult r = RunTool(h.patchtrack_mcp_exe, args, h.repo_root);
    Expect(h, r.started, "mcp-selftest-smoke: process failed to start");
    Expect(h, r.exit_code == 0, "mcp-selftest-smoke: expected exit 0 output=" + r.out);
    Expect(h, r.out.Find("mcp-selftest: ok") >= 0, "mcp-selftest-smoke: expected mcp-selftest: ok output=" + r.out);
}

void TestMcpRoundTrip(Harness& h)
{
    CaseLog case_log(h, "mcp-roundtrip", "Exercises newline-delimited MCP tool calls for hash, preview, apply, rollback, and recovery scan.");
    String root = MakeCaseRoot(h, "mcp-roundtrip");
    String rel = "src/sample.txt";
    String abs = AppendFileName(root, rel);
    Expect(h, WriteFileText(abs, "alpha\nbeta\n"), "mcp-roundtrip: failed to write seed file");

    CommandResult tools_cmd = RunMcpRequest(h, root, String("{\"jsonrpc\":\"2.0\",\"id\":0,\"method\":\"tools/list\"}"));
    Value tools_rpc;
    String tools_body_json;
    if(!Expect(h, ParseMcpJsonResult(tools_cmd, tools_rpc, tools_body_json), "mcp-roundtrip: tools/list call failed: " + tools_cmd.out))
        return;
    Value tools = tools_rpc["result"]["tools"];
    Expect(h, tools.GetCount() == 7, "mcp-roundtrip: tools/list should expose seven tools");
    Expect(h, tools_cmd.out.Find("patchtrack_hash") < 0, "mcp-roundtrip: tools/list should not advertise host-prefixed names");
    Expect(h, tools_cmd.out.Find("\"replace_text\"") < 0, "mcp-roundtrip: tools/list should not advertise replace_text");
    const char *expected_tools[] = { "version", "preview", "apply", "rollback", "hash", "history", "recovery_scan" };
    for(int i = 0; i < CountOf(expected_tools); i++) {
        bool found = false;
        for(int j = 0; j < tools.GetCount(); j++) {
            if((String)tools[j]["name"] == expected_tools[i]) {
                found = true;
                Value annotations = tools[j]["annotations"];
                if(String(expected_tools[i]) == "version" || String(expected_tools[i]) == "preview") {
                    Expect(h, (bool)annotations["readOnlyHint"], "mcp-roundtrip: preview should be annotated read-only");
                    Expect(h, (bool)annotations["idempotentHint"], "mcp-roundtrip: preview should be annotated idempotent");
                    Expect(h, !(bool)annotations["destructiveHint"], "mcp-roundtrip: preview should not be destructive");
                }
                else if(String(expected_tools[i]) == "apply" || String(expected_tools[i]) == "rollback") {
                    Expect(h, !(bool)annotations["readOnlyHint"], String("mcp-roundtrip: ") + expected_tools[i] + " should not be annotated read-only");
                    Expect(h, (bool)annotations["destructiveHint"], String("mcp-roundtrip: ") + expected_tools[i] + " should be annotated destructive");
                }
                else if(String(expected_tools[i]) == "hash" || String(expected_tools[i]) == "history") {
                    Expect(h, (bool)annotations["readOnlyHint"], String("mcp-roundtrip: ") + expected_tools[i] + " should be annotated read-only");
                    Expect(h, (bool)annotations["idempotentHint"], String("mcp-roundtrip: ") + expected_tools[i] + " should be annotated idempotent");
                }
                else if(String(expected_tools[i]) == "recovery_scan") {
                    Expect(h, !(bool)annotations["readOnlyHint"], "mcp-roundtrip: recovery_scan should not be annotated read-only");
                    Expect(h, (bool)annotations["idempotentHint"], "mcp-roundtrip: recovery_scan should be annotated idempotent");
                }
                break;
            }
        }
        Expect(h, found, String("mcp-roundtrip: tools/list missing ") + expected_tools[i]);
    }
    for(int i = 0; i < tools.GetCount(); i++) {
        String name = (String)tools[i]["name"];
        if(name == "preview" || name == "apply") {
            Value op_schema = tools[i]["inputSchema"]["properties"]["edits"]["items"]["properties"]["op"];
            Value op_enum = op_schema["enum"];
            Expect(h, op_enum.GetCount() == 12, "mcp-roundtrip: op enum should list the canonical operations");
            const char *ops[] = {
                "replace_exact",
                "replace_all_exact",
                "insert_before_exact",
                "insert_after_exact",
                "insert_before_exact_line",
                "insert_after_exact_line",
                "delete_exact",
                "create_file",
                "rewrite_file",
                "replace_between",
                "replace_lines",
                "ensure_include"
            };
            for(int j = 0; j < CountOf(ops); j++) {
                bool found = false;
                for(int k = 0; k < op_enum.GetCount(); k++) {
                    if((String)op_enum[k] == ops[j]) {
                        found = true;
                        break;
                    }
                }
                Expect(h, found, String("mcp-roundtrip: op enum missing ") + ops[j]);
            }
            Expect(h, String(op_schema["description"]).Find("replace_exact") >= 0,
                   "mcp-roundtrip: op description should map ordinary replacements to replace_exact");
            Expect(h, String(tools[i]["description"]).Find("replacement content in text") >= 0,
                   "mcp-roundtrip: tool description should point agents to text");
            Expect(h, String(tools[i]["inputSchema"]["properties"]["session"]["type"]) == "object",
                   "mcp-roundtrip: session schema should be an object");
        }
    }

    CommandResult version_cmd = RunMcpRequest(h, root, BuildMcpToolCall(50, "version", "{}"));
    Value version_rpc;
    String version_body_json;
    if(!Expect(h, ParseMcpJsonResult(version_cmd, version_rpc, version_body_json), "mcp-roundtrip: version call failed: " + version_cmd.out))
        return;
    Expect(h, (String)version_rpc["result"]["structuredContent"]["version"] == PatchtrackVersion(),
           "mcp-roundtrip: version tool should return the active core version");

    String hash_body = BuildMcpToolCall(1, "hash", String("{\"path\":") + JString(abs) + "}");
    CommandResult hash_cmd = RunMcpRequest(h, root, hash_body);
    Value hash_rpc;
    String hash_body_json;
    if(!Expect(h, ParseMcpJsonResult(hash_cmd, hash_rpc, hash_body_json), "mcp-roundtrip: hash call failed: " + hash_cmd.out))
        return;
    String sha = FindFirstSha256Hex(hash_cmd.out);
    if(!Expect(h, sha.GetCount() == 64, "mcp-roundtrip: hash response missing sha256"))
        return;

    String preview_args;
    preview_args << "{"
                 << "\"workspace_root\":" << JString(root) << ","
                 << "\"summary\":\"mcp preview\","
                 << "\"actor\":\"harness\","
                 << "\"session\":{\"id\":\"sess-mcp-roundtrip\",\"goal\":\"replace beta with gamma\"},"
                 << "\"edits\":[{\"op\":\"replace_exact\",\"file\":" << JString(rel)
                 << ",\"find\":\"beta\\n\",\"text\":\"gamma\\n\",\"expected_sha256\":" << JString(sha) << "}]"
                 << "}";

    String invalid_session_string_args;
    invalid_session_string_args << "{"
                               << "\"workspace_root\":" << JString(root) << ","
                               << "\"summary\":\"mcp preview\","
                               << "\"actor\":\"harness\","
                               << "\"session\":\"legacy-string\","
                               << "\"edits\":[{\"op\":\"replace_exact\",\"file\":" << JString(rel)
                               << ",\"find\":\"beta\\n\",\"text\":\"gamma\\n\",\"expected_sha256\":" << JString(sha) << "}]"
                               << "}";
    int string_session_key_first = invalid_session_string_args.Find("\"session\"");
    Expect(h, string_session_key_first >= 0 && string_session_key_first == invalid_session_string_args.ReverseFind("\"session\""),
           "mcp-roundtrip: string session request should contain exactly one session key");
    CommandResult invalid_session_cmd = RunMcpRequest(h, root, BuildMcpToolCall(20, "preview", invalid_session_string_args));
    Expect(h, invalid_session_cmd.out.Find("BAD_REQUEST") >= 0, "mcp-roundtrip: string session should be rejected");
    Expect(h, invalid_session_cmd.out.Find("must be an object") >= 0, "mcp-roundtrip: string session should mention object");
    Expect(h, LoadFile(abs) == "alpha\nbeta\n", "mcp-roundtrip: string session mutation leaked through");

    String invalid_session_prefix_args;
    invalid_session_prefix_args << "{"
                                << "\"workspace_root\":" << JString(root) << ","
                                << "\"summary\":\"mcp preview\","
                                << "\"actor\":\"harness\","
                                << "\"session\":{\"id\":\"legacy-roundtrip\",\"goal\":\"replace beta with gamma\"},"
                                << "\"edits\":[{\"op\":\"replace_exact\",\"file\":" << JString(rel)
                                << ",\"find\":\"beta\\n\",\"text\":\"gamma\\n\",\"expected_sha256\":" << JString(sha) << "}]"
                                << "}";
    int prefix_session_key_first = invalid_session_prefix_args.Find("\"session\"");
    Expect(h, prefix_session_key_first >= 0 && prefix_session_key_first == invalid_session_prefix_args.ReverseFind("\"session\""),
           "mcp-roundtrip: prefixed-session request should contain exactly one session key");
    CommandResult invalid_prefix_cmd = RunMcpRequest(h, root, BuildMcpToolCall(21, "preview", invalid_session_prefix_args));
    Expect(h, invalid_prefix_cmd.out.Find("BAD_REQUEST") >= 0, "mcp-roundtrip: unprefixed session id should be rejected");
    Expect(h, invalid_prefix_cmd.out.Find("sess-") >= 0, "mcp-roundtrip: unprefixed session id should mention sess-");
    Expect(h, LoadFile(abs) == "alpha\nbeta\n", "mcp-roundtrip: unprefixed session mutation leaked through");

    String unsupported_args;
    unsupported_args << "{"
                     << "\"workspace_root\":" << JString(root) << ","
                     << "\"summary\":\"unsupported op\","
                     << "\"actor\":\"harness\","
                     << "\"session\":{\"id\":\"sess-mcp-roundtrip\",\"goal\":\"replace beta with gamma\"},"
                     << "\"edits\":[{\"op\":\"replace_text\",\"file\":" << JString(rel)
                     << ",\"find\":\"beta\\n\",\"text\":\"gamma\\n\",\"expected_sha256\":" << JString(sha) << "}]"
                     << "}";
    CommandResult unsupported_cmd = RunMcpRequest(h, root, BuildMcpToolCall(22, "preview", unsupported_args));
    Expect(h, unsupported_cmd.out.Find("UNSUPPORTED_OP") >= 0, "mcp-roundtrip: replace_text should be rejected");
    Expect(h, LoadFile(abs) == "alpha\nbeta\n", "mcp-roundtrip: unsupported op mutated file");

    CommandResult preview_cmd = RunMcpRequest(h, root, BuildMcpToolCall(2, "preview", preview_args));
    Value preview_rpc;
    String preview_body_json;
    if(!Expect(h, ParseMcpJsonResult(preview_cmd, preview_rpc, preview_body_json), "mcp-roundtrip: preview call failed: " + preview_cmd.out))
        return;
    Expect(h, preview_cmd.out.Find("\"structuredContent\"") >= 0, "mcp-roundtrip: preview structured content missing");
    Expect(h, preview_cmd.out.Find("\"isError\": false") >= 0 || preview_cmd.out.Find("\"isError\":false") >= 0,
           "mcp-roundtrip: preview reported MCP error");
    Expect(h, preview_cmd.out.Find("\"ok\": true") >= 0 || preview_cmd.out.Find("\"ok\":true") >= 0,
           "mcp-roundtrip: preview did not report ok");
    Expect(h, LoadFile(abs).Find("beta") >= 0, "mcp-roundtrip: preview mutated file");

    CommandResult apply_cmd = RunMcpRequest(h, root, BuildMcpToolCall(3, "apply", preview_args));
    Value apply_rpc;
    String apply_body_json;
    if(!Expect(h, ParseMcpJsonResult(apply_cmd, apply_rpc, apply_body_json), "mcp-roundtrip: apply call failed: " + apply_cmd.out))
        return;
    Expect(h, apply_cmd.out.Find("\"structuredContent\"") >= 0, "mcp-roundtrip: apply structured content missing");
    Expect(h, apply_cmd.out.Find("\"isError\": false") >= 0 || apply_cmd.out.Find("\"isError\":false") >= 0,
           "mcp-roundtrip: apply reported MCP error");
    Expect(h, apply_cmd.out.Find("\"ok\": true") >= 0 || apply_cmd.out.Find("\"ok\":true") >= 0,
           "mcp-roundtrip: apply did not report ok");
    Expect(h, LoadFile(abs).Find("gamma") >= 0, "mcp-roundtrip: apply did not update file");

    String transaction_id = (String)apply_rpc["result"]["structuredContent"]["transaction_id"];
    if(transaction_id.IsEmpty())
        transaction_id = ExtractJsonStringField(apply_cmd.out, "transaction_id");
    Expect(h, !transaction_id.IsEmpty(), "mcp-roundtrip: apply response missing transaction_id");

    String rollback_args;
    rollback_args << "{"
                  << "\"workspace_root\":" << JString(root) << ","
                  << "\"transaction_id\":" << JString(transaction_id) << ","
                  << "\"actor\":\"harness\""
                  << "}";
    CommandResult rollback_cmd = RunMcpRequest(h, root, BuildMcpToolCall(4, "rollback", rollback_args));
    Value rollback_rpc;
    String rollback_body_json;
    if(!Expect(h, ParseMcpJsonResult(rollback_cmd, rollback_rpc, rollback_body_json), "mcp-roundtrip: rollback call failed: " + rollback_cmd.out))
        return;
    Expect(h, rollback_cmd.out.Find("\"structuredContent\"") >= 0, "mcp-roundtrip: rollback structured content missing");
    Expect(h, rollback_cmd.out.Find("\"isError\": false") >= 0 || rollback_cmd.out.Find("\"isError\":false") >= 0,
           "mcp-roundtrip: rollback reported MCP error: " + rollback_cmd.out);
    Expect(h, rollback_cmd.out.Find("\"ok\": true") >= 0 || rollback_cmd.out.Find("\"ok\":true") >= 0,
           "mcp-roundtrip: rollback did not report ok: " + rollback_cmd.out);
    Expect(h, LoadFile(abs).Find("beta") >= 0, "mcp-roundtrip: rollback did not restore file: " + LoadFile(abs));

    String scan_args = String("{\"workspace_root\":") + JString(root) + "}";
    CommandResult scan_cmd = RunMcpRequest(h, root, BuildMcpToolCall(5, "recovery_scan", scan_args));
    Value scan_rpc;
    String scan_body_json;
    if(!Expect(h, ParseMcpJsonResult(scan_cmd, scan_rpc, scan_body_json), "mcp-roundtrip: recovery scan call failed: " + scan_cmd.out))
        return;
    Expect(h, scan_cmd.out.Find("\"structuredContent\"") >= 0, "mcp-roundtrip: recovery scan structured content missing");
    Expect(h, scan_cmd.out.Find("\"isError\": false") >= 0 || scan_cmd.out.Find("\"isError\":false") >= 0,
           "mcp-roundtrip: recovery scan reported MCP error");
    Expect(h, scan_cmd.out.Find("\"ok\": true") >= 0 || scan_cmd.out.Find("\"ok\":true") >= 0,
           "mcp-roundtrip: recovery scan did not report ok");
    Value scan_structured = scan_rpc["result"]["structuredContent"];
    Expect(h, scan_structured["scan"]["temporary_artifacts"].GetCount() == 0,
           "mcp-roundtrip: recovery scan should report no temporary artifacts in a clean workspace");
}

void TestTransportBenchmark(Harness& h)
{
    CaseLog case_log(h, "transport-benchmark", "Profiles direct core preview against CLI, MCP oneshot, and repeated MCP dispatch so overhead is visible in the console output.");
    String root = MakeCaseRoot(h, "transport-benchmark");
    String rel = "src/sample.txt";
    String abs = AppendFileName(root, rel);
    Expect(h, WriteFileText(abs, "alpha\nbeta\n"), "transport-benchmark: failed to write seed file");

    String sha = HashFileViaTool(h, abs);
    if(sha.IsEmpty())
        return;

    String req_json;
    req_json << "{\n"
             << "  \"workspace_root\": " << JString(root) << ",\n"
             << "  \"summary\": \"benchmark\",\n"
             << "  \"actor\": \"harness\",\n"
             << "  \"edits\": [\n"
             << "    {\"op\":\"replace_exact\",\"file\":" << JString(rel) << ",\"find\":\"beta\\n\",\"text\":\"gamma\\n\",\"expected_sha256\":" << JString(sha) << "}\n"
             << "  ]\n"
             << "}\n";
    String request_file = WriteRequestFile(root, "bench.json", req_json);

    Value req;
    String parse_error;
    if(!Expect(h, PatchtrackParseRequestJson(req_json, req, parse_error), "transport-benchmark: parse failed: " + parse_error))
        return;

    const int iterations = 25;
    String result;
    String error;

    TimeStop core_timer;
    for(int i = 0; i < iterations; i++) {
        result.Clear();
        error.Clear();
        if(!Expect(h, PatchtrackPreview(req, result, error), "transport-benchmark: direct core preview failed: " + error))
            return;
    }
    double core_ms = core_timer.Elapsed() / 1000.0 / iterations;

    TimeStop cli_timer;
    for(int i = 0; i < iterations; i++) {
        Value out;
        if(!ExpectJsonResult(h, RunJsonCommand(h, "preview", request_file), true, out, "transport-benchmark cli"))
            return;
    }
    double cli_ms = cli_timer.Elapsed() / 1000.0 / iterations;

    String mcp_args;
    mcp_args << "{"
             << "\"workspace_root\":" << JString(root) << ","
             << "\"summary\":\"benchmark\","
             << "\"actor\":\"harness\","
             << "\"edits\":[{\"op\":\"replace_exact\",\"file\":" << JString(rel)
             << ",\"find\":\"beta\\n\",\"text\":\"gamma\\n\",\"expected_sha256\":" << JString(sha) << "}]"
             << "}";
    String mcp_body = BuildMcpToolCall(99, "preview", mcp_args);
    TimeStop mcp_oneshot_timer;
    for(int i = 0; i < iterations; i++) {
        Value rpc;
        String body;
        if(!Expect(h, ParseMcpJsonResult(RunMcpRequest(h, root, mcp_body), rpc, body), "transport-benchmark: MCP preview failed"))
            return;
    }
    double mcp_oneshot_ms = mcp_oneshot_timer.Elapsed() / 1000.0 / iterations;

    String bench_file = WriteRequestFile(root, "mcp-bench.json", mcp_body);
    Vector<String> bench_args;
    bench_args.Add("--bench");
    bench_args.Add(bench_file);
    bench_args.Add(AsString(iterations));
    CommandResult bench_cmd = RunTool(h.patchtrack_mcp_exe, bench_args, h.repo_root);
    if(!Expect(h, bench_cmd.started && bench_cmd.exit_code == 0, "transport-benchmark: MCP dispatch bench failed: " + bench_cmd.out))
        return;
    double mcp_dispatch_ms = ExtractNamedMetricMs(bench_cmd.out, "avg_ms=");
    if(!Expect(h, mcp_dispatch_ms >= 0, "transport-benchmark: MCP dispatch bench did not report avg_ms: " + bench_cmd.out))
        return;

    Cout() << Format("     avg preview ms: core=%.3f cli=%.3f mcp_oneshot=%.3f mcp_dispatch=%.3f\n",
                     core_ms, cli_ms, mcp_oneshot_ms, mcp_dispatch_ms);
}
void TestJournalStateAndSnapshots(Harness& h)
{
    CaseLog case_log(h, "journal-state-snapshots", "Checks pending-before-commit behavior leaves snapshots and failed_restored records when commit never completes.");
    String root = MakeCaseRoot(h, "journal-state");
    String rel1 = "src/a.txt";
    String rel2 = "src/b.txt";
    String abs1 = AppendFileName(root, rel1);
    String abs2 = AppendFileName(root, rel2);
    String before1 = "alpha\none\n";
    String before2 = "beta\ntwo\n";
    Expect(h, WriteFileText(abs1, before1), "journal-state-snapshots: failed to write seed a");
    Expect(h, WriteFileText(abs2, before2), "journal-state-snapshots: failed to write seed b");

    String h1 = HashFileViaTool(h, abs1);
    String h2 = HashFileViaTool(h, abs2);
    if(h1.IsEmpty() || h2.IsEmpty())
        return;

    String req;
    req << "{\n"
        << "  \"workspace_root\": " << JString(root) << ",\n"
        << "  \"summary\": \"pending state check\",\n"
        << "  \"actor\": \"harness\",\n"
        << "  \"testing\": {\"inject_fault\": \"write_failed_before_first_write\"},\n"
        << "  \"edits\": [\n"
        << "    {\"op\":\"replace_exact\",\"file\":" << JString(rel1) << ",\"find\":\"one\\n\",\"text\":\"ONE\\n\",\"expected_sha256\":" << JString(h1) << "},\n"
        << "    {\"op\":\"replace_exact\",\"file\":" << JString(rel2) << ",\"find\":\"two\\n\",\"text\":\"TWO\\n\",\"expected_sha256\":" << JString(h2) << "}\n"
        << "  ]\n"
        << "}\n";

    Value out;
    if(!ExpectJsonResult(h, RunJsonCommand(h, "apply", WriteRequestFile(root, "pending.json", req)), false, out, "journal-state-snapshots apply"))
        return;
    ExpectErrorCode(h, out, "WRITE_FAILED", "journal-state-snapshots apply");
    Expect(h, LoadFile(abs1) == before1, "journal-state-snapshots: file a should remain original");
    Expect(h, LoadFile(abs2) == before2, "journal-state-snapshots: file b should remain original");

    String tran_file;
    Value tran;
    if(!ExpectSingleTransaction(h, root, "journal-state-snapshots", tran_file, tran))
        return;
    Expect(h, (String)tran["status"] == "failed_restored", "journal-state-snapshots: expected failed_restored status");
    Expect(h, ((String)tran["error"]).StartsWith("WRITE_FAILED"), "journal-state-snapshots: expected stored write failure");
    Value files = tran["files"];
    Expect(h, files.GetCount() == 2, "journal-state-snapshots: expected two changed files in transaction log");
    ExpectSnapshotsExist(h, tran_file, files, "journal-state-snapshots");
}

void TestInjectedWriteFailures(Harness& h)
{
    CaseLog case_log(h, "injected-write-failures", "Injects write-stage failures and verifies restore-on-failed-commit behavior plus recovery records.");

    struct Scenario {
        const char *name;
        const char *fault;
    };
    const Scenario scenarios[] = {
        { "after-first-write", "write_failed_after_first_write" },
        { "transaction-log", "write_failed_transaction_log" },
    };

    for(int s = 0; s < CountOf(scenarios); s++) {
        String root = MakeCaseRoot(h, String("fault-") + scenarios[s].name);
        String rel1 = "src/a.txt";
        String rel2 = "src/b.txt";
        String abs1 = AppendFileName(root, rel1);
        String abs2 = AppendFileName(root, rel2);
        String before1 = "alpha\none\n";
        String before2 = "beta\ntwo\n";

        Expect(h, WriteFileText(abs1, before1), "injected-write-failures: failed to write seed a for " + String(scenarios[s].name));
        Expect(h, WriteFileText(abs2, before2), "injected-write-failures: failed to write seed b for " + String(scenarios[s].name));
        String h1 = HashFileViaTool(h, abs1);
        String h2 = HashFileViaTool(h, abs2);
        if(h1.IsEmpty() || h2.IsEmpty())
            return;

        String req;
        req << "{\n"
            << "  \"workspace_root\": " << JString(root) << ",\n"
            << "  \"summary\": \"fault injection\",\n"
            << "  \"actor\": \"harness\",\n"
            << "  \"testing\": {\"inject_fault\": " << JString(scenarios[s].fault) << "},\n"
            << "  \"edits\": [\n"
            << "    {\"op\":\"replace_exact\",\"file\":" << JString(rel1) << ",\"find\":\"one\\n\",\"text\":\"ONE\\n\",\"expected_sha256\":" << JString(h1) << "},\n"
            << "    {\"op\":\"replace_exact\",\"file\":" << JString(rel2) << ",\"find\":\"two\\n\",\"text\":\"TWO\\n\",\"expected_sha256\":" << JString(h2) << "}\n"
            << "  ]\n"
            << "}\n";

        Value out;
        String label = String("injected-write-failures ") + scenarios[s].name;
        if(!ExpectJsonResult(h, RunJsonCommand(h, "apply", WriteRequestFile(root, "fault.json", req)), false, out, label))
            continue;
        ExpectErrorCode(h, out, "WRITE_FAILED", label);
        Expect(h, LoadFile(abs1) == before1, label + ": file a was not restored");
        Expect(h, LoadFile(abs2) == before2, label + ": file b was not restored");

        String tran_file;
        Value tran;
        if(!ExpectSingleTransaction(h, root, label, tran_file, tran))
            continue;
        Expect(h, (String)tran["status"] == "failed_restored", label + ": expected failed_restored status");
    }
}

void TestRollbackRecoveryFailure(Harness& h)
{
    CaseLog case_log(h, "rollback-recovery", "Injects rollback failure after the first restore and verifies the rollback itself is undone and journaled.");
    String root = MakeCaseRoot(h, "rollback-recovery");
    String rel1 = "src/a.txt";
    String rel2 = "src/b.txt";
    String abs1 = AppendFileName(root, rel1);
    String abs2 = AppendFileName(root, rel2);
    String before1 = "one\nalpha\n";
    String before2 = "two\nbeta\n";
    String after1 = "ONE\nalpha\n";
    String after2 = "TWO\nbeta\n";
    Expect(h, WriteFileText(abs1, before1), "rollback-recovery: failed to write seed a");
    Expect(h, WriteFileText(abs2, before2), "rollback-recovery: failed to write seed b");

    String h1 = HashFileViaTool(h, abs1);
    String h2 = HashFileViaTool(h, abs2);
    if(h1.IsEmpty() || h2.IsEmpty())
        return;

    String apply_req;
    apply_req << "{\n"
              << "  \"workspace_root\": " << JString(root) << ",\n"
              << "  \"summary\": \"rollback recovery seed\",\n"
              << "  \"actor\": \"harness\",\n"
              << "  \"edits\": [\n"
              << "    {\"op\":\"replace_exact\",\"file\":" << JString(rel1) << ",\"find\":\"one\\n\",\"text\":\"ONE\\n\",\"expected_sha256\":" << JString(h1) << "},\n"
              << "    {\"op\":\"replace_exact\",\"file\":" << JString(rel2) << ",\"find\":\"two\\n\",\"text\":\"TWO\\n\",\"expected_sha256\":" << JString(h2) << "}\n"
              << "  ]\n"
              << "}\n";
    Value apply_out;
    if(!ExpectJsonResult(h, RunJsonCommand(h, "apply", WriteRequestFile(root, "apply.json", apply_req)), true, apply_out, "rollback-recovery apply"))
        return;

    String rb;
    rb << "{\n"
       << "  \"workspace_root\": " << JString(root) << ",\n"
       << "  \"transaction_id\": " << JString((String)apply_out["transaction_id"]) << ",\n"
       << "  \"actor\": \"harness\",\n"
       << "  \"testing\": {\"inject_fault\": \"rollback_failed_after_first_write\"}\n"
       << "}\n";

    Value out;
    if(!ExpectJsonResult(h, RunJsonCommand(h, "rollback", WriteRequestFile(root, "rollback.json", rb)), false, out, "rollback-recovery rollback"))
        return;
    ExpectErrorCode(h, out, "WRITE_FAILED", "rollback-recovery rollback");
    Expect(h, LoadFile(abs1) == after1, "rollback-recovery: file a should be restored back to post-apply state");
    Expect(h, LoadFile(abs2) == after2, "rollback-recovery: file b should remain post-apply state");

    Vector<String> logs = ListTransactionFiles(root);
    Expect(h, logs.GetCount() == 2, "rollback-recovery: expected apply log and rollback log");
    bool found_rollback_record = false;
    for(int i = 0; i < logs.GetCount(); i++) {
        Value rollback_tran;
        if(!Expect(h, LoadJsonFileValue(logs[i], rollback_tran), "rollback-recovery: failed to load transaction log " + logs[i]))
            continue;
        if(IsNull(rollback_tran["rollback_of"]))
            continue;

        found_rollback_record = true;
        Expect(h, (String)rollback_tran["status"] == "failed_restored", "rollback-recovery: expected failed_restored rollback status");
    }
    Expect(h, found_rollback_record, "rollback-recovery: failed to locate rollback transaction record");
}

void TestLargeBatchStress(Harness& h)
{
    CaseLog case_log(h, "large-batch-1000-edits", "Applies 1000 edit operations in one transaction and verifies rollback restores the full file exactly.");
    String root = MakeCaseRoot(h, "large-batch");
    String rel = "src/stress.txt";
    String abs = AppendFileName(root, rel);
    const int kBulkEdits = 1000;

    String before;
    String after;
    for(int i = 0; i < kBulkEdits; i++) {
        before << Format("line%04d old\n", i);
        after << Format("line%04d new\n", i);
    }

    Expect(h, WriteFileText(abs, before), "large-batch-stress: failed to write seed file");
    String hash = HashFileViaTool(h, abs);
    if(hash.IsEmpty())
        return;

    String req;
    req << "{\n"
        << "  \"workspace_root\": " << JString(root) << ",\n"
        << "  \"summary\": \"large batch stress\",\n"
        << "  \"actor\": \"harness\",\n"
        << "  \"edits\": [\n";
    for(int i = 0; i < kBulkEdits; i++) {
        if(i)
            req << ",\n";
        req << "    {\"op\":\"replace_exact\",\"file\":" << JString(rel)
            << ",\"find\":" << JString(Format("line%04d old\n", i))
            << ",\"text\":" << JString(Format("line%04d new\n", i))
            << ",\"expected_sha256\":" << JString(hash) << "}";
    }
    req << "\n  ]\n"
        << "}\n";

    String request = WriteRequestFile(root, "stress.json", req);
    Value preview;
    if(!ExpectJsonResult(h, RunJsonCommand(h, "preview", request), true, preview, "large-batch-stress preview"))
        return;
    Expect(h, LoadFile(abs) == before, "large-batch-stress: preview mutated seed file");

    Value apply;
    if(!ExpectJsonResult(h, RunJsonCommand(h, "apply", request), true, apply, "large-batch-stress apply"))
        return;
    Expect(h, LoadFile(abs) == after, "large-batch-stress: apply output mismatch");

    String rb;
    rb << "{\n"
       << "  \"workspace_root\": " << JString(root) << ",\n"
       << "  \"transaction_id\": " << JString((String)apply["transaction_id"]) << ",\n"
       << "  \"actor\": \"harness\"\n"
       << "}\n";

    Value rollback;
    if(!ExpectJsonResult(h, RunJsonCommand(h, "rollback", WriteRequestFile(root, "stress-rollback.json", rb)), true, rollback, "large-batch-stress rollback"))
        return;
    Expect(h, LoadFile(abs) == before, "large-batch-stress: rollback did not restore original file");
}

void TestSequentialTransactionStress(Harness& h)
{
    CaseLog case_log(h, "sequential-1000-transactions", "Runs 1000 small transactions in sequence to shake out journaling, repeated allocation, and state drift issues.");
    String root = MakeCaseRoot(h, "sequential-stress");
    String rel = "src/seq.txt";
    String abs = AppendFileName(root, rel);
    Expect(h, WriteFileText(abs, "value0000\n"), "sequential-1000-transactions: failed to write seed file");

    const int kTransactions = 1000;
    for(int i = 0; i < kTransactions; i++) {
        String current = Format("value%04d\n", i);
        String next = Format("value%04d\n", i + 1);
        String hash = HashFileViaTool(h, abs);
        if(hash.IsEmpty())
            return;

        String req;
        req << "{\n"
            << "  \"workspace_root\": " << JString(root) << ",\n"
            << "  \"summary\": " << JString(Format("sequential transaction %d", i)) << ",\n"
            << "  \"actor\": \"harness\",\n"
            << "  \"edits\": [\n"
            << "    {\"op\":\"replace_exact\",\"file\":" << JString(rel)
            << ",\"find\":" << JString(current)
            << ",\"text\":" << JString(next)
            << ",\"expected_sha256\":" << JString(hash) << "}\n"
            << "  ]\n"
            << "}\n";

        Value apply;
        String label = String("sequential-1000-transactions apply ") + AsString(i);
        if(!ExpectJsonResult(h, RunJsonCommand(h, "apply", WriteRequestFile(root, Format("seq-%04d.json", i), req)), true, apply, label))
            return;
    }

    Expect(h, LoadFile(abs) == "value1000\n", "sequential-1000-transactions: final content mismatch");
    Expect(h, CountTransactionLogs(root) == kTransactions, "sequential-1000-transactions: unexpected transaction log count");
}
void TestOsFaultClassification(Harness& h)
{
    CaseLog case_log(h, "os-fault-classification", "Forces permission-denied and disk-full classes and checks stage, ownership, and resolution hints.");

    struct Scenario {
        const char *name;
        const char *fault;
        const char *expected_error;
        const char *expected_stage;
        const char *expected_owner;
    };
    const Scenario scenarios[] = {
        { "journal-dir", "permission_denied_create_journal_dir", "PERMISSION_DENIED_CREATE_DIR", "journal_dir", "workspace_owner_or_admin" },
        { "target-write", "permission_denied_write_target", "PERMISSION_DENIED_WRITE", "apply_write", "workspace_owner_or_admin" },
        { "disk-full", "no_space_target_write", "NO_SPACE_LEFT", "apply_write", "workspace_owner_or_admin" },
    };

    for(int i = 0; i < CountOf(scenarios); i++) {
        String root = MakeCaseRoot(h, String("os-fault-") + scenarios[i].name);
        String rel = "src/file.txt";
        String abs = AppendFileName(root, rel);
        String before = "alpha\nbeta\n";
        Expect(h, WriteFileText(abs, before), String("os-fault-classification: failed to seed ") + scenarios[i].name);
        String hash = HashFileViaTool(h, abs);
        if(hash.IsEmpty())
            return;

        String req;
        req << "{\n"
            << "  \"workspace_root\": " << JString(root) << ",\n"
            << "  \"summary\": \"os fault\",\n"
            << "  \"actor\": \"harness\",\n"
            << "  \"testing\": {\"inject_fault\": " << JString(scenarios[i].fault) << "},\n"
            << "  \"edits\": [\n"
            << "    {\"op\":\"replace_exact\",\"file\":" << JString(rel) << ",\"find\":\"beta\\n\",\"text\":\"gamma\\n\",\"expected_sha256\":" << JString(hash) << "}\n"
            << "  ]\n"
            << "}\n";

        Value out;
        String label = String("os-fault-classification ") + scenarios[i].name;
        if(!ExpectJsonResult(h, RunJsonCommand(h, "apply", WriteRequestFile(root, String(scenarios[i].name) + ".json", req)), false, out, label))
            continue;
        ExpectErrorCode(h, out, scenarios[i].expected_error, label);
        ExpectField(h, out, "stage", scenarios[i].expected_stage, label);
        ExpectField(h, out, "owner_hint", scenarios[i].expected_owner, label);
        Expect(h, !IsNull(out["resolution_hint"]) && ((String)out["resolution_hint"]).GetCount() > 10, label + ": expected resolution hint");
        Expect(h, LoadFile(abs) == before, label + ": file content should remain original");
    }
}

void TestHostCrashSimulation(Harness& h)
{
    CaseLog case_log(h, "host-crash-simulation", "Simulates abrupt process termination after the first write so pending journal state can be inspected manually.");
    String root = MakeCaseRoot(h, "host-crash");
    String rel1 = "src/a.txt";
    String rel2 = "src/b.txt";
    String abs1 = AppendFileName(root, rel1);
    String abs2 = AppendFileName(root, rel2);
    String before1 = "alpha\none\n";
    String before2 = "beta\ntwo\n";
    Expect(h, WriteFileText(abs1, before1), "host-crash-simulation: failed to write seed a");
    Expect(h, WriteFileText(abs2, before2), "host-crash-simulation: failed to write seed b");

    String h1 = HashFileViaTool(h, abs1);
    String h2 = HashFileViaTool(h, abs2);
    if(h1.IsEmpty() || h2.IsEmpty())
        return;

    String req;
    req << "{\n"
        << "  \"workspace_root\": " << JString(root) << ",\n"
        << "  \"summary\": \"host crash\",\n"
        << "  \"actor\": \"harness\",\n"
        << "  \"testing\": {\"inject_fault\": \"host_crash_after_first_write\"},\n"
        << "  \"edits\": [\n"
        << "    {\"op\":\"replace_exact\",\"file\":" << JString(rel1) << ",\"find\":\"one\\n\",\"text\":\"ONE\\n\",\"expected_sha256\":" << JString(h1) << "},\n"
        << "    {\"op\":\"replace_exact\",\"file\":" << JString(rel2) << ",\"find\":\"two\\n\",\"text\":\"TWO\\n\",\"expected_sha256\":" << JString(h2) << "}\n"
        << "  ]\n"
        << "}\n";

    CommandResult r = RunJsonCommand(h, "apply", WriteRequestFile(root, "host-crash.json", req));
    Expect(h, r.started, "host-crash-simulation: process failed to start");
    Expect(h, r.exit_code != 0, "host-crash-simulation: expected non-zero exit code");
    Value parsed;
    Expect(h, !ParseJsonOutput(r.out, parsed), "host-crash-simulation: abrupt crash should not return normal JSON");
    Expect(h, LoadFile(abs1) == "alpha\nONE\n", "host-crash-simulation: first file should remain mutated after crash");
    Expect(h, LoadFile(abs2) == before2, "host-crash-simulation: second file should remain original after crash");

    String tran_file;
    Value tran;
    if(!ExpectSingleTransaction(h, root, "host-crash-simulation", tran_file, tran))
        return;
    Expect(h, (String)tran["status"] == "pending", "host-crash-simulation: expected pending transaction record");
}

void TestClaimConflict(Harness& h)
{
    CaseLog case_log(h, "claim-conflict", "Creates an active claim from another session and verifies apply is blocked with structured session metadata.");
    String root = MakeCaseRoot(h, "claim-conflict");
    String rel = "src/file.txt";
    String abs = AppendFileName(root, rel);
    String before = "alpha\nbeta\n";
    Expect(h, WriteFileText(abs, before), "claim-conflict: failed to write seed file");

    Vector<String> claimed;
    claimed.Add(rel);
    Expect(h, WriteClaimFile(root, "sess-other", "code_edit", claimed, 2, 300, "other AI edit"), "claim-conflict: failed to write active claim");

    String hash = HashFileViaTool(h, abs);
    if(hash.IsEmpty())
        return;

    String req;
    req << "{\n"
        << "  \"workspace_root\": " << JString(root) << ",\n"
        << "  \"summary\": \"claim conflict\",\n"
        << "  \"actor\": \"harness\",\n"
        << "  \"edits\": [\n"
        << "    {\"op\":\"replace_exact\",\"file\":" << JString(rel) << ",\"find\":\"beta\\n\",\"text\":\"gamma\\n\",\"expected_sha256\":" << JString(hash) << "}\n"
        << "  ]\n"
        << "}\n";

    Value out;
    if(!ExpectJsonResult(h, RunJsonCommand(h, "apply", WriteRequestFile(root, "claim-conflict.json", req)), false, out, "claim-conflict apply"))
        return;
    ExpectErrorCode(h, out, "FILE_BUSY", "claim-conflict apply");
    ExpectField(h, out, "blocking_session_id", "sess-other", "claim-conflict apply");
    ExpectField(h, out, "blocking_intent", "code_edit", "claim-conflict apply");
    ExpectFieldContains(h, out, "blocking_summary", "other AI", "claim-conflict apply");
    Expect(h, LoadFile(abs) == before, "claim-conflict: blocked apply should not mutate file");
}

void TestStaleClaimTakeover(Harness& h)
{
    CaseLog case_log(h, "stale-claim-takeover", "Creates an expired claim and verifies apply clears it, reports it in startup_scan, and continues.");
    String root = MakeCaseRoot(h, "stale-claim");
    String rel = "src/file.txt";
    String abs = AppendFileName(root, rel);
    String before = "alpha\nbeta\n";
    Expect(h, WriteFileText(abs, before), "stale-claim-takeover: failed to write seed file");

    Vector<String> claimed;
    claimed.Add(rel);
    Expect(h, WriteClaimFile(root, "sess-stale", "code_edit", claimed, 300, -5, "stale AI edit"), "stale-claim-takeover: failed to write stale claim");

    String hash = HashFileViaTool(h, abs);
    if(hash.IsEmpty())
        return;

    String req;
    req << "{\n"
        << "  \"workspace_root\": " << JString(root) << ",\n"
        << "  \"summary\": \"stale claim takeover\",\n"
        << "  \"actor\": \"harness\",\n"
        << "  \"edits\": [\n"
        << "    {\"op\":\"replace_exact\",\"file\":" << JString(rel) << ",\"find\":\"beta\\n\",\"text\":\"gamma\\n\",\"expected_sha256\":" << JString(hash) << "}\n"
        << "  ]\n"
        << "}\n";

    Value out;
    if(!ExpectJsonResult(h, RunJsonCommand(h, "apply", WriteRequestFile(root, "stale-claim.json", req)), true, out, "stale-claim-takeover apply"))
        return;
    Expect(h, LoadFile(abs) == "alpha\ngamma\n", "stale-claim-takeover: apply did not update file");
    Value startup = out["startup_scan"];
    Expect(h, startup["stale_claims"].GetCount() == 1, "stale-claim-takeover: expected one stale claim in startup_scan");
    Expect(h, !FileExists(AppendFileName(ClaimsRoot(root), "sess-stale.json")), "stale-claim-takeover: stale claim file should be removed");

    Value scan_file;
    if(!LoadRecoveryScan(h, root, scan_file, "stale-claim-takeover"))
        return;
    Expect(h, scan_file["stale_claims"].GetCount() == 1, "stale-claim-takeover: recovery_scan.json should record the stale claim");
}

void TestStartupRecoveryScan(Harness& h)
{
    CaseLog case_log(h, "startup-recovery-scan", "Leaves a pending transaction behind, adds a stale claim, and verifies preview reports both in startup_scan.");
    String root = MakeCaseRoot(h, "startup-scan");
    String rel1 = "src/a.txt";
    String rel2 = "src/b.txt";
    String rel3 = "src/c.txt";
    String abs1 = AppendFileName(root, rel1);
    String abs2 = AppendFileName(root, rel2);
    String abs3 = AppendFileName(root, rel3);
    Expect(h, WriteFileText(abs1, "alpha\none\n"), "startup-recovery-scan: failed to write seed a");
    Expect(h, WriteFileText(abs2, "beta\ntwo\n"), "startup-recovery-scan: failed to write seed b");
    Expect(h, WriteFileText(abs3, "three\n"), "startup-recovery-scan: failed to write seed c");

    String h1 = HashFileViaTool(h, abs1);
    String h2 = HashFileViaTool(h, abs2);
    String h3 = HashFileViaTool(h, abs3);
    if(h1.IsEmpty() || h2.IsEmpty() || h3.IsEmpty())
        return;

    String crash_req;
    crash_req << "{\n"
              << "  \"workspace_root\": " << JString(root) << ",\n"
              << "  \"summary\": \"pending crash\",\n"
              << "  \"actor\": \"harness\",\n"
              << "  \"testing\": {\"inject_fault\": \"host_crash_after_first_write\"},\n"
              << "  \"edits\": [\n"
              << "    {\"op\":\"replace_exact\",\"file\":" << JString(rel1) << ",\"find\":\"one\\n\",\"text\":\"ONE\\n\",\"expected_sha256\":" << JString(h1) << "},\n"
              << "    {\"op\":\"replace_exact\",\"file\":" << JString(rel2) << ",\"find\":\"two\\n\",\"text\":\"TWO\\n\",\"expected_sha256\":" << JString(h2) << "}\n"
              << "  ]\n"
              << "}\n";
    CommandResult crash = RunJsonCommand(h, "apply", WriteRequestFile(root, "crash.json", crash_req));
    Expect(h, crash.started, "startup-recovery-scan: crash apply did not start");
    Expect(h, crash.exit_code != 0, "startup-recovery-scan: crash apply should exit non-zero");

    Vector<String> stale_files;
    stale_files.Add(rel3);
    Expect(h, WriteClaimFile(root, "sess-stale-scan", "code_edit", stale_files, 300, -5, "stale scan claim"), "startup-recovery-scan: failed to write stale claim");

    String preview_req;
    preview_req << "{\n"
                << "  \"workspace_root\": " << JString(root) << ",\n"
                << "  \"summary\": \"scan preview\",\n"
                << "  \"actor\": \"harness\",\n"
                << "  \"edits\": [\n"
                << "    {\"op\":\"replace_exact\",\"file\":" << JString(rel3) << ",\"find\":\"three\\n\",\"text\":\"THREE\\n\",\"expected_sha256\":" << JString(h3) << "}\n"
                << "  ]\n"
                << "}\n";

    Value preview;
    if(!ExpectJsonResult(h, RunJsonCommand(h, "preview", WriteRequestFile(root, "preview-scan.json", preview_req)), true, preview, "startup-recovery-scan preview"))
        return;
    Value startup = preview["startup_scan"];
    Expect(h, startup["pending_transactions"].GetCount() == 1, "startup-recovery-scan: expected one pending transaction in startup_scan");
    Expect(h, startup["stale_claims"].GetCount() == 1, "startup-recovery-scan: expected one stale claim in startup_scan");
    Expect(h, !FileExists(AppendFileName(ClaimsRoot(root), "sess-stale-scan.json")), "startup-recovery-scan: stale claim file should be removed by scan");
}
String Usage()
{
    return
        "patchtrack_tests - end-to-end protocol harness\n\n"
        "Usage:\n"
        "  patchtrack_tests [repo-root] [patchtrack-exe]\n\n"
        "Defaults:\n"
        "  repo-root      current working directory\n"
        "  patchtrack-exe <repo-root>/build/patchtrack.exe\n";
}

int RunAll(Harness& h)
{
    Expect(h, FileExists(h.patchtrack_exe), "patchtrack executable not found: " + h.patchtrack_exe);
    Expect(h, FileExists(h.patchtrack_mcp_exe), "patchtrack_mcp executable not found: " + h.patchtrack_mcp_exe);
    if(!h.failures.IsEmpty())
        return 1;

    TestSelfTestSmoke(h);
    TestMcpSelfTestSmoke(h);
    TestMcpRoundTrip(h);
    TestCommandSurface(h);
    TestPreviewNoWrite(h);
    TestBatchPrimitivesAndRollback(h);
    TestAliasesAndNoOpPreview(h);
    TestEngineFailures(h);
    TestCreateAndDriftDiagnostics(h);
    TestTransportFailures(h);
    TestRollbackFailures(h);
    TestJournalStateAndSnapshots(h);
    TestInjectedWriteFailures(h);
    TestOsFaultClassification(h);
    TestClaimConflict(h);
    TestStaleClaimTakeover(h);
    TestHostCrashSimulation(h);
    TestStartupRecoveryScan(h);
    TestRollbackRecoveryFailure(h);
    TestLargeBatchStress(h);
    TestSequentialTransactionStress(h);
    TestTransportBenchmark(h);

    Cout() << "\nSummary\n";
    Cout() << "  Passed: " << h.passed_count << "\n";
    Cout() << "  Failed: " << (h.test_count - h.passed_count) << "\n";
    Cout() << "  Total:  " << h.test_count << "\n";

    if(h.failures.IsEmpty()) {
        Cout() << "protocol-tests: ok\n";
        return 0;
    }

    Cout() << "protocol-tests: failed\n";
    return 1;
}

} // namespace

CONSOLE_APP_MAIN
{
    const Vector<String>& cmd = CommandLine();
    if(cmd.GetCount() > 2) {
        Cout() << Usage();
        SetExitCode(2);
        return;
    }

    Harness h;
    h.repo_root = cmd.GetCount() >= 1 ? cmd[0] : GetCurrentDirectory();
    h.patchtrack_exe = cmd.GetCount() >= 2 ? cmd[1] : AppendFileName(AppendFileName(h.repo_root, "build"), "patchtrack.exe");
    h.patchtrack_mcp_exe = AppendFileName(AppendFileName(h.repo_root, "build"), "patchtrack_mcp.exe");

    SetExitCode(RunAll(h));
}
































