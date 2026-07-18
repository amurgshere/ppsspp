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

#include <algorithm>
#include <cstring>
#include <memory>

#include "ext/xxhash.h"
#include "Common/Common.h"
#include "Common/Data/Convert/ColorConv.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Profiler/Profiler.h"
#include "Common/System/OSD.h"
#include "Common/GPU/OpenGL/GLRenderManager.h"
#include "Common/TimeUtil.h"
#include "Core/Config.h"

#include "GPU/GPU.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"
#include "GPU/GPUDefinitions.h"
#include "GPU/GLES/TextureCacheGLES.h"
#include "GPU/GLES/FramebufferManagerGLES.h"
#include "GPU/Common/AsyncTextureDecode.h"
#include "GPU/Common/TextureShaderCommon.h"
#include "GPU/Common/DrawEngineCommon.h"

TextureCacheGLES::TextureCacheGLES(Draw::DrawContext *draw, Draw2D *draw2D)
	: TextureCacheCommon(draw, draw2D) {
	render_ = (GLRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);

	nextTexture_ = nullptr;
}

TextureCacheGLES::~TextureCacheGLES() {
	Clear(true);
}

void TextureCacheGLES::SetFramebufferManager(FramebufferManagerGLES *fbManager) {
	framebufferManagerGL_ = fbManager;
	framebufferManager_ = fbManager;
}

void TextureCacheGLES::ReleaseTexture(TexCacheEntry *entry, bool delete_them) {
	if (delete_them) {
		if (entry->textureName) {
			render_->DeleteTexture(entry->textureName);
		}
	}
	entry->textureName = nullptr;
}

void TextureCacheGLES::Clear(bool delete_them) {
	TextureCacheCommon::Clear(delete_them);
}

static Draw::DataFormat getClutDestFormat(GEPaletteFormat format) {
	switch (format) {
	case GE_CMODE_16BIT_ABGR4444:
		return Draw::DataFormat::R4G4B4A4_UNORM_PACK16;
	case GE_CMODE_16BIT_ABGR5551:
		return Draw::DataFormat::R5G5B5A1_UNORM_PACK16;
	case GE_CMODE_16BIT_BGR5650:
		return Draw::DataFormat::R5G6B5_UNORM_PACK16;
	case GE_CMODE_32BIT_ABGR8888:
		return Draw::DataFormat::R8G8B8A8_UNORM;
	}
	return Draw::DataFormat::UNDEFINED;
}

static const GLuint MinFiltGL[8] = {
	GL_NEAREST,
	GL_LINEAR,
	GL_NEAREST,
	GL_LINEAR,
	GL_NEAREST_MIPMAP_NEAREST,
	GL_LINEAR_MIPMAP_NEAREST,
	GL_NEAREST_MIPMAP_LINEAR,
	GL_LINEAR_MIPMAP_LINEAR,
};

static const GLuint MagFiltGL[2] = {
	GL_NEAREST,
	GL_LINEAR
};

void TextureCacheGLES::ApplySamplingParams(const SamplerCacheKey &key) {
	if (gstate_c.Use(GPU_USE_TEXTURE_LOD_CONTROL)) {
		float minLod = (float)key.minLevel / 256.0f;
		float maxLod = (float)key.maxLevel / 256.0f;
		float lodBias = (float)key.lodBias / 256.0f;
		render_->SetTextureLod(0, minLod, maxLod, lodBias);
	}

	float aniso = 0.0f;
	int minKey = ((int)key.mipEnable << 2) | ((int)key.mipFilt << 1) | ((int)key.minFilt);
	render_->SetTextureSampler(0,
		key.sClamp ? GL_CLAMP_TO_EDGE : GL_REPEAT, key.tClamp ? GL_CLAMP_TO_EDGE : GL_REPEAT,
		key.magFilt ? GL_LINEAR : GL_NEAREST, MinFiltGL[minKey], aniso);
}

static void ConvertColors(void *dstBuf, const void *srcBuf, Draw::DataFormat dstFmt, int numPixels) {
	const u32 *src = (const u32 *)srcBuf;
	u32 *dst = (u32 *)dstBuf;
	switch (dstFmt) {
	case Draw::DataFormat::R4G4B4A4_UNORM_PACK16:
		ConvertRGBA4444ToABGR4444((u16 *)dst, (const u16 *)src, numPixels);
		break;
	// Final Fantasy 2 uses this heavily in animated textures.
	case Draw::DataFormat::R5G5B5A1_UNORM_PACK16:
		ConvertRGBA5551ToABGR1555((u16 *)dst, (const u16 *)src, numPixels);
		break;
	case Draw::DataFormat::R5G6B5_UNORM_PACK16:
		ConvertRGB565ToBGR565((u16 *)dst, (const u16 *)src, numPixels);
		break;
	default:
		// No need to convert RGBA8888, right order already
		if (dst != src)
			memcpy(dst, src, numPixels * sizeof(u32));
		break;
	}
}

void TextureCacheGLES::StartFrame() {
	TextureCacheCommon::StartFrame();

	GLRenderManager *renderManager = (GLRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
	if (!lowMemoryMode_ && renderManager->SawOutOfMemory()) {
		lowMemoryMode_ = true;
		decimationCounter_ = 0;

		auto err = GetI18NCategory(I18NCat::ERRORS);
		if (standardScaleFactor_ > 1) {
			g_OSD.Show(OSDType::MESSAGE_WARNING, err->T("Warning: Video memory FULL, reducing upscaling and switching to slow caching mode"), 2.0f);
		} else {
			g_OSD.Show(OSDType::MESSAGE_WARNING, err->T("Warning: Video memory FULL, switching to slow caching mode"), 2.0f);
		}
	}
}

void TextureCacheGLES::UpdateCurrentClut(GEPaletteFormat clutFormat, u32 clutBase, bool clutIndexIsSimple) {
	const u32 clutBaseBytes = clutFormat == GE_CMODE_32BIT_ABGR8888 ? (clutBase * sizeof(u32)) : (clutBase * sizeof(u16));
	// Technically, these extra bytes weren't loaded, but hopefully it was loaded earlier.
	// If not, we're going to hash random data, which hopefully doesn't cause a performance issue.
	//
	// TODO: Actually, this seems like a hack.  The game can upload part of a CLUT and reference other data.
	// clutTotalBytes_ is the last amount uploaded.  We should hash clutMaxBytes_, but this will often hash
	// unrelated old entries for small palettes.
	// Adding clutBaseBytes may just be mitigating this for some usage patterns.
	const u32 clutExtendedBytes = std::min(clutTotalBytes_ + clutBaseBytes, clutMaxBytes_);

	if (replacer_.Enabled())
		clutHash_ = XXH32((const char *)clutBufRaw_, clutExtendedBytes, 0xC0108888);
	else
		clutHash_ = XXH3_64bits((const char *)clutBufRaw_, clutExtendedBytes) & 0xFFFFFFFF;

	// Avoid a copy when we don't need to convert colors.
	if (clutFormat != GE_CMODE_32BIT_ABGR8888) {
		const int numColors = clutFormat == GE_CMODE_32BIT_ABGR8888 ? (clutMaxBytes_ / sizeof(u32)) : (clutMaxBytes_ / sizeof(u16));
		ConvertColors(clutBufConverted_, clutBufRaw_, getClutDestFormat(clutFormat), numColors);
		clutBuf_ = clutBufConverted_;
	} else {
		clutBuf_ = clutBufRaw_;
	}

	// Special optimization: fonts typically draw clut4 with just alpha values in a single color.
	clutAlphaLinear_ = false;
	clutAlphaLinearColor_ = 0;
	if (clutFormat == GE_CMODE_16BIT_ABGR4444 && clutIndexIsSimple) {
		const u16_le *clut = GetCurrentClut<u16_le>();
		clutAlphaLinear_ = true;
		clutAlphaLinearColor_ = clut[15] & 0xFFF0;
		for (int i = 0; i < 16; ++i) {
			u16 step = clutAlphaLinearColor_ | i;
			if (clut[i] != step) {
				clutAlphaLinear_ = false;
				break;
			}
		}
	}

	clutLastFormat_ = gstate.clutformat;
}

void TextureCacheGLES::BindTexture(TexCacheEntry *entry) {
	if (!entry) {
		render_->BindTexture(0, nullptr);
		lastBoundTexture = nullptr;
		return;
	}
	if (entry->textureName != lastBoundTexture) {
		render_->BindTexture(0, entry->textureName);
		lastBoundTexture = entry->textureName;
	}
	int maxLevel = (entry->status & TexCacheEntry::STATUS_NO_MIPS) ? 0 : entry->maxLevel;
	SamplerCacheKey samplerKey = GetSamplingParams(maxLevel, entry);
	ApplySamplingParams(samplerKey);
	gstate_c.SetUseShaderDepal(ShaderDepalMode::OFF);
}

void TextureCacheGLES::Unbind() {
	render_->BindTexture(TEX_SLOT_PSP_TEXTURE, nullptr);
	ForgetLastTexture();
}

void TextureCacheGLES::BindAsClutTexture(Draw::Texture *tex, bool smooth) {
	GLRTexture *glrTex = (GLRTexture *)draw_->GetNativeObject(Draw::NativeObject::TEXTURE_VIEW, tex);
	render_->BindTexture(TEX_SLOT_CLUT, glrTex);
	render_->SetTextureSampler(TEX_SLOT_CLUT, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, smooth ? GL_LINEAR : GL_NEAREST, smooth ? GL_LINEAR : GL_NEAREST, 0.0f);
}

void TextureCacheGLES::BuildTexture(TexCacheEntry *const entry) {
	BuildTexturePlan plan;
	if (!PrepareBuildTexture(plan, entry)) {
		// We're screwed?
		return;
	}

	_assert_(!entry->textureName);

	// GLES2 doesn't have support for a "Max lod" which is critical as PSP games often
	// don't specify mips all the way down. As a result, we either need to manually generate
	// the bottom few levels or rely on OpenGL's autogen mipmaps instead, which might not
	// be as good quality as the game's own (might even be better in some cases though).

	int tw = plan.createW;
	int th = plan.createH;

	Draw::DataFormat dstFmt = GetDestFormat(GETextureFormat(entry->format), gstate.getClutPaletteFormat());
	if (plan.doReplace) {
		plan.replaced->GetSize(plan.baseLevelSrc, &tw, &th);
		dstFmt = plan.replaced->Format();
	} else if (plan.scaleFactor > 1 || plan.saveTexture) {
		dstFmt = Draw::DataFormat::R8G8B8A8_UNORM;
	} else if (plan.decodeToClut8) {
		dstFmt = Draw::DataFormat::R8_UNORM;
	}

	if (plan.depth == 1) {
		entry->textureName = render_->CreateTexture(GL_TEXTURE_2D, tw, th, 1, plan.levelsToCreate);
	} else {
		entry->textureName = render_->CreateTexture(GL_TEXTURE_3D, tw, th, plan.depth, 1);
	}

	// Apply some additional compatibility checks.
	if (plan.levelsToLoad > 1) {
		// Avoid PowerVR driver bug
		if (plan.w > 1 && plan.h > 1 && !(plan.h > plan.w && draw_->GetBugs().Has(Draw::Bugs::PVR_GENMIPMAP_HEIGHT_GREATER))) {  // Really! only seems to fail if height > width
			// It's ok to generate mipmaps beyond the loaded levels.
		} else {
			plan.levelsToCreate = plan.levelsToLoad;
		}
	} 

	if (!gstate_c.Use(GPU_USE_TEXTURE_LOD_CONTROL)) {
		// If the mip chain is not full..
		if (plan.levelsToCreate != plan.maxPossibleLevels) {
			// We need to avoid creating mips at all, or generate them all - can't be incomplete
			// on this hardware (strict OpenGL rules).
			plan.levelsToCreate = 1;
			plan.levelsToLoad = 1;
			entry->status |= TexCacheEntry::STATUS_NO_MIPS;
		}
	}

	if (plan.depth == 1) {
		for (int i = 0; i < plan.levelsToLoad; i++) {
			int srcLevel = i == 0 ? plan.baseLevelSrc : i;

			int mipWidth;
			int mipHeight;
			plan.GetMipSize(i, &mipWidth, &mipHeight);

			u8 *data = nullptr;
			int stride = 0;
			int dataSize;

			bool bc = false;

			if (plan.doReplace) {
				int blockSize = 0;
				if (Draw::DataFormatIsBlockCompressed(plan.replaced->Format(), &blockSize)) {
					stride = mipWidth * 4;
					dataSize = plan.replaced->GetLevelDataSizeAfterCopy(i);
					bc = true;
				} else {
					int bpp = (int)Draw::DataFormatSizeInBytes(plan.replaced->Format());
					stride = mipWidth * bpp;
					dataSize = stride * mipHeight;
				}
			} else {
				int bpp = 0;
				if (plan.scaleFactor > 1) {
					bpp = 4;
				} else {
					bpp = (int)Draw::DataFormatSizeInBytes(dstFmt);
				}
				stride = mipWidth * bpp;
				dataSize = stride * mipHeight;
			}

			data = (u8 *)AllocateAlignedMemory(dataSize, 16);
			_assert_msg_(data != nullptr, "Failed to allocate aligned memory for texture level %d: %d bytes (%dx%d)", i, (int)dataSize, mipWidth, mipHeight);

			if (!data) {
				ERROR_LOG(Log::G3D, "Ran out of RAM trying to allocate a temporary texture upload buffer (%dx%d)", mipWidth, mipHeight);
				return;
			}

			LoadTextureLevel(*entry, data, dataSize, stride, plan, srcLevel, dstFmt, TexDecodeFlags::REVERSE_COLORS);

			// NOTE: TextureImage takes ownership of data, so we don't free it afterwards.
			render_->TextureImage(entry->textureName, i, mipWidth, mipHeight, 1, dstFmt, data, GLRAllocType::ALIGNED);
		}

		bool genMips = plan.levelsToCreate > plan.levelsToLoad;

		render_->FinalizeTexture(entry->textureName, plan.levelsToLoad, genMips);
	} else {
		int bpp = (int)Draw::DataFormatSizeInBytes(dstFmt);
		int stride = bpp * (plan.w * plan.scaleFactor);
		int levelStride = stride * (plan.h * plan.scaleFactor);

		size_t dataSize = levelStride * plan.depth;
		u8 *data = (u8 *)AllocateAlignedMemory(dataSize, 16);
		_assert_msg_(data != nullptr, "Failed to allocate aligned memory for 3d texture: %d bytes", (int)dataSize);
		memset(data, 0, levelStride * plan.depth);
		u8 *p = data;

		for (int i = 0; i < plan.depth; i++) {
			LoadTextureLevel(*entry, p, dataSize, stride, plan, i, dstFmt, TexDecodeFlags::REVERSE_COLORS);
			p += levelStride;
		}

		render_->TextureImage(entry->textureName, 0, plan.w * plan.scaleFactor, plan.h * plan.scaleFactor, plan.depth, dstFmt, data, GLRAllocType::ALIGNED);

		// Signal that we support depth textures so use it as one.
		entry->status |= TexCacheEntry::STATUS_3D;

		render_->FinalizeTexture(entry->textureName, 1, false);
	}

	if (plan.doReplace) {
		entry->SetAlphaStatus(TexCacheEntry::TexStatus(plan.replaced->AlphaStatus()));
	}
}

bool TextureCacheGLES::StartAsyncBuildTexture(TexCacheEntry *const entry) {
	if (!g_Config.bAsyncTextureDecode) {
		return false;
	}
	// STATUS_CHANGE_FREQUENT: background decode can not outrun a texture that is rewritten
	// every frame or two -- the display would perpetually lag behind by however many frames
	// decode takes, which is worse than today's synchronous one-frame-behind-at-worst
	// behavior, not just slower. STATUS_CLUT_GPU: separate depal path, not a plain decode.
	if (entry->status & (TexCacheEntry::STATUS_CHANGE_FREQUENT | TexCacheEntry::STATUS_CLUT_GPU)) {
		return false;
	}
	if (IsVideo(entry->addr)) {
		return false;
	}
	// Only take the async path for an entry's first-ever build. HandleTextureChange() has
	// already released any previous texture by the time we get here (see ApplyTexture), so a
	// change-rebuild would otherwise flash this entry's placeholder gray for a frame in place
	// of content that was already on screen -- visible and jarring, unlike the placeholder
	// flash on a texture's first appearance, which nothing was on screen to compare against.
	// The burst-of-newly-visible-textures stutter this feature targets is entirely first
	// builds, so falling back to the synchronous path here for rebuilds costs nothing but an
	// occasional single-frame hit for a texture that was already rare enough to not be
	// STATUS_CHANGE_FREQUENT.
	if (entry->numInvalidated > 0) {
		return false;
	}
	// TEMPORARY diagnostic for the async-decode gray-flash investigation -- remove once the
	// eviction-recreation hypothesis is confirmed or ruled out. Correlate against GRAYFLASH
	// evict log lines by addr: a matching addr shortly before this line means the entry was
	// evicted and recreated (numInvalidated reset to 0), not genuinely first-seen.
	WARN_LOG(Log::G3D, "GRAYFLASH asyncstart addr=%08x fmt=%d hasClut=%d curFrame=%d",
		entry->addr, entry->format, (entry->status & TexCacheEntry::STATUS_CLUT_VARIANTS) ? 1 : 0, gpuStats.numFlips);
	// Guarantees PrepareBuildTexture below can not end up with plan.scaleFactor != 1 for this
	// backend (hardwareScaling is never requested here, and lowMemoryMode_ only ever clamps
	// scaleFactor downward), which in turn guarantees PrepareBuildTexture's frame-accumulating
	// side effects (texelsScaledThisFrame_, STATUS_TO_SCALE) never fire. That matters because
	// if this function returns false below, the caller re-runs PrepareBuildTexture itself via
	// the normal synchronous BuildTexture() -- safe only because those side effects are all
	// gated behind scaleFactor != 1.
	if (standardScaleFactor_ != 1) {
		return false;
	}

	BuildTexturePlan plan;
	if (!PrepareBuildTexture(plan, entry)) {
		return false;
	}
	// doReplace/saveTexture/decodeToClut8 all drive further TextureCacheCommon member state
	// (replacer_, scaler_) that is not safe to touch concurrently with a worker thread; 3D
	// textures need the separate depth-layer upload path BuildTexture has. None of these are
	// worth widening the async path for yet -- see the plan doc for the full reasoning.
	_dbg_assert_(plan.scaleFactor == 1);
	if (plan.depth != 1 || plan.saveTexture || plan.doReplace || plan.decodeToClut8) {
		return false;
	}

	_assert_(!entry->textureName);

	Draw::DataFormat dstFmt = GetDestFormat(GETextureFormat(entry->format), gstate.getClutPaletteFormat());
	int bpp = (int)Draw::DataFormatSizeInBytes(dstFmt);

	entry->textureName = render_->CreateTexture(GL_TEXTURE_2D, plan.createW, plan.createH, 1, plan.levelsToCreate);

	// Placeholder: a flat level 0, mip chain auto-generated from it. Wholesale-replaced once
	// the real decode finishes -- see FinishAsyncBuildTexture. Keeps ApplyTexture's draw calls
	// valid for this entry in the meantime without needing to touch PSP memory at all.
	size_t placeholderSize = (size_t)plan.createW * plan.createH * bpp;
	u8 *placeholder = new u8[placeholderSize];
	memset(placeholder, 0x80, placeholderSize);
	render_->TextureImage(entry->textureName, 0, plan.createW, plan.createH, 1, dstFmt, placeholder, GLRAllocType::NEW);
	render_->FinalizeTexture(entry->textureName, 1, true);

	auto pending = std::make_unique<PendingTextureDecode>();
	pending->format = GETextureFormat(entry->format);
	pending->clutFormat = gstate.getClutPaletteFormat();
	pending->dstFmt = dstFmt;
	pending->tw = plan.createW;
	pending->th = plan.createH;
	pending->levelsToLoad = plan.levelsToLoad;
	pending->levelsToCreate = plan.levelsToCreate;

	TexDecodeFlags flags = TexDecodeFlags::REVERSE_COLORS;
	if (!gstate_c.Use(GPU_USE_16BIT_FORMATS) || dstFmt == Draw::DataFormat::R8G8B8A8_UNORM) {
		flags |= TexDecodeFlags::EXPAND32;
	}
	pending->flags = flags;

	pending->gs.swizzled = gstate.isTextureSwizzled();
	pending->gs.clutSharedForMipmaps = gstate.isClutSharedForMipmaps();
	pending->gs.clutIndexStartPos = gstate.getClutIndexStartPos();
	pending->gs.clutIndexShift = gstate.getClutIndexShift();
	pending->gs.clutIndexMask = gstate.getClutIndexMask();
	pending->gs.clutLoadBlocks = gstate.getClutLoadBlocks();
	pending->gs.clutPaletteFormat = (GEPaletteFormat)gstate.getClutPaletteFormat();
	pending->gs.clutAlphaLinear = clutAlphaLinear_;
	pending->gs.clutAlphaLinearColor = clutAlphaLinearColor_;

	// Snapshot the actual palette contents now -- clutBuf_/clutBufRaw_ get rewritten by
	// LoadClut() as soon as the game issues its next LOADCLUT, which can easily happen before
	// this decode reaches the worker thread. See AsyncTextureDecode.h for the full reasoning.
	pending->clutBufSnapshot.assign(clutBuf_, clutBuf_ + 512);
	pending->clutBufRawSnapshot.assign(clutBufRaw_, clutBufRaw_ + 512);

	GETextureFormat tfmt = (GETextureFormat)entry->format;
	pending->levels.resize(plan.levelsToLoad);
	for (int i = 0; i < plan.levelsToLoad; i++) {
		int srcLevel = i == 0 ? plan.baseLevelSrc : i;
		PendingLevelSnapshot &level = pending->levels[i];
		level.texaddr = gstate.getTextureAddress(srcLevel);
		level.bufw = GetTextureBufw(srcLevel, level.texaddr, tfmt);
		level.w = gstate.getTextureWidth(srcLevel);
		level.h = gstate.getTextureHeight(srcLevel);
		// DoUnswizzleTex16 always reads in 8-row blocks (see UnswizzleFromMem's byc rounding
		// and its matching (h + 7) & ~7 destination sizing in DecodeTextureLevel) -- for a
		// swizzled texture whose height isn't a multiple of 8, it reads a few rows past the
		// end of this snapshot. That's harmless against the sync path's live PSP RAM, but a
		// real heap over-read against this exactly-sized buffer, so round up here to match.
		const int readHeight = pending->gs.swizzled ? ((level.h + 7) & ~7) : level.h;
		const uint32_t byteSize = (textureBitsPerPixel[tfmt] * level.bufw * readHeight) / 8;
		level.srcData.resize(byteSize);
		memcpy(level.srcData.data(), Memory::GetPointer(level.texaddr), byteSize);
	}

	entry->pendingDecode = std::move(pending);
	entry->pendingDecode->Start(this);
	entry->status |= TexCacheEntry::STATUS_DECODE_PENDING;
	return true;
}

void TextureCacheGLES::PollAsyncBuildTexture(TexCacheEntry *const entry) {
	if (!entry->pendingDecode || !entry->pendingDecode->Poll()) {
		return;
	}
	FinishAsyncBuildTexture(entry);
}

void TextureCacheGLES::CancelAsyncBuildTexture(TexCacheEntry *const entry) {
	// pendingDecode's destructor blocks briefly if the background task hasn't finished yet.
	entry->pendingDecode.reset();
	if (entry->textureName) {
		render_->DeleteTexture(entry->textureName);
		entry->textureName = nullptr;
	}
	entry->status &= ~TexCacheEntry::STATUS_DECODE_PENDING;
}

void TextureCacheGLES::FinishAsyncBuildTexture(TexCacheEntry *const entry) {
	PendingTextureDecode *pending = entry->pendingDecode.get();

	GLRTexture *placeholderTexture = entry->textureName;
	entry->textureName = render_->CreateTexture(GL_TEXTURE_2D, pending->tw, pending->th, 1, pending->levelsToCreate);

	for (int i = 0; i < pending->levelsToLoad; i++) {
		// Must match the width Run() actually decoded each level's row pitch against
		// (PendingLevelSnapshot::w, the PSP-declared per-level width), not a power-of-two
		// halving of the base level -- real content's mip chain isn't always a strict halving,
		// and a mismatch here means GL reads the upload buffer's scanlines at the wrong stride.
		int mipWidth = pending->levels[i].w;
		int mipHeight = pending->levels[i].h;
		std::vector<u8> &decoded = pending->decodedLevels[i];
		u8 *data = new u8[decoded.size()];
		memcpy(data, decoded.data(), decoded.size());
		render_->TextureImage(entry->textureName, i, mipWidth, mipHeight, 1, pending->dstFmt, data, GLRAllocType::NEW);
		entry->SetAlphaStatus(pending->alphaResults[i], i);
	}

	bool genMips = pending->levelsToCreate > pending->levelsToLoad;
	render_->FinalizeTexture(entry->textureName, pending->levelsToLoad, genMips);

	if (placeholderTexture) {
		render_->DeleteTexture(placeholderTexture);
	}

	entry->status &= ~TexCacheEntry::STATUS_DECODE_PENDING;
	entry->pendingDecode.reset();
}

Draw::DataFormat TextureCacheGLES::GetDestFormat(GETextureFormat format, GEPaletteFormat clutFormat) {
	switch (format) {
	case GE_TFMT_CLUT4:
	case GE_TFMT_CLUT8:
	case GE_TFMT_CLUT16:
	case GE_TFMT_CLUT32:
		return getClutDestFormat(clutFormat);
	case GE_TFMT_4444:
		return Draw::DataFormat::R4G4B4A4_UNORM_PACK16;
	case GE_TFMT_5551:
		return Draw::DataFormat::R5G5B5A1_UNORM_PACK16;
	case GE_TFMT_5650:
		return Draw::DataFormat::R5G6B5_UNORM_PACK16;
	case GE_TFMT_8888:
	case GE_TFMT_DXT1:
	case GE_TFMT_DXT3:
	case GE_TFMT_DXT5:
	default:
		return Draw::DataFormat::R8G8B8A8_UNORM;
	}
}

bool TextureCacheGLES::GetCurrentTextureDebug(GPUDebugBuffer &buffer, int level, bool *isFramebuffer) {
	ForgetLastTexture();
	SetTexture();
	if (!nextTexture_) {
		return GetCurrentFramebufferTextureDebug(buffer, isFramebuffer);
	}

	// Apply texture may need to rebuild the texture if we're about to render, or bind a framebuffer.
	TexCacheEntry *entry = nextTexture_;
	// We might need a render pass to set the sampling params, unfortunately.  Otherwise BuildTexture may crash.
	framebufferManagerGL_->RebindFramebuffer("RebindFramebuffer - GetCurrentTextureDebug");
	ApplyTexture(false);

	GLRenderManager *renderManager = (GLRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);

	// Not a framebuffer, so let's assume these are right.
	// TODO: But they may definitely not be, if the texture was scaled.
	int w = gstate.getTextureWidth(level);
	int h = gstate.getTextureHeight(level);

	bool result = entry->textureName != nullptr;
	if (result) {
		buffer.Allocate(w, h, GE_FORMAT_8888, false);
		renderManager->CopyImageToMemorySync(entry->textureName, level, 0, 0, w, h, Draw::DataFormat::R8G8B8A8_UNORM, (uint8_t *)buffer.GetData(), w, "GetCurrentTextureDebug");
	} else {
		ERROR_LOG(Log::G3D, "Failed to get debug texture: texture is null");
	}
	gstate_c.Dirty(DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS);
	framebufferManager_->RebindFramebuffer("RebindFramebuffer - GetCurrentTextureDebug");

	*isFramebuffer = false;
	return result;
}

void TextureCacheGLES::DeviceLost() {
	textureShaderCache_->DeviceLost();
	Clear(false);
	draw_ = nullptr;
	render_ = nullptr;
}

void TextureCacheGLES::DeviceRestore(Draw::DrawContext *draw) {
	draw_ = draw;
	render_ = (GLRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
	textureShaderCache_->DeviceRestore(draw);
}

void *TextureCacheGLES::GetNativeTextureView(const TexCacheEntry *entry, bool flat) const {
	GLRTexture *tex = entry->textureName;
	return (void *)tex;
}
