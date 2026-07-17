// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#pragma once

class PointerWrap;
class KernelObject;

void __IoInit();
void __IoDoState(PointerWrap &p);
void __IoShutdown();

// Resolves the number of I/O worker threads that should be running right now, combining
// the per-game IOThreadCount setting (g_Config.iIOThreadCount; 0 = "Default", i.e. defer
// to the compat.ini value below) with the IOThreadCount compat.ini flag (0 if the current
// game has no entry), clamped to a sane range.
int GetEffectiveIOThreadCount();

// Re-resolves GetEffectiveIOThreadCount() and grows or shrinks the live I/O worker pool
// to match, without needing to pause or restart emulation. Safe to call at any time
// (e.g. from a settings screen) once a game is running.
void __IoRefreshThreadCount();

struct ScePspDateTime;
struct tm;

u32 sceIoIoctl(u32 id, u32 cmd, u32 indataPtr, u32 inlen, u32 outdataPtr, u32 outlen);

// Called by sceIoIoctl, which normally applies the delay this function writes to usec.
// If you need to call sceIoIoctl from a HLE function implementation more than once, use
// __IoIoctl directly to avoid double-delays.
int __IoIoctl(u32 id, u32 cmd, u32 indataPtr, u32 inlen, u32 outdataPtr, u32 outlen, int &usec);

u32 __IoGetFileHandleFromId(u32 id, u32 &outError);
void __IoCopyDate(ScePspDateTime& date_out, const tm& date_in);

KernelObject *__KernelFileNodeObject();
KernelObject *__KernelDirListingObject();

void Register_IoFileMgrForKernel();
void Register_IoFileMgrForUser();
void Register_StdioForKernel();
void Register_StdioForUser();
