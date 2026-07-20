/*
PatchTrack platform filesystem abstraction.

This isolates OS-specific file, directory, error-code, and abrupt-exit behavior
from the transactional patch engine so the engine can build cleanly on Windows,
Linux, and macOS.

Change log:
- 2026-04-28: Split platform-specific filesystem and crash helpers out of main.cpp.
*/

#ifndef _patchtrack_platform_fs_h_
#define _patchtrack_platform_fs_h_

#include <Core/Core.h>

namespace Upp {

using PlatformErrorCode = int;

bool PlatformIsPermissionDenied(PlatformErrorCode code);
bool PlatformIsDiskFull(PlatformErrorCode code);
bool PlatformIsPathDirectory(const String& path, bool& is_directory, PlatformErrorCode& code);
bool PlatformCreateDirectory(const String& path, PlatformErrorCode& code);
bool PlatformReadFileRaw(const String& path, String& out, PlatformErrorCode& code, bool& not_found);
bool PlatformWriteFileRaw(const String& path, const String& data, PlatformErrorCode& code);
bool PlatformDeleteFileRaw(const String& path, PlatformErrorCode& code, bool& not_found);
// Confirms that a path and its existing parent chain remain inside the workspace.
bool PlatformPathContained(const String& workspace_root, const String& path, bool allow_missing,
                           String& detail);
void PlatformExitAbruptly(int code);

}

#endif
