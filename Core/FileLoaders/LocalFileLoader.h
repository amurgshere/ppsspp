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

#include "ppsspp_config.h"

#include <mutex>

#if PPSSPP_PLATFORM(SWITCH)
#include <memory>
#include <vector>
#endif

#include "Common/CommonTypes.h"
#include "Common/File/Path.h"
#include "Common/StringUtils.h"
#include "Core/Loaders.h"

#ifdef _WIN32
typedef void *HANDLE;
#endif

#ifdef HAVE_LIBRETRO_VFS
#include <streams/file_stream.h>
typedef RFILE* HANDLE;
#endif

class LocalFileLoader : public FileLoader {
public:
	LocalFileLoader(const Path &filename);
	~LocalFileLoader();

	bool Exists() override;
	bool IsDirectory() override;
	s64 FileSize() override;
	Path GetPath() const override {
		return filename_;
	}
	size_t ReadAt(s64 absolutePos, size_t bytes, size_t count, void *data, Flags flags = Flags::NONE) override;

#if PPSSPP_PLATFORM(SWITCH)
	// libnx has no pread, so normally every ReadAt() serializes behind readLock_ below.
	// PrepareConcurrency() opens additional independent file descriptors to the same
	// path (each with its own lock) so concurrent AsyncIOManager worker threads (see
	// the IOThreadCount compat/config setting) can actually read concurrently instead
	// of just taking turns. Safe to call more than once (e.g. if the setting is raised
	// again at runtime) -- it only ever grows the pool, never shrinks it, since a read
	// could be in flight on a handle we might otherwise want to close.
	void PrepareConcurrency(int hintThreads) override;
#endif

private:
#if !defined(_WIN32) && !defined(HAVE_LIBRETRO_VFS)
	void DetectSizeFd();
	int fd_ = -1;
#else
	HANDLE handle_ = 0;
#endif
	u64 filesize_ = 0;
	Path filename_;
	std::mutex readLock_;
	bool isOpenedByFd_ = false;

#if PPSSPP_PLATFORM(SWITCH)
	struct ExtraHandle {
		int fd = -1;
		std::mutex lock;
	};
	std::mutex extraHandlesMutex_;  // Guards growing extraHandles_ itself, not the reads.
	std::vector<std::unique_ptr<ExtraHandle>> extraHandles_;
#endif
};
