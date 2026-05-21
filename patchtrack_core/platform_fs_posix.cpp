/*
POSIX filesystem implementation for PatchTrack.

Change log:
- 2026-04-28: Added POSIX-backed platform helpers for Linux and macOS builds.
*/

#include "platform_fs.h"

#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace Upp {

bool PlatformIsPermissionDenied(PlatformErrorCode code)
{
    return code == EACCES ||
           code == EPERM ||
           code == EROFS ||
           code == ETXTBSY ||
           code == EBUSY;
}

bool PlatformIsDiskFull(PlatformErrorCode code)
{
    return code == ENOSPC || code == EDQUOT;
}

bool PlatformIsPathDirectory(const String& path, bool& is_directory, PlatformErrorCode& code)
{
    struct stat st;
    if(stat(~path, &st) != 0) {
        code = errno;
        is_directory = false;
        return false;
    }
    is_directory = S_ISDIR(st.st_mode);
    code = 0;
    return true;
}

bool PlatformCreateDirectory(const String& path, PlatformErrorCode& code)
{
    if(mkdir(~path, 0777) == 0) {
        code = 0;
        return true;
    }
    code = errno;
    return false;
}

bool PlatformReadFileRaw(const String& path, String& out, PlatformErrorCode& code, bool& not_found)
{
    not_found = false;
    int fd = open(~path, O_RDONLY);
    if(fd < 0) {
        code = errno;
        not_found = code == ENOENT || code == ENOTDIR;
        return false;
    }

    out.Clear();
    const int kChunk = 1 << 15;
    Buffer<char> buffer(kChunk);
    for(;;) {
        ssize_t got = read(fd, buffer, kChunk);
        if(got < 0) {
            code = errno;
            close(fd);
            return false;
        }
        if(got == 0)
            break;
        out.Cat(buffer, (int)got);
    }

    close(fd);
    code = 0;
    return true;
}

bool PlatformWriteFileRaw(const String& path, const String& data, PlatformErrorCode& code)
{
    int fd = open(~path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if(fd < 0) {
        code = errno;
        return false;
    }

    int done = 0;
    while(done < data.GetLength()) {
        int ask = min(data.GetLength() - done, 1 << 15);
        ssize_t wrote = write(fd, ~data + done, ask);
        if(wrote < 0) {
            code = errno;
            close(fd);
            return false;
        }
        done += (int)wrote;
    }

    close(fd);
    code = 0;
    return true;
}

void PlatformExitAbruptly(int code)
{
    _exit(code);
}

}
#endif