// Copyright (c) 2026- PPSSPP Project.

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

#include <atomic>
#include <vector>

#include "Common/Thread/ThreadManager.h"
#include "GPU/Common/TextureCacheCommon.h"

class LimitedWaitable;

enum class PendingDecodeState : uint32_t { PENDING, DONE };

// Raw source bytes for one mip level, memcpy'd out of PSP RAM synchronously on the emu
// thread before the decode is handed off to a worker (see TextureCacheGLES::BuildTexture).
struct PendingLevelSnapshot {
	std::vector<u8> srcData;
	int w = 0;
	int h = 0;
	int bufw = 0;
	uint32_t texaddr = 0;  // Kept for logging/NotifyMemInfo only, never dereferenced by the task.
};

// Owns everything a background texture decode needs: the inputs (snapshotted synchronously
// on the emu thread, safe to read from any thread since nothing else can mutate them), and
// the outputs (written once by the worker, then only read after Poll() reports DONE).
//
// Strictly owned by a single TexCacheEntry (see TexCacheEntry::pendingDecode) -- unlike
// ReplacedTexture, nothing else needs to share a decode result, so there's no separate
// cache/decimation policy here. The task holds a pointer to this object, never to the
// TexCacheEntry itself, so entry eviction mid-decode is safe (~PendingTextureDecode blocks
// briefly on the worker if it's still running, then frees these buffers -- the worker never
// dereferences anything entry-owned, so there's nothing left to dangle).
class PendingTextureDecode {
public:
	PendingTextureDecode();
	~PendingTextureDecode();

	// Enqueues the background task. Call exactly once, after filling in the inputs below.
	void Start(TextureCacheCommon *owner);

	// Non-blocking. Returns true once decodedLevels/alphaResult are ready to consume.
	bool Poll();

	// Inputs -- fill these in synchronously on the emu thread before calling Start().
	GETextureFormat format = GE_TFMT_5650;
	GEPaletteFormat clutFormat = GE_CMODE_16BIT_BGR5650;
	TexDecodeFlags flags = (TexDecodeFlags)0;
	std::vector<PendingLevelSnapshot> levels;
	DecodeGStateSnapshot gs{};

	// Also snapshotted at start time, not recomputed at swap-in time: by the time decode
	// finishes (frames later), gstate has moved on to whatever the emu thread is currently
	// drawing, so PrepareBuildTexture can't safely be re-run then. These mirror the
	// BuildTexturePlan fields the swap-in upload step needs.
	Draw::DataFormat dstFmt = Draw::DataFormat::UNDEFINED;
	int tw = 0, th = 0;
	int levelsToLoad = 0;
	int levelsToCreate = 0;

	// The CLUT palette itself (not just the scalar fields in `gs`), memcpy'd out of
	// TextureCacheCommon::clutBuf_/clutBufRaw_ synchronously at the same time as the source
	// texel data above. Those members are rewritten by LoadClut() on the emu thread whenever
	// the game issues a further LOADCLUT while this decode is in flight (unlike the raw texel
	// bytes, which the plan doc's "GE command lists execute strictly sequentially" argument
	// only covers for the *synchronous* case) -- reading them live from the worker thread
	// would race and can end up decoding with a completely different texture's palette.
	// Always sized to match clutBuf_/clutBufRaw_'s fixed 2048-byte allocation, even when this
	// texture isn't actually CLUT-indexed (cheap, keeps Start() unconditional/simple).
	std::vector<u32> clutBufSnapshot;
	std::vector<u32> clutBufRawSnapshot;

	// Outputs -- valid only after Poll() returns true. One entry per level in `levels`.
	std::vector<std::vector<u8>> decodedLevels;
	std::vector<CheckAlphaResult> alphaResults;

private:
	friend class TextureDecodeTask;

	void Run(TextureCacheCommon *owner);

	// A member (not a free function) so it inherits PendingTextureDecode's friendship into
	// TextureCacheCommon (TextureCacheCommon.h:516), needed to call the protected
	// DecodeTextureLevel below.
	static void DecodeAllLevels(TextureCacheCommon *owner, const std::vector<PendingLevelSnapshot> &levels,
		GETextureFormat format, GEPaletteFormat clutFormat, TexDecodeFlags flags, Draw::DataFormat dstFmt,
		const DecodeGStateSnapshot &gs, DecodeScratch &scratch,
		std::vector<std::vector<u8>> &outLevels, std::vector<CheckAlphaResult> &outAlpha);

	LimitedWaitable *waitable_ = nullptr;
	std::atomic<PendingDecodeState> state_{ PendingDecodeState::PENDING };

	// Task-local scratch, never TextureCacheCommon's own tmpTexBuf32_/expandClut_ -- those
	// are shared mutable state a concurrent synchronous decode (for some other texture) could
	// be using at the same time. 2048 bytes matches TextureCacheCommon::expandClut_'s size.
	AlignedVector<u32, 16> unswizzleScratch_;
	std::vector<u32> expandClutScratch_ = std::vector<u32>(512);
};
