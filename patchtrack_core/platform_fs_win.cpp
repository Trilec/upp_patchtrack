/*
Windows filesystem implementation for PatchTrack.

Change log:
- 2026-04-28: Added Win32-backed platform helpers for file IO, directory creation,
  error classification, and abrupt test termination.
*/

#include "platform_fs.h"

#ifdef _WIN32
#include <windows.h>

namespace Upp {

bool PlatformIsPermissionDenied(PlatformErrorCode code)
{
    DWORD dw = (DWORD)code;
    return dw == ERROR_ACCESS_DENIED ||
           dw == ERROR_SHARING_VIOLATION ||
           dw == ERROR_LOCK_VIOLATION ||
           dw == ERROR_WRITE_PROTECT;
}

bool PlatformIsDiskFull(PlatformErrorCode code)
{
    DWORD dw = (DWORD)code;
    return dw == ERROR_DISK_FULL || dw == ERROR_HANDLE_DISK_FULL;
}

bool PlatformIsPathDirectory(const String& path, bool& is_directory, PlatformErrorCode& code)
{
    DWORD attr = GetFileAttributesA(~path);
    if(attr == INVALID_FILE_ATTRIBUTES) {
        code = (PlatformErrorCode)GetLastError();
        is_directory = false;
        return false;
    }
    is_directory = (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
    code = 0;
    return true;
}

bool PlatformCreateDirectory(const String& path, PlatformErrorCode& code)
{
    if(CreateDirectoryA(~path, NULL)) {
        code = 0;
        return true;
    }
    code = (PlatformErrorCode)GetLastError();
    return false;
}

bool PlatformReadFileRaw(const String& path, String& out, PlatformErrorCode& code, bool& not_found)
{
    not_found = false;
    HANDLE h = CreateFileA(~path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if(h == INVALID_HANDLE_VALUE) {
        code = (PlatformErrorCode)GetLastError();
        not_found = (DWORD)code == ERROR_FILE_NOT_FOUND || (DWORD)code == ERROR_PATH_NOT_FOUND;
        return false;
    }

    LARGE_INTEGER size;
    if(!GetFileSizeEx(h, &size)) {
        code = (PlatformErrorCode)GetLastError();
        CloseHandle(h);
        return false;
    }

    out.Clear();
    out.Reserve((int)size.QuadPart);
    const DWORD kChunk = 1 << 15;
    while(size.QuadPart > 0) {
        DWORD ask = (DWORD)min<int64>(size.QuadPart, kChunk);
        Buffer<char> buffer(ask);
        DWORD got = 0;
        if(!ReadFile(h, buffer, ask, &got, NULL)) {
            code = (PlatformErrorCode)GetLastError();
            CloseHandle(h);
            return false;
        }
        if(got == 0)
            break;
        out.Cat(buffer, got);
        size.QuadPart -= got;
    }

    CloseHandle(h);
    code = 0;
    return true;
}

bool PlatformWriteFileRaw(const String& path, const String& data, PlatformErrorCode& code)
{
    String temp_name;
    HANDLE h = INVALID_HANDLE_VALUE;
    for(int i = 0; i < 16; i++) {
        temp_name = path + Format(".patchtrack-tmp-%d-%d", (int)GetCurrentProcessId(), i);
        h = CreateFileA(~temp_name, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if(h != INVALID_HANDLE_VALUE)
            break;
        if(GetLastError() != ERROR_FILE_EXISTS) {
            code = (PlatformErrorCode)GetLastError();
            return false;
        }
    }
    if(h == INVALID_HANDLE_VALUE) {
        code = ERROR_FILE_EXISTS;
        return false;
    }

    int done = 0;
    while(done < data.GetLength()) {
        DWORD wrote = 0;
        DWORD ask = (DWORD)min(data.GetLength() - done, 1 << 15);
        if(!WriteFile(h, ~data + done, ask, &wrote, NULL)) {
            code = (PlatformErrorCode)GetLastError();
            CloseHandle(h);
            DeleteFileA(~temp_name);
            return false;
        }
        if(wrote == 0) {
            code = ERROR_WRITE_FAULT;
            CloseHandle(h);
            DeleteFileA(~temp_name);
            return false;
        }
        done += wrote;
    }

    if(!FlushFileBuffers(h)) {
        code = (PlatformErrorCode)GetLastError();
        CloseHandle(h);
        DeleteFileA(~temp_name);
        return false;
    }
    CloseHandle(h);

    if(!MoveFileExA(~temp_name, ~path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        code = (PlatformErrorCode)GetLastError();
        DeleteFileA(~temp_name);
        return false;
    }

    code = 0;
    return true;
}

void PlatformExitAbruptly(int code)
{
    ExitProcess((UINT)code);
}

}
#endif
