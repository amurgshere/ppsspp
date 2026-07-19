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

#include "Common/GPU/OpenGL/GLFeatures.h"
#include "Common/GPU/OpenGL/GLCommon.h"
#include "Common/GPU/OpenGL/GLRenderManager.h"
#include "Common/GPU/thin3d.h"
#include "GPU/GPUState.h"
#include "GPU/Common/TextureCacheCommon.h"

struct VirtualFramebuffer;
class FramebufferManagerGLES;
class TextureShaderCache;
class ShaderManagerGLES;
class DrawEngineGLES;
class GLRTexture;

class TextureCacheGLES : public TextureCacheCommon {
public:
	TextureCacheGLES(Draw::DrawContext *draw, Draw2D *draw2D);
	~TextureCacheGLES();

	void Clear(bool delete_them) override;
	void StartFrame() override;

	void SetFramebufferManager(FramebufferManagerGLES *fbManager);
	void SetDepalShaderCache(TextureShaderCache *dpCache) {
		textureShaderCache_ = dpCache;
	}
	void SetDrawEngine(DrawEngineGLES *td) {
		drawEngine_ = td;
	}

	void ForgetLastTexture() override {
		lastBoundTexture = nullptr;
	}

	bool GetCurrentTextureDebug(GPUDebugBuffer &buffer, int level, bool *isFramebuffer) override;

	void DeviceLost() override;
	void DeviceRestore(Draw::DrawContext *draw) override;

protected:
	void BindTexture(TexCacheEntry *entry) override;
	void Unbind() override;
	void ReleaseTexture(TexCacheEntry *entry, bool delete_them) override;

	void BindAsClutTexture(Draw::Texture *tex, bool smooth) override;
	void *GetNativeTextureView(const TexCacheEntry *entry, bool flat) const override;

private:
	void ApplySamplingParams(const SamplerCacheKey &key) override;
	static Draw::DataFormat GetDestFormat(GETextureFormat format, GEPaletteFormat clutFormat) ;

	void UpdateCurrentClut(GEPaletteFormat clutFormat, u32 clutBase, bool clutIndexIsSimple) override;
	void BuildTexture(TexCacheEntry *const entry) override;

	// Async texture decode (see GPU/Common/AsyncTextureDecode.h). Deliberately separate,
	// self-contained code paths rather than sharing BuildTexture's per-level loop -- the
	// eligibility gate in StartAsyncBuildTexture already excludes every case that loop
	// handles beyond plain 2D decode (replacement, scaling, 3D, CLUT-GPU depal), so the
	// swap-in step only ever needs the simple case, and BuildTexture itself stays untouched.
	bool StartAsyncBuildTexture(TexCacheEntry *const entry) override;
	void PollAsyncBuildTexture(TexCacheEntry *const entry) override;
	void CancelAsyncBuildTexture(TexCacheEntry *const entry) override;
	void FinishAsyncBuildTexture(TexCacheEntry *const entry);
	void PollDeferredBuildTexture(TexCacheEntry *const entry) override;
	void CancelDeferredBuildTexture(TexCacheEntry *const entry) override;

	// Shared by both StartAsyncBuildTexture (fresh room in the budget) and
	// PollDeferredBuildTexture (room freed up after being deferred): builds the real-sized
	// placeholder, snapshots the source data/gstate, and enqueues the actual decode task.
	// Precondition: entry->textureName == nullptr and a slot is available.
	void StartRealAsyncDecode(TexCacheEntry *const entry, const BuildTexturePlan &plan);
	// Binds a 1x1 flat-color placeholder -- visually identical to the real placeholder
	// (texture coordinates are normalized, so a single stretched texel reads the same as a
	// flat full-size buffer) but with near-zero GPU alloc/upload/mip-gen cost, for entries
	// that want async decode but the concurrent-decode budget is currently full.
	void CreateDeferredPlaceholder(TexCacheEntry *const entry, const BuildTexturePlan &plan);

	GLRenderManager *render_;

	// TEMPORARY diagnostic for the bug #11 deferred-starvation investigation -- remove once
	// confirmed/fixed. Tracks which frame each currently-deferred entry started waiting on,
	// keyed by address, so ASYNCSTARVE log lines can report how long an entry has been stuck.
	std::map<u32, int> deferredDiagStartFrame_;

	GLRTexture *lastBoundTexture = nullptr;

	FramebufferManagerGLES *framebufferManagerGL_;
	DrawEngineGLES *drawEngine_;

	enum { INVALID_TEX = -1 };
};

static Draw::DataFormat getClutDestFormat(GEPaletteFormat format);
