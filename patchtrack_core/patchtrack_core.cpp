/*
PatchTrack core engine.

This file contains the journaled patch engine, CLI entry points, and built-in selftest.
The transactional areas intentionally prefer explicit state recording over implicit success
so that hosts and future MCP adapters can distinguish pending, applied, restored, and
manual-recovery-required outcomes.

Change log:
- 2026-04-27: Added pending transaction records, failed_restored /
  failed_recovery_required journal states, rollback recovery, stronger selftests, and
  process-safe ID generation for repeated short-lived CLI invocations.
*/

#include <Core/Core.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <patchtrack_core/patchtrack_core.h>
#include "platform_fs.h"

using namespace Upp;

namespace {

struct Sha256Ctx {
    uint32_t state[8];
    uint64_t bitlen = 0;
    byte data[64];
    int datalen = 0;
};

static const uint32_t K256[64] = {
    0x428a2f98ul, 0x71374491ul, 0xb5c0fbcful, 0xe9b5dba5ul,
    0x3956c25bul, 0x59f111f1ul, 0x923f82a4ul, 0xab1c5ed5ul,
    0xd807aa98ul, 0x12835b01ul, 0x243185beul, 0x550c7dc3ul,
    0x72be5d74ul, 0x80deb1feul, 0x9bdc06a7ul, 0xc19bf174ul,
    0xe49b69c1ul, 0xefbe4786ul, 0x0fc19dc6ul, 0x240ca1ccul,
    0x2de92c6ful, 0x4a7484aaul, 0x5cb0a9dcul, 0x76f988daul,
    0x983e5152ul, 0xa831c66dul, 0xb00327c8ul, 0xbf597fc7ul,
    0xc6e00bf3ul, 0xd5a79147ul, 0x06ca6351ul, 0x14292967ul,
    0x27b70a85ul, 0x2e1b2138ul, 0x4d2c6dfcul, 0x53380d13ul,
    0x650a7354ul, 0x766a0abbul, 0x81c2c92eul, 0x92722c85ul,
    0xa2bfe8a1ul, 0xa81a664bul, 0xc24b8b70ul, 0xc76c51a3ul,
    0xd192e819ul, 0xd6990624ul, 0xf40e3585ul, 0x106aa070ul,
    0x19a4c116ul, 0x1e376c08ul, 0x2748774cul, 0x34b0bcb5ul,
    0x391c0cb3ul, 0x4ed8aa4aul, 0x5b9cca4ful, 0x682e6ff3ul,
    0x748f82eeul, 0x78a5636ful, 0x84c87814ul, 0x8cc70208ul,
    0x90befffaul, 0xa4506cebul, 0xbef9a3f7ul, 0xc67178f2ul
};

inline uint32_t Ror(uint32_t x, uint32_t n)
{
    return (x >> n) | (x << (32 - n));
}

inline uint32_t Ch(uint32_t x, uint32_t y, uint32_t z)
{
    return (x & y) ^ (~x & z);
}

inline uint32_t Maj(uint32_t x, uint32_t y, uint32_t z)
{
    return (x & y) ^ (x & z) ^ (y & z);
}

inline uint32_t Ep0(uint32_t x)
{
    return Ror(x, 2) ^ Ror(x, 13) ^ Ror(x, 22);
}

inline uint32_t Ep1(uint32_t x)
{
    return Ror(x, 6) ^ Ror(x, 11) ^ Ror(x, 25);
}

inline uint32_t Sig0(uint32_t x)
{
    return Ror(x, 7) ^ Ror(x, 18) ^ (x >> 3);
}

inline uint32_t Sig1(uint32_t x)
{
    return Ror(x, 17) ^ Ror(x, 19) ^ (x >> 10);
}

void Sha256Transform(Sha256Ctx& ctx, const byte data[])
{
    uint32_t m[64];

    for(int i = 0, j = 0; i < 16; i++, j += 4) {
        m[i] = ((uint32_t)data[j] << 24) |
               ((uint32_t)data[j + 1] << 16) |
               ((uint32_t)data[j + 2] << 8) |
               ((uint32_t)data[j + 3]);
    }

    for(int i = 16; i < 64; i++)
        m[i] = Sig1(m[i - 2]) + m[i - 7] + Sig0(m[i - 15]) + m[i - 16];

    uint32_t a = ctx.state[0];
    uint32_t b = ctx.state[1];
    uint32_t c = ctx.state[2];
    uint32_t d = ctx.state[3];
    uint32_t e = ctx.state[4];
    uint32_t f = ctx.state[5];
    uint32_t g = ctx.state[6];
    uint32_t h = ctx.state[7];

    for(int i = 0; i < 64; i++) {
        uint32_t t1 = h + Ep1(e) + Ch(e, f, g) + K256[i] + m[i];
        uint32_t t2 = Ep0(a) + Maj(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    ctx.state[0] += a;
    ctx.state[1] += b;
    ctx.state[2] += c;
    ctx.state[3] += d;
    ctx.state[4] += e;
    ctx.state[5] += f;
    ctx.state[6] += g;
    ctx.state[7] += h;
}

void Sha256Init(Sha256Ctx& ctx)
{
    ctx.datalen = 0;
    ctx.bitlen = 0;
    ctx.state[0] = 0x6a09e667ul;
    ctx.state[1] = 0xbb67ae85ul;
    ctx.state[2] = 0x3c6ef372ul;
    ctx.state[3] = 0xa54ff53aul;
    ctx.state[4] = 0x510e527ful;
    ctx.state[5] = 0x9b05688cul;
    ctx.state[6] = 0x1f83d9abul;
    ctx.state[7] = 0x5be0cd19ul;
}

void Sha256Update(Sha256Ctx& ctx, const byte* data, int len)
{
    for(int i = 0; i < len; i++) {
        ctx.data[ctx.datalen++] = data[i];
        if(ctx.datalen == 64) {
            Sha256Transform(ctx, ctx.data);
            ctx.bitlen += 512;
            ctx.datalen = 0;
        }
    }
}

String HexByte(byte b)
{
    static const char* h = "0123456789abcdef";
    String out;
    out.Cat(h[(b >> 4) & 15]);
    out.Cat(h[b & 15]);
    return out;
}

String Sha256Final(Sha256Ctx& ctx)
{
    byte hash[32];
    int i = ctx.datalen;

    if(ctx.datalen < 56) {
        ctx.data[i++] = 0x80;
        while(i < 56)
            ctx.data[i++] = 0x00;
    }
    else {
        ctx.data[i++] = 0x80;
        while(i < 64)
            ctx.data[i++] = 0x00;
        Sha256Transform(ctx, ctx.data);
        memset(ctx.data, 0, 56);
    }

    ctx.bitlen += (uint64_t)ctx.datalen * 8;
    ctx.data[63] = (byte)(ctx.bitlen);
    ctx.data[62] = (byte)(ctx.bitlen >> 8);
    ctx.data[61] = (byte)(ctx.bitlen >> 16);
    ctx.data[60] = (byte)(ctx.bitlen >> 24);
    ctx.data[59] = (byte)(ctx.bitlen >> 32);
    ctx.data[58] = (byte)(ctx.bitlen >> 40);
    ctx.data[57] = (byte)(ctx.bitlen >> 48);
    ctx.data[56] = (byte)(ctx.bitlen >> 56);
    Sha256Transform(ctx, ctx.data);

    for(i = 0; i < 4; i++) {
        hash[i]      = (byte)((ctx.state[0] >> (24 - i * 8)) & 0xff);
        hash[i + 4]  = (byte)((ctx.state[1] >> (24 - i * 8)) & 0xff);
        hash[i + 8]  = (byte)((ctx.state[2] >> (24 - i * 8)) & 0xff);
        hash[i + 12] = (byte)((ctx.state[3] >> (24 - i * 8)) & 0xff);
        hash[i + 16] = (byte)((ctx.state[4] >> (24 - i * 8)) & 0xff);
        hash[i + 20] = (byte)((ctx.state[5] >> (24 - i * 8)) & 0xff);
        hash[i + 24] = (byte)((ctx.state[6] >> (24 - i * 8)) & 0xff);
        hash[i + 28] = (byte)((ctx.state[7] >> (24 - i * 8)) & 0xff);
    }

    String out;
    for(i = 0; i < 32; i++)
        out << HexByte(hash[i]);
    return out;
}

String Sha256String(const String& s)
{
    Sha256Ctx ctx;
    Sha256Init(ctx);
    Sha256Update(ctx, (const byte*)~s, s.GetLength());
    return Sha256Final(ctx);
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

String JBool(bool b)
{
    return b ? "true" : "false";
}

String NormalizeLf(String s)
{
    String out;
    for(int i = 0; i < s.GetLength(); i++) {
        if(s[i] == '\r') {
            if(i + 1 < s.GetLength() && s[i + 1] == '\n')
                i++;
            out.Cat('\n');
        }
        else
            out.Cat(s[i]);
    }
    return out;
}

String RestoreNewlines(const String& s, const String& nl)
{
    if(nl == "\n")
        return s;

    String out;
    for(int i = 0; i < s.GetLength(); i++) {
        if(s[i] == '\n')
            out << nl;
        else
            out.Cat(s[i]);
    }
    return out;
}

bool StartsWith2(const String& s, const char* p)
{
    int n = (int)strlen(p);
    return s.GetLength() >= n && memcmp(~s, p, n) == 0;
}

bool EndsWithChar(const String& s, char c)
{
    return s.GetLength() && s[s.GetLength() - 1] == c;
}

int CountOccurrences(const String& s, const String& needle)
{
    if(needle.IsEmpty())
        return 0;

    int count = 0;
    int pos = 0;
    for(;;) {
        int p = s.Find(needle, pos);
        if(p < 0)
            break;
        count++;
        pos = p + needle.GetLength();
    }
    return count;
}

String ReplaceFirstAt(const String& s, int pos, int len, const String& replacement)
{
    return s.Left(pos) + replacement + s.Mid(pos + len);
}

String ReplaceAllExact(const String& s, const String& find, const String& replacement)
{
    if(find.IsEmpty())
        return s;

    String out;
    int pos = 0;
    for(;;) {
        int p = s.Find(find, pos);
        if(p < 0) {
            out << s.Mid(pos);
            break;
        }
        out << s.Mid(pos, p - pos);
        out << replacement;
        pos = p + find.GetLength();
    }
    return out;
}

Vector<String> SplitLinesKeepEmpty(const String& s)
{
    Vector<String> out;
    int start = 0;
    for(int i = 0; i < s.GetLength(); i++) {
        if(s[i] == '\n') {
            out.Add(s.Mid(start, i - start));
            start = i + 1;
        }
    }
    if(start < s.GetLength() || !EndsWithChar(s, '\n'))
        out.Add(s.Mid(start));
    return out;
}

String JoinLines(const Vector<String>& lines)
{
    String out;
    for(int i = 0; i < lines.GetCount(); i++) {
        if(i)
            out.Cat('\n');
        out << lines[i];
    }
    return out;
}

String MakeUnifiedDiff(const String& path, const String& before, const String& after)
{
    Vector<String> a = SplitLinesKeepEmpty(before);
    Vector<String> b = SplitLinesKeepEmpty(after);

    String out;
    out << "--- a/" << path << "\n";
    out << "+++ b/" << path << "\n";

    int maxn = max(a.GetCount(), b.GetCount());
    for(int i = 0; i < maxn; i++) {
        String av = i < a.GetCount() ? a[i] : String();
        String bv = i < b.GetCount() ? b[i] : String();
        if(i < a.GetCount() && i < b.GetCount() && av == bv)
            continue;

        out << "@@ line " << (i + 1) << " @@\n";
        if(i < a.GetCount())
            out << "-" << av << "\n";
        if(i < b.GetCount())
            out << "+" << bv << "\n";
    }

    if(out.Find("@@") < 0)
        out << " no changes\n";
    return out;
}

String Slug(String s, int max_len = 16)
{
    s = ToLower(s);
    String out;
    bool dash = false;

    for(int i = 0; i < s.GetLength() && out.GetLength() < max_len; i++) {
        int c = (byte)s[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if(ok) {
            out.Cat(c);
            dash = false;
        }
        else {
            if(!dash && out.GetLength() > 0) {
                out.Cat('-');
                dash = true;
            }
        }
    }

    while(out.GetLength() && out[out.GetLength() - 1] == '-')
        out.Trim(out.GetLength() - 1);

    return out;
}

String Id10()
{
    static int seq = 0;

    const char* alphabet = "abcdefghijklmnopqrstuvwxyz0123456789";
    String seed;
    seed << AsString(GetSysTime())
         << "|pid=" << Format("%d", (int)GetCurrentProcessId())
         << "|clock=" << Format("%d", (int)clock())
         << "|seq=" << AsString(++seq);

    String h = Sha256String(seed);
    String out;
    for(int i = 0; i < 10; i++) {
        int hi = h[i * 2];
        int lo = h[i * 2 + 1];
        int v = ((hi >= 'a' ? hi - 'a' + 10 : hi - '0') << 4) |
                (lo >= 'a' ? lo - 'a' + 10 : lo - '0');
        out.Cat(alphabet[v % 36]);
    }
    return out;
}

String Now()
{
    return AsString(GetSysTime());
}

bool IsAbsoluteOrUnsafePath(const String& rel)
{
    if(rel.IsEmpty())
        return true;
    if(rel[0] == '/' || rel[0] == '\\')
        return true;
    if(rel.GetLength() >= 2 && rel[1] == ':')
        return true;
    if(rel.Find("..") >= 0)
        return true;
    return false;
}

String JoinPath(const String& root, const String& rel)
{
    return AppendFileName(root, rel);
}

String JournalRoot(const String& workspace)
{
    return AppendFileName(workspace, ".patchtrack");
}


bool IsPermissionDeniedCode(PlatformErrorCode code)
{
    return PlatformIsPermissionDenied(code);
}

bool IsDiskFullCode(PlatformErrorCode code)
{
    return PlatformIsDiskFull(code);
}

String EncodeStructuredError(const String& code,
                             const String& stage,
                             const String& path,
                             const String& operation,
                             const String& owner_hint,
                             const String& resolution_hint,
                             const String& why,
                             PlatformErrorCode os_code = 0)
{
    String out = code;
    if(!stage.IsEmpty()) out << "||stage=" << stage;
    if(!path.IsEmpty()) out << "||path=" << path;
    if(!operation.IsEmpty()) out << "||operation=" << operation;
    if(!owner_hint.IsEmpty()) out << "||owner_hint=" << owner_hint;
    if(!resolution_hint.IsEmpty()) out << "||resolution_hint=" << resolution_hint;
    if(!why.IsEmpty()) out << "||why=" << why;
    if(os_code)
        out << "||os_code=" << AsString((int)os_code);
    return out;
}

String ErrorCodeOnly(const String& error)
{
    int p = error.Find("||");
    if(p >= 0)
        return error.Left(p);
    p = error.Find(": ");
    if(p >= 0)
        return error.Left(p);
    return error;
}

String ErrorField(const String& error, const String& key)
{
    String token = key + "=";
    int start = 0;
    for(;;) {
        int p = error.Find("||", start);
        String part = p < 0 ? error.Mid(start) : error.Mid(start, p - start);
        if(StartsWith2(part, ~token))
            return part.Mid(token.GetLength());
        if(p < 0)
            break;
        start = p + 2;
    }
    return String();
}

void AppendStructuredErrorFields(String& out, const String& error)
{
    String stage = ErrorField(error, "stage");
    String path = ErrorField(error, "path");
    String operation = ErrorField(error, "operation");
    String owner_hint = ErrorField(error, "owner_hint");
    String resolution_hint = ErrorField(error, "resolution_hint");
    String why = ErrorField(error, "why");
    String os_code = ErrorField(error, "os_code");
    String blocking_session_id = ErrorField(error, "blocking_session_id");
    String blocking_intent = ErrorField(error, "blocking_intent");
    String blocking_summary = ErrorField(error, "blocking_summary");
    String heartbeat_age_sec = ErrorField(error, "heartbeat_age_sec");

    if(!why.IsEmpty())
        out << ",\n  \"message\": " << JString(why);
    if(!stage.IsEmpty())
        out << ",\n  \"stage\": " << JString(stage);
    if(!path.IsEmpty())
        out << ",\n  \"path\": " << JString(path);
    if(!operation.IsEmpty())
        out << ",\n  \"operation\": " << JString(operation);
    if(!owner_hint.IsEmpty())
        out << ",\n  \"owner_hint\": " << JString(owner_hint);
    if(!resolution_hint.IsEmpty())
        out << ",\n  \"resolution_hint\": " << JString(resolution_hint);
    if(!os_code.IsEmpty())
        out << ",\n  \"os_code\": " << JString(os_code);
    if(!blocking_session_id.IsEmpty())
        out << ",\n  \"blocking_session_id\": " << JString(blocking_session_id);
    if(!blocking_intent.IsEmpty())
        out << ",\n  \"blocking_intent\": " << JString(blocking_intent);
    if(!blocking_summary.IsEmpty())
        out << ",\n  \"blocking_summary\": " << JString(blocking_summary);
    if(!heartbeat_age_sec.IsEmpty())
        out << ",\n  \"heartbeat_age_sec\": " << JString(heartbeat_age_sec);
}
String BuildErrorJson(const String& error)
{
    String out;
    out << "{\n  \"ok\": false,\n  \"error\": " << JString(ErrorCodeOnly(error));
    AppendStructuredErrorFields(out, error);
    out << "\n}\n";
    return out;
}

String BuildPermissionOwnerHint(const String& stage, const String& path, const String& operation)
{
    if(operation == "read_request")
        return "caller_can_fix";
    if(path.Find(".patchtrack") >= 0 || stage.Find("journal") >= 0 || stage.Find("transaction_log") >= 0)
        return "workspace_owner_or_admin";
    if(stage.Find("request") >= 0)
        return "caller_can_fix";
    return "workspace_owner_or_admin";
}

String BuildResolutionHint(const String& code, const String& stage, const String& path, const String& operation)
{
    if(code == "PERMISSION_DENIED_READ")
        return operation == "read_request"
            ? "Grant read access to the request file or regenerate it in a readable location."
            : "Grant read access to the target path or run PatchTrack as a user that can read the workspace file.";
    if(code == "PERMISSION_DENIED_WRITE")
        return "Grant write access to the target file or run PatchTrack with a user that can modify workspace files.";
    if(code == "PERMISSION_DENIED_CREATE_DIR")
        return "Grant create-directory permission for the workspace journal path or choose a writable workspace root.";
    if(code == "PERMISSION_DENIED_TRANSACTION_LOG")
        return "Grant write access to the .patchtrack journal directory so PatchTrack can persist transaction state and recovery records.";
    if(code == "PERMISSION_DENIED_ROLLBACK")
        return "Grant write access to the target files and .patchtrack journal so rollback can restore content and record recovery state.";
    if(code == "NO_SPACE_LEFT")
        return "Free disk space on the workspace volume, then retry the operation after confirming files were restored or manually repaired.";
    if(code == "HOST_TERMINATED_DURING_APPLY")
        return "Inspect the pending transaction record and changed files, then run rollback or repair the workspace manually before retrying.";
    if(code == "HOST_TERMINATED_DURING_ROLLBACK")
        return "Inspect rollback journal state and affected files, then complete recovery manually before issuing another rollback.";
    return "Inspect the reported path and OS error, then retry after fixing the filesystem or host environment restriction.";
}

String BuildFsErrorForCode(PlatformErrorCode os_code, const String& stage, const String& path, const String& operation)
{
    String code;
    String why;
    if(IsPermissionDeniedCode(os_code)) {
        if(operation == "create_dir")
            code = "PERMISSION_DENIED_CREATE_DIR";
        else if(stage.Find("rollback") >= 0)
            code = "PERMISSION_DENIED_ROLLBACK";
        else if(operation == "write_transaction_log")
            code = "PERMISSION_DENIED_TRANSACTION_LOG";
        else if(operation == "read_file" || operation == "read_request")
            code = "PERMISSION_DENIED_READ";
        else
            code = "PERMISSION_DENIED_WRITE";
        why = "The OS denied access for this filesystem operation.";
    }
    else if(IsDiskFullCode(os_code)) {
        code = "NO_SPACE_LEFT";
        why = "The workspace volume does not have enough free space to complete the operation.";
    }
    else if(operation == "read_file" || operation == "read_request") {
        code = "READ_FAILED";
        why = "The filesystem read operation failed.";
    }
    else {
        code = "WRITE_FAILED";
        why = "The filesystem write operation failed.";
    }

    return EncodeStructuredError(code,
                                 stage,
                                 path,
                                 operation,
                                 BuildPermissionOwnerHint(stage, path, operation),
                                 BuildResolutionHint(code, stage, path, operation),
                                 why,
                                 os_code);
}

bool ShouldInjectFault(const String& inject_fault, const char *fault_name)
{
    return inject_fault == fault_name;
}

bool InjectFailure(const String& inject_fault,
                   const char *fault_name,
                   const String& code,
                   const String& stage,
                   const String& path,
                   const String& operation,
                   const String& why,
                   String& error)
{
    if(!ShouldInjectFault(inject_fault, fault_name))
        return false;
    error = EncodeStructuredError(code,
                                  stage,
                                  path,
                                  operation,
                                  BuildPermissionOwnerHint(stage, path, operation),
                                  BuildResolutionHint(code, stage, path, operation),
                                  why,
                                  0);
    return true;
}

bool EnsureDirectoryDetailed(const String& path,
                             const String& stage,
                             const String& operation,
                             const String& inject_fault,
                             const char *fault_name,
                             String& error)
{
    if(path.IsEmpty())
        return true;

    bool is_directory = false;
    PlatformErrorCode code = 0;
    if(PlatformIsPathDirectory(path, is_directory, code))
        return is_directory;

    String parent = GetFileFolder(path);
    if(!parent.IsEmpty() && parent != path) {
        bool parent_is_directory = false;
        PlatformErrorCode parent_code = 0;
        if(!PlatformIsPathDirectory(parent, parent_is_directory, parent_code)) {
            if(!EnsureDirectoryDetailed(parent, stage, operation, inject_fault, fault_name, error))
                return false;
        }
    }

    if(InjectFailure(inject_fault, fault_name, "PERMISSION_DENIED_CREATE_DIR", stage, path, operation,
                     "The test fault denied directory creation.", error))
        return false;
    if(InjectFailure(inject_fault, "no_space_create_dir", "NO_SPACE_LEFT", stage, path, operation,
                     "The test fault simulated a full disk during directory creation.", error))
        return false;

    if(PlatformCreateDirectory(path, code))
        return true;

    if(PlatformIsPathDirectory(path, is_directory, code))
        return is_directory;

    error = BuildFsErrorForCode(code, stage, path, operation);
    return false;
}

bool ReadFileDetailed(const String& path,
                      String& out,
                      const String& stage,
                      const String& operation,
                      const String& inject_fault,
                      const char *fault_name,
                      String& error)
{
    if(InjectFailure(inject_fault, fault_name, "PERMISSION_DENIED_READ", stage, path, operation,
                     "The test fault denied file read access.", error))
        return false;

    PlatformErrorCode code = 0;
    bool not_found = false;
    if(!PlatformReadFileRaw(path, out, code, not_found)) {
        if(not_found) {
            error = "FILE_NOT_FOUND: " + path;
            return false;
        }
        error = BuildFsErrorForCode(code, stage, path, operation);
        return false;
    }
    return true;
}

bool WriteFileDetailed(const String& path,
                       const String& data,
                       const String& stage,
                       const String& operation,
                       const String& inject_fault,
                       const char *fault_name,
                       String& error)
{
    String dir = GetFileFolder(path);
    if(!dir.IsEmpty()) {
        if(!EnsureDirectoryDetailed(dir, stage, "create_dir", inject_fault, fault_name, error))
            return false;
    }

    if(InjectFailure(inject_fault, fault_name, operation == "write_transaction_log" ? "PERMISSION_DENIED_TRANSACTION_LOG" :
                                              (stage.Find("rollback") >= 0 ? "PERMISSION_DENIED_ROLLBACK" : "PERMISSION_DENIED_WRITE"),
                     stage, path, operation,
                     "The test fault denied file write access.", error))
        return false;
    if(stage == "apply_write"
    && InjectFailure(inject_fault, "no_space_target_write", "NO_SPACE_LEFT", stage, path, operation,
                     "The test fault simulated a full disk during file write.", error))
        return false;
    if(operation == "write_transaction_log"
    && InjectFailure(inject_fault, "no_space_transaction_log", "NO_SPACE_LEFT", stage, path, operation,
                     "The test fault simulated a full disk while writing the transaction log.", error))
        return false;
    if(stage == "snapshot_before"
    && InjectFailure(inject_fault, "no_space_snapshot_write", "NO_SPACE_LEFT", stage, path, operation,
                     "The test fault simulated a full disk while writing a snapshot.", error))
        return false;

    PlatformErrorCode code = 0;
    if(!PlatformWriteFileRaw(path, data, code)) {
        error = BuildFsErrorForCode(code, stage, path, operation);
        return false;
    }
    return true;
}
struct TextFile : Moveable<TextFile> {
    String path;
    String rel;
    String raw;
    String text;
    String newline = "\n";
    bool bom = false;
    bool eof_newline = false;
    String sha256;
};

bool ReadTextFile(const String& path,
                  const String& rel,
                  TextFile& out,
                  String& error,
                  const String& inject_fault = String(),
                  const char *fault_name = "permission_denied_read_target",
                  const String& stage = "read_target")
{
    String raw;
    if(!ReadFileDetailed(path, raw, stage, "read_file", inject_fault, fault_name, error))
        return false;

    out.path = path;
    out.rel = rel;
    out.raw = raw;
    out.sha256 = Sha256String(raw);
    out.bom = raw.GetLength() >= 3 &&
              (byte)raw[0] == 0xef &&
              (byte)raw[1] == 0xbb &&
              (byte)raw[2] == 0xbf;

    String body = out.bom ? raw.Mid(3) : raw;
    out.newline = body.Find("\r\n") >= 0 ? "\r\n" : "\n";
    out.eof_newline = body.GetLength() > 0 &&
                     (body[body.GetLength() - 1] == '\n' ||
                      body[body.GetLength() - 1] == '\r');
    out.text = NormalizeLf(body);
    return true;
}

String BuildRawBytes(const TextFile& tf, const String& normalized_text)
{
    String body = RestoreNewlines(normalized_text, tf.newline);
    if(tf.eof_newline && body.GetLength() && body[body.GetLength() - 1] != '\n' && body[body.GetLength() - 1] != '\r')
        body << tf.newline;

    String out;
    if(tf.bom) {
        out.Cat(0xef);
        out.Cat(0xbb);
        out.Cat(0xbf);
    }
    out << body;
    return out;
}

bool SaveTextFilePreserving(const TextFile& original,
                            const String& normalized_text,
                            String& error,
                            const String& inject_fault = String(),
                            const char *fault_name = "permission_denied_write_target",
                            const String& stage = "apply_write")
{
    String bytes = BuildRawBytes(original, normalized_text);
    return WriteFileDetailed(original.path, bytes, stage, "write_file", inject_fault, fault_name, error);
}

String GetJsonString(Value v, const String& def = String())
{
    if(IsNull(v))
        return def;
    return (String)v;
}

bool GetJsonBool(Value v, bool def = false)
{
    if(IsNull(v))
        return def;
    return (bool)v;
}

Vector<String> GetJsonStringArray(Value v)
{
    Vector<String> out;
    if(IsNull(v))
        return out;

    for(int i = 0; i < v.GetCount(); i++)
        out.Add((String)v[i]);

    return out;
}

String GetPayloadText(Value edit)
{
    if(!IsNull(edit["text"]))
        return NormalizeLf((String)edit["text"]);
    if(!IsNull(edit["replace"]))
        return NormalizeLf((String)edit["replace"]);
    if(!IsNull(edit["new_text"]))
        return NormalizeLf((String)edit["new_text"]);
    if(!IsNull(edit["new_lines"])) {
        Vector<String> lines = GetJsonStringArray(edit["new_lines"]);
        return NormalizeLf(JoinLines(lines));
    }
    return String();
}

bool HasSuspiciousText(const String& text, String& reason)
{
    if(text.Find("`r`n") >= 0) {
        reason = "literal PowerShell `r`n sequence found";
        return true;
    }
    if(text.Find("<<<<<<<") >= 0 || text.Find("=======") >= 0 || text.Find(">>>>>>>") >= 0) {
        reason = "merge marker found";
        return true;
    }
    for(int i = 0; i < text.GetLength(); i++) {
        if(text[i] == '\0') {
            reason = "NUL byte found";
            return true;
        }
    }
    return false;
}

struct PlannedFile : Moveable<PlannedFile> {
    String rel;
    TextFile original;
    String next_text;
    bool changed = false;
};

int FindPlanned(Vector<PlannedFile>& files, const String& rel)
{
    for(int i = 0; i < files.GetCount(); i++)
        if(files[i].rel == rel)
            return i;
    return -1;
}

bool EnsurePlanned(Vector<PlannedFile>& files, const String& root, const String& rel,
                   PlannedFile*& pf, String& error)
{
    if(IsAbsoluteOrUnsafePath(rel)) {
        error = "UNSAFE_PATH: " + rel;
        return false;
    }

    int idx = FindPlanned(files, rel);
    if(idx >= 0) {
        pf = &files[idx];
        return true;
    }

    PlannedFile& add = files.Add();
    add.rel = rel;
    if(!ReadTextFile(JoinPath(root, rel), rel, add.original, error))
        return false;
    add.next_text = add.original.text;
    pf = &add;
    return true;
}

bool VerifyHashGuard(Value edit, const PlannedFile& pf, String& error)
{
    String expected = GetJsonString(edit["expected_sha256"]);
    if(expected.IsEmpty())
        expected = GetJsonString(edit["expected_hash"]);

    if(expected.GetLength() && expected != pf.original.sha256) {
        error = "HASH_MISMATCH: " + pf.rel;
        return false;
    }

    return true;
}

bool ApplyOneEdit(Value edit, Vector<PlannedFile>& files, const String& root, String& error)
{
    String op = GetJsonString(edit["op"]);
    String rel = GetJsonString(edit["file"]);
    if(op.IsEmpty() || rel.IsEmpty()) {
        error = "BAD_REQUEST: edit requires op and file";
        return false;
    }

    PlannedFile* pf = NULL;
    if(!EnsurePlanned(files, root, rel, pf, error))
        return false;
    if(!VerifyHashGuard(edit, *pf, error))
        return false;

    String& text = pf->next_text;

    if(op == "replace_exact") {
        String find = NormalizeLf(GetJsonString(edit["find"]));
        String repl = GetPayloadText(edit);

        int count = CountOccurrences(text, find);
        if(count == 0) {
            error = "NO_MATCH: " + rel;
            return false;
        }
        if(count > 1) {
            error = "AMBIGUOUS_MATCH: " + rel;
            return false;
        }

        int pos = text.Find(find);
        text = ReplaceFirstAt(text, pos, find.GetLength(), repl);
        pf->changed = true;
        return true;
    }

    if(op == "replace_all_exact") {
        String find = NormalizeLf(GetJsonString(edit["find"]));
        String repl = GetPayloadText(edit);

        int count = CountOccurrences(text, find);
        if(count == 0) {
            error = "NO_MATCH: " + rel;
            return false;
        }

        text = ReplaceAllExact(text, find, repl);
        pf->changed = true;
        return true;
    }

    if(op == "insert_before_exact" || op == "insert_before_exact_line") {
        String anchor = NormalizeLf(GetJsonString(edit["anchor"]));
        String insert = GetPayloadText(edit);

        int count = CountOccurrences(text, anchor);
        if(count == 0) {
            error = "NO_MATCH: " + rel;
            return false;
        }
        if(count > 1) {
            error = "AMBIGUOUS_MATCH: " + rel;
            return false;
        }

        int pos = text.Find(anchor);
        text = text.Left(pos) + insert + text.Mid(pos);
        pf->changed = true;
        return true;
    }

    if(op == "insert_after_exact" || op == "insert_after_exact_line") {
        String anchor = NormalizeLf(GetJsonString(edit["anchor"]));
        String insert = GetPayloadText(edit);

        int count = CountOccurrences(text, anchor);
        if(count == 0) {
            error = "NO_MATCH: " + rel;
            return false;
        }
        if(count > 1) {
            error = "AMBIGUOUS_MATCH: " + rel;
            return false;
        }

        int pos = text.Find(anchor) + anchor.GetLength();
        text = text.Left(pos) + insert + text.Mid(pos);
        pf->changed = true;
        return true;
    }

    if(op == "delete_exact") {
        String find = NormalizeLf(GetJsonString(edit["find"]));

        int count = CountOccurrences(text, find);
        if(count == 0) {
            error = "NO_MATCH: " + rel;
            return false;
        }
        if(count > 1) {
            error = "AMBIGUOUS_MATCH: " + rel;
            return false;
        }

        int pos = text.Find(find);
        text = ReplaceFirstAt(text, pos, find.GetLength(), "");
        pf->changed = true;
        return true;
    }

    if(op == "rewrite_file") {
        text = GetPayloadText(edit);
        pf->changed = true;
        return true;
    }

    if(op == "replace_between") {
        String start = NormalizeLf(GetJsonString(edit["start"]));
        String end = NormalizeLf(GetJsonString(edit["end"]));
        String repl = GetPayloadText(edit);

        int sp = text.Find(start);
        if(sp < 0) {
            error = "NO_MATCH: start anchor in " + rel;
            return false;
        }
        int content_start = sp + start.GetLength();
        int ep = text.Find(end, content_start);
        if(ep < 0) {
            error = "NO_MATCH: end anchor in " + rel;
            return false;
        }
        if(text.Find(start, content_start) >= 0) {
            error = "AMBIGUOUS_MATCH: start anchor in " + rel;
            return false;
        }

        text = text.Left(content_start) + repl + text.Mid(ep);
        pf->changed = true;
        return true;
    }

    if(op == "ensure_include") {
        String include_line = NormalizeLf(GetJsonString(edit["include"]));
        if(include_line.IsEmpty()) {
            error = "BAD_REQUEST: ensure_include requires include";
            return false;
        }
        if(text.Find(include_line) >= 0)
            return true;

        Vector<String> lines = SplitLinesKeepEmpty(text);
        int insert_at = 0;
        for(int i = 0; i < lines.GetCount(); i++) {
            String t = TrimBoth(lines[i]);
            if(StartsWith2(t, "#include"))
                insert_at = i + 1;
        }
        lines.Insert(insert_at, include_line);
        text = JoinLines(lines);
        pf->changed = true;
        return true;
    }

    if(op == "replace_lines") {
        int start_line = IsNull(edit["start_line"]) ? -1 : (int)edit["start_line"];
        int end_line = IsNull(edit["end_line"]) ? -1 : (int)edit["end_line"];
        if(start_line < 1 || end_line < start_line) {
            error = "BAD_REQUEST: replace_lines needs 1-based start_line/end_line";
            return false;
        }

        Vector<String> lines = SplitLinesKeepEmpty(text);
        if(end_line > lines.GetCount()) {
            error = "RANGE_ERROR: line range outside file";
            return false;
        }

        Vector<String> contains = GetJsonStringArray(edit["expected_contains"]);
        Vector<String> old_lines;
        for(int k = start_line - 1; k < end_line; k++)
            old_lines.Add(lines[k]);
        String old_block = JoinLines(old_lines);
        for(int i = 0; i < contains.GetCount(); i++) {
            if(old_block.Find(contains[i]) < 0) {
                error = "VALIDATION_FAILED: expected_contains missing in line range";
                return false;
            }
        }

        Vector<String> repl = GetJsonStringArray(edit["new_lines"]);
        lines.Remove(start_line - 1, end_line - start_line + 1);
        for(int i = 0; i < repl.GetCount(); i++)
            lines.Insert(start_line - 1 + i, repl[i]);
        text = JoinLines(lines);
        pf->changed = true;
        return true;
    }

    error = "UNSUPPORTED_OP: " + op;
    return false;
}

bool ValidatePlanned(Value req, const Vector<PlannedFile>& files, String& error)
{
    bool allow_suspicious = GetJsonBool(req["allow_suspicious"], false);

    for(int i = 0; i < files.GetCount(); i++) {
        const PlannedFile& pf = files[i];
        String reason;
        if(!allow_suspicious && HasSuspiciousText(pf.next_text, reason)) {
            error = "VALIDATION_FAILED: " + pf.rel + ": " + reason;
            return false;
        }
    }

    Value validation = req["validation"];
    Vector<String> must = GetJsonStringArray(validation["must_contain"]);
    Vector<String> forbid = GetJsonStringArray(validation["forbid"]);

    for(int i = 0; i < files.GetCount(); i++) {
        const PlannedFile& pf = files[i];
        for(int j = 0; j < must.GetCount(); j++) {
            if(pf.next_text.Find(must[j]) < 0) {
                error = "VALIDATION_FAILED: missing required text in " + pf.rel;
                return false;
            }
        }
        for(int j = 0; j < forbid.GetCount(); j++) {
            if(pf.next_text.Find(forbid[j]) >= 0) {
                error = "VALIDATION_FAILED: forbidden text in " + pf.rel;
                return false;
            }
        }
    }

    return true;
}

bool Plan(Value req, Vector<PlannedFile>& files, String& error)
{
    String root = GetJsonString(req["workspace_root"], GetCurrentDirectory());
    Value edits = req["edits"];
    if(IsNull(edits) || edits.GetCount() == 0) {
        error = "BAD_REQUEST: no edits";
        return false;
    }

    for(int i = 0; i < edits.GetCount(); i++) {
        if(!ApplyOneEdit(edits[i], files, root, error))
            return false;
    }

    return ValidatePlanned(req, files, error);
}

String BuildPreviewJson(bool ok, const String& error, const Vector<PlannedFile>& files, const String& startup_scan_json = "{}")
{
    String out;
    out << "{\n";
    out << "  \"ok\": " << JBool(ok) << ",\n";
    out << "  \"error\": " << JString(ErrorCodeOnly(error));
    if(!ok)
        AppendStructuredErrorFields(out, error);
    out << ",\n  \"startup_scan\": " << startup_scan_json << ",\n  \"files\": [\n";
    for(int i = 0; i < files.GetCount(); i++) {
        const PlannedFile& pf = files[i];
        if(i)
            out << ",\n";
        out << "    {\n";
        out << "      \"path\": " << JString(pf.rel) << ",\n";
        out << "      \"changed\": " << JBool(pf.changed) << ",\n";
        out << "      \"hash_before\": " << JString(pf.original.sha256) << ",\n";
        out << "      \"hash_after\": " << JString(Sha256String(BuildRawBytes(pf.original, pf.next_text))) << ",\n";
        out << "      \"diff\": " << JString(MakeUnifiedDiff(pf.rel, pf.original.text, pf.next_text)) << "\n";
        out << "    }";
    }
    out << "\n  ]\n";
    out << "}\n";
    return out;
}

bool EnsureJournal(const String& root, String& error, const String& inject_fault = String())
{
    String jr = JournalRoot(root);
    if(!EnsureDirectoryDetailed(jr, "journal_dir", "create_dir", inject_fault, "permission_denied_create_journal_dir", error))
        return false;

    String workspace_json = AppendFileName(jr, "workspace.json");
    if(!FileExists(workspace_json)) {
        String id = "work-" + Id10();
        String body;
        body << "{\n"
             << "  \"workspace_id\": " << JString(id) << ",\n"
             << "  \"workspace_root\": " << JString(root) << ",\n"
             << "  \"created\": " << JString(Now()) << ",\n"
             << "  \"format_version\": 1\n"
             << "}\n";
        if(!WriteFileDetailed(workspace_json, body, "journal_workspace", "write_file", inject_fault, "permission_denied_transaction_log", error))
            return false;
    }

    return true;
}

String EnsureSession(Value req, const String& root, String& session_dir, String& error, const String& inject_fault = String())
{
    Value session = req["session"];
    String sid = GetJsonString(session["id"]);
    String goal = GetJsonString(session["goal"], GetJsonString(req["summary"], "patchtrack session"));
    if(sid.IsEmpty())
        sid = "sess-" + Id10();

    String folder = sid + "-" + Slug(goal);
    session_dir = AppendFileName(JournalRoot(root), folder);

    if(!EnsureDirectoryDetailed(session_dir, "session_dir", "create_dir", inject_fault, "permission_denied_create_session_dir", error))
        return String();
    if(!EnsureDirectoryDetailed(AppendFileName(session_dir, "snap"), "snapshot_dir", "create_dir", inject_fault, "permission_denied_create_snapshot_dir", error))
        return String();

    String session_json = AppendFileName(session_dir, "session.json");
    if(!FileExists(session_json)) {
        String body;
        body << "{\n"
             << "  \"session_id\": " << JString(sid) << ",\n"
             << "  \"goal\": " << JString(goal) << ",\n"
             << "  \"created\": " << JString(Now()) << ",\n"
             << "  \"last_active\": " << JString(Now()) << ",\n"
             << "  \"archived\": false\n"
             << "}\n";
        if(!WriteFileDetailed(session_json, body, "session_json", "write_file", inject_fault, "permission_denied_transaction_log", error))
            return String();
    }

    return sid;
}

String GetTestingFault(Value req)
{
    Value testing = req["testing"];
    return GetJsonString(testing["inject_fault"], GetJsonString(req["inject_fault"]));
}

const int kClaimLeaseSeconds = 120;

int64 NowEpochSeconds()
{
    return (int64)time(NULL);
}

String ClaimsRoot(const String& root)
{
    return AppendFileName(JournalRoot(root), "claims");
}

String RecoveryScanPath(const String& root)
{
    return AppendFileName(JournalRoot(root), "recovery_scan.json");
}

struct SessionClaimInfo : Moveable<SessionClaimInfo> {
    String session_id;
    String actor;
    String summary;
    String intent;
    Vector<String> files;
    String claim_file;
    int64 started_epoch = 0;
    int64 last_heartbeat_epoch = 0;
    int64 expires_epoch = 0;
    int lease_seconds = kClaimLeaseSeconds;
};

struct RecoveryScanInfo : Moveable<RecoveryScanInfo> {
    Vector<SessionClaimInfo> active_claims;
    Vector<SessionClaimInfo> stale_claims;
    Vector<String> pending_transactions;
    Vector<String> recovery_required_transactions;
};

String BuildJsonStringArray(const Vector<String>& items);

struct ClaimGuard : NoCopy {
    String claim_file;
    bool active = false;

    ~ClaimGuard()
    {
        if(active && !claim_file.IsEmpty())
            FileDelete(claim_file);
    }
};

Vector<String> CollectChangedFiles(const Vector<PlannedFile>& files)
{
    Vector<String> out;
    for(int i = 0; i < files.GetCount(); i++)
        if(files[i].changed)
            out.Add(files[i].rel);
    return out;
}

String BuildClaimJson(const SessionClaimInfo& claim)
{
    String body;
    body << "{\n"
         << "  \"session_id\": " << JString(claim.session_id) << ",\n"
         << "  \"actor\": " << JString(claim.actor) << ",\n"
         << "  \"summary\": " << JString(claim.summary) << ",\n"
         << "  \"intent\": " << JString(claim.intent) << ",\n"
         << "  \"started_epoch\": " << AsString((int)claim.started_epoch) << ",\n"
         << "  \"last_heartbeat_epoch\": " << AsString((int)claim.last_heartbeat_epoch) << ",\n"
         << "  \"expires_epoch\": " << AsString((int)claim.expires_epoch) << ",\n"
         << "  \"lease_seconds\": " << AsString(claim.lease_seconds) << ",\n"
         << "  \"files\": " << BuildJsonStringArray(claim.files) << "\n"
         << "}\n";
    return body;
}

bool SaveClaimInfo(const String& root,
                   SessionClaimInfo& claim,
                   String& error,
                   const String& inject_fault = String())
{
    String claims_root = ClaimsRoot(root);
    if(!EnsureDirectoryDetailed(claims_root, "claims_dir", "create_dir", inject_fault, "permission_denied_transaction_log", error))
        return false;

    if(claim.claim_file.IsEmpty())
        claim.claim_file = AppendFileName(claims_root, claim.session_id + ".json");

    int64 now = NowEpochSeconds();
    if(claim.started_epoch == 0)
        claim.started_epoch = now;
    claim.last_heartbeat_epoch = now;
    claim.expires_epoch = now + claim.lease_seconds;

    return WriteFileDetailed(claim.claim_file, BuildClaimJson(claim), "claim_write", "write_transaction_log", inject_fault, "permission_denied_transaction_log", error);
}

bool LoadClaimInfo(const String& claim_file, SessionClaimInfo& claim)
{
    if(!FileExists(claim_file))
        return false;

    Value parsed;
    try {
        parsed = ParseJSON(LoadFile(claim_file));
    }
    catch(CParser::Error) {
        return false;
    }
    if(parsed.IsError() || !parsed.Is<ValueMap>())
        return false;

    claim.session_id = GetJsonString(parsed["session_id"]);
    claim.actor = GetJsonString(parsed["actor"]);
    claim.summary = GetJsonString(parsed["summary"]);
    claim.intent = GetJsonString(parsed["intent"]);
    claim.started_epoch = IsNull(parsed["started_epoch"]) ? 0 : (int64)(int)parsed["started_epoch"];
    claim.last_heartbeat_epoch = IsNull(parsed["last_heartbeat_epoch"]) ? 0 : (int64)(int)parsed["last_heartbeat_epoch"];
    claim.expires_epoch = IsNull(parsed["expires_epoch"]) ? 0 : (int64)(int)parsed["expires_epoch"];
    claim.lease_seconds = IsNull(parsed["lease_seconds"]) ? kClaimLeaseSeconds : (int)parsed["lease_seconds"];
    claim.files = GetJsonStringArray(parsed["files"]);
    claim.claim_file = claim_file;
    return !claim.session_id.IsEmpty();
}

bool ClaimIntentConflicts(const String& existing_intent, const String& requested_intent)
{
    if(existing_intent == "read" || requested_intent == "read")
        return false;
    return true;
}

bool ClaimsOverlap(const SessionClaimInfo& claim, const Vector<String>& files, String& overlap_file)
{
    for(int i = 0; i < claim.files.GetCount(); i++)
        for(int j = 0; j < files.GetCount(); j++)
            if(claim.files[i] == files[j]) {
                overlap_file = files[j];
                return true;
            }
    return false;
}

String BuildFileBusyError(const SessionClaimInfo& claim, const String& rel)
{
    int64 age = NowEpochSeconds() - claim.last_heartbeat_epoch;
    String error = EncodeStructuredError("FILE_BUSY",
                                         "claim_check",
                                         rel,
                                         "claim_conflict",
                                         "another_patchtrack_session",
                                         "Wait for the active PatchTrack claim to finish, retry after it expires, or ask the user to resolve the concurrent edit.",
                                         "Another PatchTrack session is actively editing this file.");
    error << "||blocking_session_id=" << claim.session_id;
    error << "||blocking_intent=" << claim.intent;
    error << "||blocking_summary=" << claim.summary;
    error << "||heartbeat_age_sec=" << AsString((int)max<int64>(age, 0));
    return error;
}

String BuildClaimArrayJson(const Vector<SessionClaimInfo>& claims)
{
    String out;
    out << "[\n";
    for(int i = 0; i < claims.GetCount(); i++) {
        if(i)
            out << ",\n";
        out << "    {\n"
            << "      \"session_id\": " << JString(claims[i].session_id) << ",\n"
            << "      \"actor\": " << JString(claims[i].actor) << ",\n"
            << "      \"summary\": " << JString(claims[i].summary) << ",\n"
            << "      \"intent\": " << JString(claims[i].intent) << ",\n"
            << "      \"last_heartbeat_epoch\": " << AsString((int)claims[i].last_heartbeat_epoch) << ",\n"
            << "      \"expires_epoch\": " << AsString((int)claims[i].expires_epoch) << ",\n"
            << "      \"files\": " << BuildJsonStringArray(claims[i].files) << "\n"
            << "    }";
    }
    out << "\n  ]";
    return out;
}

String BuildRecoveryScanJson(const RecoveryScanInfo& scan)
{
    String out;
    out << "{\n"
        << "  \"timestamp\": " << JString(Now()) << ",\n"
        << "  \"active_claims\": " << BuildClaimArrayJson(scan.active_claims) << ",\n"
        << "  \"stale_claims\": " << BuildClaimArrayJson(scan.stale_claims) << ",\n"
        << "  \"pending_transactions\": " << BuildJsonStringArray(scan.pending_transactions) << ",\n"
        << "  \"recovery_required_transactions\": " << BuildJsonStringArray(scan.recovery_required_transactions) << "\n"
        << "}";
    return out;
}

bool PersistRecoveryScan(const String& root, const RecoveryScanInfo& scan, const String& inject_fault = String())
{
    if(!DirectoryExists(JournalRoot(root)))
        return true;
    String error;
    return WriteFileDetailed(RecoveryScanPath(root), BuildRecoveryScanJson(scan) + "\n", "recovery_scan", "write_transaction_log", inject_fault, "permission_denied_transaction_log", error);
}

bool RunStartupRecoveryScan(const String& root,
                            RecoveryScanInfo& scan,
                            const String& inject_fault = String(),
                            bool persist = true)
{
    String journal = JournalRoot(root);
    if(!DirectoryExists(journal))
        return true;

    int64 now = NowEpochSeconds();
    String claims_root = ClaimsRoot(root);
    if(DirectoryExists(claims_root)) {
        FindFile ff(AppendFileName(claims_root, "*.json"));
        while(ff) {
            if(ff.IsFile()) {
                SessionClaimInfo claim;
                String claim_file = AppendFileName(claims_root, ff.GetName());
                if(LoadClaimInfo(claim_file, claim)) {
                    if(claim.expires_epoch > 0 && claim.expires_epoch <= now) {
                        scan.stale_claims.Add(pick(claim));
                        FileDelete(claim_file);
                    }
                    else {
                        scan.active_claims.Add(pick(claim));
                    }
                }
            }
            ff.Next();
        }
    }

    FindFile sess(AppendFileName(journal, "sess-*"));
    while(sess) {
        if(sess.IsFolder()) {
            String session_dir = AppendFileName(journal, sess.GetName());
            FindFile tr(AppendFileName(session_dir, "tran-*.json"));
            while(tr) {
                if(tr.IsFile()) {
                    Value parsed;
                    try {
                        parsed = ParseJSON(LoadFile(AppendFileName(session_dir, tr.GetName())));
                    }
                    catch(CParser::Error) {
                        tr.Next();
                        continue;
                    }
                    String status = GetJsonString(parsed["status"]);
                    String tran_id = GetJsonString(parsed["transaction_id"], tr.GetName());
                    if(status == "pending")
                        scan.pending_transactions.Add(tran_id);
                    else if(status == "failed_recovery_required")
                        scan.recovery_required_transactions.Add(tran_id);
                }
                tr.Next();
            }
        }
        sess.Next();
    }

    if(persist)
        PersistRecoveryScan(root, scan, inject_fault);
    return true;
}

bool CheckClaimConflicts(const RecoveryScanInfo& scan,
                         const Vector<String>& files,
                         const String& requested_intent,
                         const String& session_id,
                         String& error)
{
    for(int i = 0; i < scan.active_claims.GetCount(); i++) {
        const SessionClaimInfo& claim = scan.active_claims[i];
        if(claim.session_id == session_id)
            continue;
        if(!ClaimIntentConflicts(claim.intent, requested_intent))
            continue;

        String overlap_file;
        if(ClaimsOverlap(claim, files, overlap_file)) {
            error = BuildFileBusyError(claim, overlap_file);
            return false;
        }
    }
    return true;
}
bool RestorePlannedFiles(const Vector<PlannedFile>& files,
                         const Vector<int>& written_indices,
                         String& error,
                         const String& inject_fault = String())
{
    for(int i = written_indices.GetCount() - 1; i >= 0; i--) {
        const PlannedFile& pf = files[written_indices[i]];
        if(!WriteFileDetailed(pf.original.path, pf.original.raw, "apply_restore", "write_file", inject_fault, "permission_denied_write_target", error))
            return false;
    }
    return true;
}

String BuildJsonStringArray(const Vector<String>& items)
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

String SnapshotName(const String& tran_id, const String& rel)
{
    String safe;
    for(int i = 0; i < rel.GetLength(); i++) {
        int c = (byte)rel[i];
        if((c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') ||
           c == '.' || c == '_' || c == '-')
            safe.Cat(c);
        else
            safe.Cat('-');
    }
    return tran_id + "-" + safe + ".before";
}

String BuildApplyFilesJson(const Vector<PlannedFile>& files,
                           const Vector<String>& snapshot_rel,
                           const Vector<String>& hash_after)
{
    String files_json;
    files_json << "[\n";
    int changed = 0;
    for(int i = 0; i < files.GetCount(); i++) {
        const PlannedFile& pf = files[i];
        if(!pf.changed)
            continue;

        if(changed)
            files_json << ",\n";

        files_json << "    {\n"
                   << "      \"path\": " << JString(pf.rel) << ",\n"
                   << "      \"hash_before\": " << JString(pf.original.sha256) << ",\n"
                   << "      \"hash_after\": " << JString(hash_after[changed]) << ",\n"
                   << "      \"snapshot_before\": " << JString(snapshot_rel[changed]) << ",\n"
                   << "      \"diff\": " << JString(MakeUnifiedDiff(pf.rel, pf.original.text, pf.next_text)) << "\n"
                   << "    }";
        changed++;
    }
    files_json << "\n  ]";
    return files_json;
}

bool SaveTransactionRecord(const String& tran_file,
                           const String& tran_id,
                           const String& session_id,
                           const String& actor,
                           const String& status,
                           const String& summary,
                           const String& files_json,
                           const Vector<String>& checks,
                           const String& rollback_of,
                           const String& primary_error,
                           const String& recovery_error,
                           String& error,
                           const String& inject_fault = String(),
                           const String& stage = "transaction_log")
{
    String tran_json;
    tran_json << "{\n"
              << "  \"transaction_id\": " << JString(tran_id) << ",\n"
              << "  \"session_id\": " << JString(session_id) << ",\n"
              << "  \"timestamp\": " << JString(Now()) << ",\n"
              << "  \"actor\": " << JString(actor) << ",\n"
              << "  \"status\": " << JString(status) << ",\n"
              << "  \"summary\": " << JString(summary);
    if(!rollback_of.IsEmpty())
        tran_json << ",\n  \"rollback_of\": " << JString(rollback_of);
    if(!primary_error.IsEmpty())
        tran_json << ",\n  \"error\": " << JString(primary_error);
    if(!recovery_error.IsEmpty())
        tran_json << ",\n  \"recovery_error\": " << JString(recovery_error);
    tran_json << ",\n  \"files\": " << files_json << ",\n"
              << "  \"validation\": {\"ok\": true, \"checks\": " << BuildJsonStringArray(checks) << "}\n"
              << "}\n";

    return WriteFileDetailed(tran_file, tran_json, stage, "write_transaction_log", inject_fault, "permission_denied_transaction_log", error);
}

bool FinalizeApplyFailure(const String& tran_file,
                          const String& tran_id,
                          const String& session_id,
                          const String& actor,
                          const String& summary,
                          const String& files_json,
                          const Vector<String>& checks,
                          const Vector<PlannedFile>& files,
                          const Vector<int>& written_indices,
                          const String& primary_error,
                          String& error,
                          const String& inject_fault = String())
{
    String restore_error;
    if(RestorePlannedFiles(files, written_indices, restore_error, inject_fault)) {
        String record_error;
        if(!SaveTransactionRecord(tran_file, tran_id, session_id, actor,
                                  "failed_restored", summary, files_json,
                                  checks, String(), primary_error, String(), record_error,
                                  inject_fault, "apply_transaction_log")) {
            error = primary_error + "; JOURNAL_UPDATE_FAILED: " + record_error;
            return false;
        }
        error = primary_error;
        return false;
    }

    String recovery_message = primary_error + "; RECOVERY_FAILED: " + restore_error;
    String record_error;
    SaveTransactionRecord(tran_file, tran_id, session_id, actor,
                          "failed_recovery_required", summary, files_json,
                          checks, String(), primary_error, restore_error, record_error,
                          inject_fault, "apply_transaction_log");
    error = recovery_message;
    return false;
}

bool WriteRecentTransaction(const String& root,
                            const String& session_id,
                            const String& tran_id,
                            const String& inject_fault = String())
{
    String recent = AppendFileName(JournalRoot(root), "recent.json");
    String recent_json;
    recent_json << "{\n"
                << "  \"last_active\": " << JString(Now()) << ",\n"
                << "  \"last_session_id\": " << JString(session_id) << ",\n"
                << "  \"last_transaction_id\": " << JString(tran_id) << "\n"
                << "}\n";
    String error;
    return WriteFileDetailed(recent, recent_json, "recent_transaction", "write_transaction_log", inject_fault, "permission_denied_transaction_log", error);
}

bool ApplyWrite(Value req, Vector<PlannedFile>& files, String& result_json, String& error)
{
    Vector<String> checks;
    checks.Add("hash_precondition");
    checks.Add("snapshot_before");
    checks.Add("commit_precondition_recheck");
    checks.Add("write_verify");
    checks.Add("startup_recovery_scan");
    checks.Add("session_claim");

    String root = GetJsonString(req["workspace_root"], GetCurrentDirectory());
    String inject_fault = GetTestingFault(req);

    if(!EnsureJournal(root, error, inject_fault))
        return false;

    String session_dir;
    String session_id = EnsureSession(req, root, session_dir, error, inject_fault);
    if(session_id.IsEmpty())
        return false;

    String summary = GetJsonString(req["summary"], "PatchTrack transaction");
    String actor = GetJsonString(req["actor"], "patchtrack");

    RecoveryScanInfo startup_scan;
    RunStartupRecoveryScan(root, startup_scan, inject_fault, true);

    Vector<String> claimed_files = CollectChangedFiles(files);
    if(!CheckClaimConflicts(startup_scan, claimed_files, "code_edit", session_id, error))
        return false;

    ClaimGuard claim_guard;
    SessionClaimInfo claim;
    if(!claimed_files.IsEmpty()) {
        claim.session_id = session_id;
        claim.actor = actor;
        claim.summary = summary;
        claim.intent = "code_edit";
        claim.files.Clear();
        for(int i = 0; i < claimed_files.GetCount(); i++)
            claim.files.Add(claimed_files[i]);
        if(!SaveClaimInfo(root, claim, error, inject_fault))
            return false;
        claim_guard.claim_file = claim.claim_file;
        claim_guard.active = true;
    }

    String tran_id = "tran-" + Id10();
    String tran_file = AppendFileName(session_dir, tran_id + "-" + Slug(summary) + ".json");

    Vector<String> snapshot_rel;
    Vector<String> planned_after_hashes;
    Vector<int> written_indices;

    for(int i = 0; i < files.GetCount(); i++) {
        PlannedFile& pf = files[i];
        if(!pf.changed)
            continue;

        String snap = AppendFileName("snap", SnapshotName(tran_id, pf.rel));
        String snap_abs = AppendFileName(session_dir, snap);
        if(!WriteFileDetailed(snap_abs, pf.original.raw, "snapshot_before", "write_file", inject_fault, "permission_denied_snapshot_write", error))
            return false;
        snapshot_rel.Add(snap);
        planned_after_hashes.Add(Sha256String(BuildRawBytes(pf.original, pf.next_text)));
    }

    if(inject_fault == "write_failed_snapshot") {
        error = "WRITE_FAILED: injected snapshot failure";
        return false;
    }

    String planned_files_json = BuildApplyFilesJson(files, snapshot_rel, planned_after_hashes);
    if(!SaveTransactionRecord(tran_file, tran_id, session_id, actor,
                              "pending", summary, planned_files_json,
                              checks, String(), String(), String(), error,
                              inject_fault, "apply_transaction_log"))
        return false;

    int writes_completed = 0;
    for(int i = 0; i < files.GetCount(); i++) {
        PlannedFile& pf = files[i];
        if(!pf.changed)
            continue;

        if(claim_guard.active) {
            String claim_error;
            if(!SaveClaimInfo(root, claim, claim_error, inject_fault))
                return FinalizeApplyFailure(tran_file, tran_id, session_id, actor, summary,
                                            planned_files_json, checks, files, written_indices,
                                            claim_error, error, inject_fault);
        }

        if(inject_fault == "write_failed_before_first_write")
            return FinalizeApplyFailure(tran_file, tran_id, session_id, actor, summary,
                                        planned_files_json, checks, files, written_indices,
                                        "WRITE_FAILED: injected before first write", error, inject_fault);

        TextFile latest;
        String read_error;
        if(!ReadTextFile(pf.original.path, pf.rel, latest, read_error, inject_fault, "permission_denied_read_target", "commit_precondition_recheck"))
            return FinalizeApplyFailure(tran_file, tran_id, session_id, actor, summary,
                                        planned_files_json, checks, files, written_indices,
                                        read_error, error, inject_fault);
        if(latest.sha256 != pf.original.sha256)
            return FinalizeApplyFailure(tran_file, tran_id, session_id, actor, summary,
                                        planned_files_json, checks, files, written_indices,
                                        "HASH_MISMATCH: " + pf.rel + " changed before write", error, inject_fault);

        if(!SaveTextFilePreserving(pf.original, pf.next_text, read_error, inject_fault, "permission_denied_write_target", "apply_write"))
            return FinalizeApplyFailure(tran_file, tran_id, session_id, actor, summary,
                                        planned_files_json, checks, files, written_indices,
                                        read_error, error, inject_fault);

        written_indices.Add(i);
        writes_completed++;

        if(inject_fault == "host_crash_after_first_write" && writes_completed == 1)
            PlatformExitAbruptly(97);

        if(inject_fault == "write_failed_after_first_write" && writes_completed == 1)
            return FinalizeApplyFailure(tran_file, tran_id, session_id, actor, summary,
                                        planned_files_json, checks, files, written_indices,
                                        "WRITE_FAILED: injected after first write", error, inject_fault);
    }

    Vector<String> actual_after_hashes;
    for(int i = 0; i < files.GetCount(); i++) {
        PlannedFile& pf = files[i];
        if(!pf.changed)
            continue;

        TextFile after;
        String read_error;
        if(!ReadTextFile(pf.original.path, pf.rel, after, read_error, inject_fault, "permission_denied_read_target", "write_verify"))
            return FinalizeApplyFailure(tran_file, tran_id, session_id, actor, summary,
                                        planned_files_json, checks, files, written_indices,
                                        read_error, error, inject_fault);
        actual_after_hashes.Add(after.sha256);
    }

    String applied_files_json = BuildApplyFilesJson(files, snapshot_rel, actual_after_hashes);

    if(inject_fault == "write_failed_transaction_log")
        return FinalizeApplyFailure(tran_file, tran_id, session_id, actor, summary,
                                    planned_files_json, checks, files, written_indices,
                                    "WRITE_FAILED: injected transaction log failure", error, inject_fault);

    String save_error;
    if(!SaveTransactionRecord(tran_file, tran_id, session_id, actor,
                              "applied", summary, applied_files_json,
                              checks, String(), String(), String(), save_error,
                              inject_fault, "apply_transaction_log"))
        return FinalizeApplyFailure(tran_file, tran_id, session_id, actor, summary,
                                    planned_files_json, checks, files, written_indices,
                                    save_error, error, inject_fault);

    WriteRecentTransaction(root, session_id, tran_id, inject_fault);

    result_json << "{\n"
                << "  \"ok\": true,\n"
                << "  \"transaction_id\": " << JString(tran_id) << ",\n"
                << "  \"session_id\": " << JString(session_id) << ",\n"
                << "  \"transaction_file\": " << JString(tran_file) << ",\n"
                << "  \"startup_scan\": " << BuildRecoveryScanJson(startup_scan) << ",\n"
                << "  \"files\": " << applied_files_json << "\n"
                << "}\n";

    return true;
}
bool FindTransactionFile(const String& root, const String& tran_id,
                         String& session_dir, String& tran_file, String& error)
{
    String journal = JournalRoot(root);
    FindFile sess(AppendFileName(journal, "sess-*"));
    while(sess) {
        if(sess.IsFolder()) {
            String sd = AppendFileName(journal, sess.GetName());
            FindFile tr(AppendFileName(sd, tran_id + "*.json"));
            while(tr) {
                if(tr.IsFile()) {
                    session_dir = sd;
                    tran_file = AppendFileName(sd, tr.GetName());
                    return true;
                }
                tr.Next();
            }
        }
        sess.Next();
    }

    error = "TRANSACTION_NOT_FOUND: " + tran_id;
    return false;
}

struct RollbackFilePlan : Moveable<RollbackFilePlan> {
    String rel;
    String path;
    String current_raw;
    String current_hash;
    String restore_raw;
    String restore_hash;
};

Vector<String> CollectRollbackFiles(const Vector<RollbackFilePlan>& plans)
{
    Vector<String> out;
    for(int i = 0; i < plans.GetCount(); i++)
        out.Add(plans[i].rel);
    return out;
}
String BuildRollbackFilesJson(const Vector<RollbackFilePlan>& plans, const String& rolled_back_transaction)
{
    String files_json;
    files_json << "[\n";
    for(int i = 0; i < plans.GetCount(); i++) {
        if(i)
            files_json << ",\n";
        files_json << "    {\n"
                   << "      \"path\": " << JString(plans[i].rel) << ",\n"
                   << "      \"hash_before\": " << JString(plans[i].current_hash) << ",\n"
                   << "      \"hash_after\": " << JString(plans[i].restore_hash) << ",\n"
                   << "      \"rolled_back_transaction\": " << JString(rolled_back_transaction) << "\n"
                   << "    }";
    }
    files_json << "\n  ]";
    return files_json;
}

bool RestoreRollbackFiles(const Vector<RollbackFilePlan>& plans,
                          const Vector<int>& restored_indices,
                          String& error,
                          const String& inject_fault = String())
{
    for(int i = restored_indices.GetCount() - 1; i >= 0; i--) {
        const RollbackFilePlan& plan = plans[restored_indices[i]];
        if(!WriteFileDetailed(plan.path, plan.current_raw, "rollback_recovery", "write_file", inject_fault, "permission_denied_rollback_write", error))
            return false;
    }
    return true;
}

bool FinalizeRollbackFailure(const String& tran_file,
                             const String& rb_id,
                             const String& session_id,
                             const String& actor,
                             const String& summary,
                             const String& rollback_of,
                             const String& files_json,
                             const Vector<String>& checks,
                             const Vector<RollbackFilePlan>& plans,
                             const Vector<int>& restored_indices,
                             const String& primary_error,
                             String& error,
                             const String& inject_fault = String())
{
    String restore_error;
    if(RestoreRollbackFiles(plans, restored_indices, restore_error, inject_fault)) {
        String record_error;
        if(!SaveTransactionRecord(tran_file, rb_id, session_id, actor,
                                  "failed_restored", summary, files_json,
                                  checks, rollback_of, primary_error, String(), record_error,
                                  inject_fault, "rollback_transaction_log")) {
            error = primary_error + "; JOURNAL_UPDATE_FAILED: " + record_error;
            return false;
        }
        error = primary_error;
        return false;
    }

    String recovery_message = primary_error + "; RECOVERY_FAILED: " + restore_error;
    String record_error;
    SaveTransactionRecord(tran_file, rb_id, session_id, actor,
                          "failed_recovery_required", summary, files_json,
                          checks, rollback_of, primary_error, restore_error, record_error,
                          inject_fault, "rollback_transaction_log");
    error = recovery_message;
    return false;
}

bool Rollback(Value req, String& result_json, String& error)
{
    Vector<String> checks;
    checks.Add("rollback_hash_preflight");
    checks.Add("snapshot_restore");
    checks.Add("rollback_restore_recovery");
    checks.Add("startup_recovery_scan");
    checks.Add("session_claim");

    String root = GetJsonString(req["workspace_root"], GetCurrentDirectory());
    String tran_id = GetJsonString(req["transaction_id"]);
    String inject_fault = GetTestingFault(req);
    if(tran_id.IsEmpty()) {
        error = "BAD_REQUEST: rollback requires transaction_id";
        return false;
    }

    String session_dir, tran_file;
    if(!FindTransactionFile(root, tran_id, session_dir, tran_file, error))
        return false;

    Value tran;
    try {
        String tran_body;
        if(!ReadFileDetailed(tran_file, tran_body, "read_transaction_log", "read_file", inject_fault, "permission_denied_transaction_log", error))
            return false;
        tran = ParseJSON(tran_body);
    }
    catch(CParser::Error) {
        error = "BAD_TRANSACTION_JSON: " + tran_file;
        return false;
    }

    String session_id = GetJsonString(tran["session_id"]);
    Value files = tran["files"];
    String rb_id = "tran-" + Id10();
    String summary = "Rollback " + tran_id;
    String actor = GetJsonString(req["actor"], "patchtrack");
    String rb_file = AppendFileName(session_dir, rb_id + "-rollback.json");

    Vector<RollbackFilePlan> plans;
    for(int i = 0; i < files.GetCount(); i++) {
        String rel = GetJsonString(files[i]["path"]);
        String expected_after = GetJsonString(files[i]["hash_after"]);
        String snapshot_rel = GetJsonString(files[i]["snapshot_before"]);
        String path = JoinPath(root, rel);
        String snap_path = AppendFileName(session_dir, snapshot_rel);

        TextFile current;
        if(!ReadTextFile(path, rel, current, error, inject_fault, "permission_denied_read_target", "rollback_preflight"))
            return false;
        if(current.sha256 != expected_after) {
            error = "ROLLBACK_BLOCKED: current hash does not match transaction hash_after for " + rel;
            return false;
        }

        String before_bytes;
        if(!ReadFileDetailed(snap_path, before_bytes, "read_snapshot_before", "read_file", inject_fault, "permission_denied_snapshot_write", error)) {
            if(ErrorCodeOnly(error) == "FILE_NOT_FOUND")
                error = "ROLLBACK_BLOCKED: missing snapshot " + snap_path;
            return false;
        }

        RollbackFilePlan& plan = plans.Add();
        plan.rel = rel;
        plan.path = path;
        plan.current_raw = current.raw;
        plan.current_hash = current.sha256;
        plan.restore_raw = before_bytes;
        plan.restore_hash = Sha256String(before_bytes);
    }

    RecoveryScanInfo startup_scan;
    RunStartupRecoveryScan(root, startup_scan, inject_fault, true);

    Vector<String> claimed_files = CollectRollbackFiles(plans);
    if(!CheckClaimConflicts(startup_scan, claimed_files, "rollback", session_id, error))
        return false;

    ClaimGuard claim_guard;
    SessionClaimInfo claim;
    if(!claimed_files.IsEmpty()) {
        claim.session_id = session_id;
        claim.actor = actor;
        claim.summary = summary;
        claim.intent = "rollback";
        claim.files.Clear();
        for(int i = 0; i < claimed_files.GetCount(); i++)
            claim.files.Add(claimed_files[i]);
        if(!SaveClaimInfo(root, claim, error, inject_fault))
            return false;
        claim_guard.claim_file = claim.claim_file;
        claim_guard.active = true;
    }

    String files_json = BuildRollbackFilesJson(plans, tran_id);
    if(!SaveTransactionRecord(rb_file, rb_id, session_id, actor,
                              "pending", summary, files_json,
                              checks, tran_id, String(), String(), error,
                              inject_fault, "rollback_transaction_log"))
        return false;

    Vector<int> restored_indices;
    for(int i = 0; i < plans.GetCount(); i++) {
        if(claim_guard.active) {
            String claim_error;
            if(!SaveClaimInfo(root, claim, claim_error, inject_fault))
                return FinalizeRollbackFailure(rb_file, rb_id, session_id, actor,
                                               summary, tran_id, files_json, checks, plans,
                                               restored_indices, claim_error, error, inject_fault);
        }

        if(inject_fault == "rollback_failed_before_first_write")
            return FinalizeRollbackFailure(rb_file, rb_id, session_id, actor,
                                           summary, tran_id, files_json, checks, plans,
                                           restored_indices, "WRITE_FAILED: injected rollback before first write", error, inject_fault);

        if(!WriteFileDetailed(plans[i].path, plans[i].restore_raw, "rollback_write", "write_file", inject_fault, "permission_denied_rollback_write", error))
            return FinalizeRollbackFailure(rb_file, rb_id, session_id, actor,
                                           summary, tran_id, files_json, checks, plans,
                                           restored_indices, error, error, inject_fault);

        restored_indices.Add(i);

        if(inject_fault == "host_crash_during_rollback_after_first_write" && restored_indices.GetCount() == 1)
            PlatformExitAbruptly(98);

        if(inject_fault == "rollback_failed_after_first_write" && restored_indices.GetCount() == 1)
            return FinalizeRollbackFailure(rb_file, rb_id, session_id, actor,
                                           summary, tran_id, files_json, checks, plans,
                                           restored_indices, "WRITE_FAILED: injected rollback after first write", error, inject_fault);
    }

    if(inject_fault == "rollback_failed_transaction_log")
        return FinalizeRollbackFailure(rb_file, rb_id, session_id, actor,
                                       summary, tran_id, files_json, checks, plans,
                                       restored_indices, "WRITE_FAILED: injected rollback transaction log failure", error, inject_fault);

    String save_error;
    if(!SaveTransactionRecord(rb_file, rb_id, session_id, actor,
                              "applied", summary, files_json,
                              checks, tran_id, String(), String(), save_error,
                              inject_fault, "rollback_transaction_log"))
        return FinalizeRollbackFailure(rb_file, rb_id, session_id, actor,
                                       summary, tran_id, files_json, checks, plans,
                                       restored_indices, save_error, error, inject_fault);

    WriteRecentTransaction(root, session_id, rb_id, inject_fault);

    result_json << "{\n"
                << "  \"ok\": true,\n"
                << "  \"rollback_transaction_id\": " << JString(rb_id) << ",\n"
                << "  \"rolled_back\": " << JString(tran_id) << ",\n"
                << "  \"startup_scan\": " << BuildRecoveryScanJson(startup_scan) << ",\n"
                << "  \"files\": " << files_json << "\n"
                << "}\n";
    return true;
}
String Usage()
{
    return
        "patchtrack - transactional source editing for agents\n\n"
        "Commands:\n"
        "  patchtrack preview request.json\n"
        "  patchtrack apply request.json\n"
        "  patchtrack rollback request.json\n"
        "  patchtrack selftest\n"
        "  patchtrack hash <file>\n"
        "  patchtrack history <workspace-root>\n\n"
        "Request schema: workspace_root, summary, actor, edits[].\n"
        "Supported ops: replace_exact, replace_all_exact, insert_before_exact,\n"
        "insert_after_exact, delete_exact, rewrite_file, replace_between,\n"
        "replace_lines, ensure_include.\n";
}

bool Expect(bool condition, const String& message, Vector<String>& failures)
{
    if(condition)
        return true;
    failures.Add(message);
    return false;
}

String MakeSelfTestRoot(const String& name)
{
    String base = AppendFileName(GetCurrentDirectory(), "_selftest");
    RealizeDirectory(base);
    String root = AppendFileName(base, name + "-" + Id10());
    RealizeDirectory(root);
    return root;
}

bool WriteSelfTestFile(const String& root, const String& rel, const String& text, Vector<String>& failures)
{
    String path = JoinPath(root, rel);
    String dir = GetFileFolder(path);
    if(!RealizeDirectory(dir)) {
        failures.Add("failed to create directory: " + dir);
        return false;
    }
    if(!SaveFile(path, text)) {
        failures.Add("failed to write file: " + path);
        return false;
    }
    return true;
}

bool ParseTestJson(const String& json, Value& out, const String& label, Vector<String>& failures)
{
    try {
        out = ParseJSON(json);
        return true;
    }
    catch(CParser::Error) {
        failures.Add("invalid test json: " + label);
        return false;
    }
}

void TestPreviewApplyRollback(Vector<String>& failures)
{
    String root = MakeSelfTestRoot("preview-apply-rollback");
    String rel = "src/sample.txt";
    String before = "alpha\nbeta\n";
    String after = "alpha\ngamma\n";

    if(!WriteSelfTestFile(root, rel, before, failures))
        return;

    String expected = Sha256String(before);
    String req_json;
    req_json
        << "{\n"
        << "  \"workspace_root\": " << JString(root) << ",\n"
        << "  \"summary\": \"selftest replace\",\n"
        << "  \"actor\": \"selftest\",\n"
        << "  \"session\": {\"goal\": \"preview apply rollback\"},\n"
        << "  \"edits\": [\n"
        << "    {\n"
        << "      \"op\": \"replace_exact\",\n"
        << "      \"file\": " << JString(rel) << ",\n"
        << "      \"find\": \"beta\\n\",\n"
        << "      \"text\": \"gamma\\n\",\n"
        << "      \"expected_sha256\": " << JString(expected) << "\n"
        << "    }\n"
        << "  ],\n"
        << "  \"validation\": {\n"
        << "    \"must_contain\": [\"gamma\"],\n"
        << "    \"forbid\": [\"<<<<<<<\"]\n"
        << "  }\n"
        << "}\n";

    Value req;
    if(!ParseTestJson(req_json, req, "apply request", failures))
        return;

    Vector<PlannedFile> preview_files;
    String preview_error;
    bool ok = Plan(req, preview_files, preview_error);
    if(!Expect(ok, "Preview plan failed: " + preview_error, failures))
        return;
    Expect(LoadFile(JoinPath(root, rel)) == before, "Preview mutated workspace content", failures);

    Vector<PlannedFile> files;
    String error;
    ok = Plan(req, files, error);
    if(!Expect(ok, "Plan failed: " + error, failures))
        return;
    Expect(files.GetCount() == 1, "Expected one planned file", failures);
    if(!files.IsEmpty())
        Expect(files[0].next_text == after, "Planned content mismatch", failures);

    String apply_json;
    ok = ApplyWrite(req, files, apply_json, error);
    if(!Expect(ok, "ApplyWrite failed: " + error, failures))
        return;
    Expect(LoadFile(JoinPath(root, rel)) == after, "Applied file content mismatch", failures);
    Expect(FileExists(AppendFileName(root, ".patchtrack\\recent.json")), "recent.json was not created", failures);

    Value apply_result;
    if(!ParseTestJson(apply_json, apply_result, "apply result", failures))
        return;

    String tran_id = GetJsonString(apply_result["transaction_id"]);
    if(!Expect(!tran_id.IsEmpty(), "Missing transaction id in apply result", failures))
        return;

    String rollback_json;
    rollback_json
        << "{\n"
        << "  \"workspace_root\": " << JString(root) << ",\n"
        << "  \"transaction_id\": " << JString(tran_id) << ",\n"
        << "  \"actor\": \"selftest\"\n"
        << "}\n";

    Value rollback_req;
    if(!ParseTestJson(rollback_json, rollback_req, "rollback request", failures))
        return;

    String rollback_result;
    ok = Rollback(rollback_req, rollback_result, error);
    if(!Expect(ok, "Rollback failed: " + error, failures))
        return;
    Expect(LoadFile(JoinPath(root, rel)) == before, "Rollback did not restore original content", failures);
}

void TestHashMismatch(Vector<String>& failures)
{
    String root = MakeSelfTestRoot("hash-mismatch");
    String rel = "src/hash.txt";
    String before = "one\ntwo\n";

    if(!WriteSelfTestFile(root, rel, before, failures))
        return;

    String req_json;
    req_json
        << "{\n"
        << "  \"workspace_root\": " << JString(root) << ",\n"
        << "  \"summary\": \"selftest hash mismatch\",\n"
        << "  \"actor\": \"selftest\",\n"
        << "  \"edits\": [\n"
        << "    {\n"
        << "      \"op\": \"replace_exact\",\n"
        << "      \"file\": " << JString(rel) << ",\n"
        << "      \"find\": \"two\\n\",\n"
        << "      \"text\": \"three\\n\",\n"
        << "      \"expected_sha256\": \"deadbeef\"\n"
        << "    }\n"
        << "  ]\n"
        << "}\n";

    Value req;
    if(!ParseTestJson(req_json, req, "hash mismatch request", failures))
        return;

    Vector<PlannedFile> files;
    String error;
    bool ok = Plan(req, files, error);
    Expect(!ok, "Expected hash mismatch plan failure", failures);
    if(!error.IsEmpty())
        Expect(StartsWith2(error, "HASH_MISMATCH: "), "Unexpected hash mismatch error: " + error, failures);
    Expect(LoadFile(JoinPath(root, rel)) == before, "Hash mismatch should not mutate file", failures);
}

void TestRollbackBlocked(Vector<String>& failures)
{
    String root = MakeSelfTestRoot("rollback-blocked");
    String rel = "src/rollback.txt";
    String before = "red\nblue\n";
    String after = "red\ngreen\n";
    String diverged = "red\nyellow\n";

    if(!WriteSelfTestFile(root, rel, before, failures))
        return;

    String expected = Sha256String(before);
    String req_json;
    req_json
        << "{\n"
        << "  \"workspace_root\": " << JString(root) << ",\n"
        << "  \"summary\": \"selftest rollback blocked\",\n"
        << "  \"actor\": \"selftest\",\n"
        << "  \"edits\": [\n"
        << "    {\n"
        << "      \"op\": \"replace_exact\",\n"
        << "      \"file\": " << JString(rel) << ",\n"
        << "      \"find\": \"blue\\n\",\n"
        << "      \"text\": \"green\\n\",\n"
        << "      \"expected_sha256\": " << JString(expected) << "\n"
        << "    }\n"
        << "  ]\n"
        << "}\n";

    Value req;
    if(!ParseTestJson(req_json, req, "rollback blocked request", failures))
        return;

    Vector<PlannedFile> files;
    String error;
    bool ok = Plan(req, files, error);
    if(!Expect(ok, "Rollback blocked plan failed: " + error, failures))
        return;

    String apply_json;
    ok = ApplyWrite(req, files, apply_json, error);
    if(!Expect(ok, "Rollback blocked apply failed: " + error, failures))
        return;
    Expect(LoadFile(JoinPath(root, rel)) == after, "Rollback blocked apply content mismatch", failures);

    if(!SaveFile(JoinPath(root, rel), diverged)) {
        failures.Add("failed to write diverged rollback test content");
        return;
    }

    Value apply_result;
    if(!ParseTestJson(apply_json, apply_result, "rollback blocked apply result", failures))
        return;

    String tran_id = GetJsonString(apply_result["transaction_id"]);
    String rollback_json;
    rollback_json
        << "{\n"
        << "  \"workspace_root\": " << JString(root) << ",\n"
        << "  \"transaction_id\": " << JString(tran_id) << ",\n"
        << "  \"actor\": \"selftest\"\n"
        << "}\n";

    Value rollback_req;
    if(!ParseTestJson(rollback_json, rollback_req, "rollback blocked rollback request", failures))
        return;

    String rollback_result;
    ok = Rollback(rollback_req, rollback_result, error);
    Expect(!ok, "Expected rollback to be blocked after divergence", failures);
    if(!error.IsEmpty())
        Expect(StartsWith2(error, "ROLLBACK_BLOCKED: "), "Unexpected rollback blocked error: " + error, failures);
    Expect(LoadFile(JoinPath(root, rel)) == diverged, "Blocked rollback should not restore diverged file", failures);
}

void TestCommitPreconditionRecheck(Vector<String>& failures)
{
    String root = MakeSelfTestRoot("commit-precondition");
    String rel = "src/commit.txt";
    String before = "alpha\nbeta\n";
    String diverged = "alpha\ndelta\n";

    if(!WriteSelfTestFile(root, rel, before, failures))
        return;

    String expected = Sha256String(before);
    String req_json;
    req_json
        << "{\n"
        << "  \"workspace_root\": " << JString(root) << ",\n"
        << "  \"summary\": \"selftest commit recheck\",\n"
        << "  \"actor\": \"selftest\",\n"
        << "  \"edits\": [\n"
        << "    {\n"
        << "      \"op\": \"replace_exact\",\n"
        << "      \"file\": " << JString(rel) << ",\n"
        << "      \"find\": \"beta\\n\",\n"
        << "      \"text\": \"gamma\\n\",\n"
        << "      \"expected_sha256\": " << JString(expected) << "\n"
        << "    }\n"
        << "  ]\n"
        << "}\n";

    Value req;
    if(!ParseTestJson(req_json, req, "commit precheck request", failures))
        return;

    Vector<PlannedFile> files;
    String error;
    bool ok = Plan(req, files, error);
    if(!Expect(ok, "Commit precheck plan failed: " + error, failures))
        return;

    if(!SaveFile(JoinPath(root, rel), diverged)) {
        failures.Add("failed to write diverged commit precheck content");
        return;
    }

    String apply_json;
    ok = ApplyWrite(req, files, apply_json, error);
    Expect(!ok, "Expected commit precondition recheck to fail", failures);
    if(!error.IsEmpty())
        Expect(StartsWith2(error, "HASH_MISMATCH: "), "Unexpected commit precondition error: " + error, failures);
    Expect(LoadFile(JoinPath(root, rel)) == diverged, "Commit precondition failure should leave diverged content untouched", failures);
}void TestEofNewlinePreserved(Vector<String>& failures)
{
    String root = MakeSelfTestRoot("eof-newline");
    String rel = "src/include.cpp";
    String before = "#include <A>\n\nint main() {}\n";
    String after = "#include <A>\n#include <B>\n\nint main() {}\n";

    if(!WriteSelfTestFile(root, rel, before, failures))
        return;

    String expected = Sha256String(before);
    String req_json;
    req_json
        << "{\n"
        << "  \"workspace_root\": " << JString(root) << ",\n"
        << "  \"summary\": \"selftest eof newline\",\n"
        << "  \"actor\": \"selftest\",\n"
        << "  \"edits\": [\n"
        << "    {\n"
        << "      \"op\": \"ensure_include\",\n"
        << "      \"file\": " << JString(rel) << ",\n"
        << "      \"include\": \"#include <B>\",\n"
        << "      \"expected_sha256\": " << JString(expected) << "\n"
        << "    }\n"
        << "  ]\n"
        << "}\n";

    Value req;
    if(!ParseTestJson(req_json, req, "eof newline request", failures))
        return;

    Vector<PlannedFile> files;
    String error;
    bool ok = Plan(req, files, error);
    if(!Expect(ok, "EOF newline plan failed: " + error, failures))
        return;

    String apply_json;
    ok = ApplyWrite(req, files, apply_json, error);
    if(!Expect(ok, "EOF newline apply failed: " + error, failures))
        return;

    Expect(LoadFile(JoinPath(root, rel)) == after, "EOF newline was not preserved after ensure_include", failures);
}

int BuildSelfTestReport(String& report)
{
    Vector<String> failures;

    TestPreviewApplyRollback(failures);
    TestHashMismatch(failures);
    TestRollbackBlocked(failures);
    TestEofNewlinePreserved(failures);

    if(failures.IsEmpty()) {
        report = "selftest: ok\n";
        return 0;
    }

    report = "selftest: failed\n";
    for(int i = 0; i < failures.GetCount(); i++)
        report << " - " << failures[i] << "\n";
    return 1;
}

} // namespace

namespace Upp {

String PatchtrackFormatErrorJson(const String& error)
{
    return BuildErrorJson(error);
}

bool PatchtrackReadRequestFile(const String& path, String& body, String& error)
{
    return ReadFileDetailed(path,
                            body,
                            "request_read",
                            "read_request",
                            String(),
                            "permission_denied_read_target",
                            error);
}

bool PatchtrackParseRequestJson(const String& body, Value& req, String& error)
{
    try {
        req = ParseJSON(body);
    }
    catch(CParser::Error) {
        error = "BAD_JSON";
        return false;
    }
    if(req.IsError() || !req.Is<ValueMap>()) {
        error = "BAD_JSON";
        return false;
    }
    return true;
}

bool PatchtrackPreview(Value req, String& result_json, String& error)
{
    Vector<PlannedFile> files;
    bool ok = Plan(req, files, error);

    RecoveryScanInfo startup_scan;
    RunStartupRecoveryScan(GetJsonString(req["workspace_root"], GetCurrentDirectory()),
                           startup_scan,
                           GetTestingFault(req),
                           false);

    result_json = BuildPreviewJson(ok, error, files, BuildRecoveryScanJson(startup_scan));
    return ok;
}

bool PatchtrackApply(Value req, String& result_json, String& error)
{
    Vector<PlannedFile> files;
    if(!Plan(req, files, error))
        return false;
    return ApplyWrite(req, files, result_json, error);
}

bool PatchtrackRollback(Value req, String& result_json, String& error)
{
    return Rollback(req, result_json, error);
}

bool PatchtrackHash(const String& path, String& result_text, String& error)
{
    String s;
    if(!ReadFileDetailed(path, s, "hash_read", "read_file", String(), "permission_denied_read_target", error))
        return false;
    result_text = Sha256String(s) + "  " + path + "\n";
    return true;
}

bool PatchtrackHistory(const String& workspace_root, String& result_text, String& error)
{
    String journal = JournalRoot(workspace_root);
    result_text << "Journal: " << journal << "\n";

    FindFile sess(AppendFileName(journal, "sess-*"));
    while(sess) {
        if(sess.IsFolder()) {
            String sd = AppendFileName(journal, sess.GetName());
            result_text << sess.GetName() << "\n";

            // Each session folder can hold several transactions, so we keep the
            // listing grouped while we already have that folder open.
            FindFile tr(AppendFileName(sd, "tran-*.json"));
            while(tr) {
                if(tr.IsFile())
                    result_text << "  " << tr.GetName() << "\n";
                tr.Next();
            }
        }
        sess.Next();
    }
    return true;
}

bool PatchtrackRecoveryScan(const String& workspace_root, String& result_json, String& error)
{
    RecoveryScanInfo scan;
    if(!RunStartupRecoveryScan(workspace_root, scan, String(), true)) {
        error = "RECOVERY_SCAN_FAILED";
        return false;
    }
    result_json << "{\n"
                << "  \"ok\": true,\n"
                << "  \"workspace_root\": " << JString(workspace_root) << ",\n"
                << "  \"scan\": " << BuildRecoveryScanJson(scan) << "\n"
                << "}\n";
    return true;
}

int PatchtrackSelfTest(String& result_text)
{
    return BuildSelfTestReport(result_text);
}

} // namespace Upp

