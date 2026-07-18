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

#include "GPU/Common/AsyncTextureDecode.h"

#include "Common/GPU/thin3d.h"
#include "Common/StutterMonitor.h"
#include "Common/Thread/Waitable.h"

class TextureDecodeTask : public Task {
public:
	TextureDecodeTask(TextureCacheCommon *owner, PendingTextureDecode *decode, LimitedWaitable *w)
		: owner_(owner), decode_(decode), waitable_(w) {}

	TaskType Type() const override { return TaskType::CPU_COMPUTE; }
	TaskPriority Priority() const override { return TaskPriority::NORMAL; }

	void Run() override {
		decode_->Run(owner_);
		waitable_->Notify();
	}

private:
	TextureCacheCommon *owner_;
	PendingTextureDecode *decode_;
	LimitedWaitable *waitable_;
};

PendingTextureDecode::PendingTextureDecode() {
	waitable_ = new LimitedWaitable();
	StutterMonitor::AdjustPendingDecodeCount(1);
}

PendingTextureDecode::~PendingTextureDecode() {
	// Blocks briefly if the task is still running -- see class comment in the header for why
	// this is safe (the task only ever touches this object, never the owning TexCacheEntry).
	waitable_->WaitAndRelease();
	StutterMonitor::AdjustPendingDecodeCount(-1);
}

void PendingTextureDecode::Start(TextureCacheCommon *owner) {
	g_threadManager.EnqueueTask(new TextureDecodeTask(owner, this, waitable_));
}

bool PendingTextureDecode::Poll() {
	return state_.load(std::memory_order_acquire) == PendingDecodeState::DONE;
}

void PendingTextureDecode::DecodeAllLevels(TextureCacheCommon *owner, const std::vector<PendingLevelSnapshot> &levels,
		GETextureFormat format, GEPaletteFormat clutFormat, TexDecodeFlags flags, Draw::DataFormat dstFmt,
		const DecodeGStateSnapshot &gs, DecodeScratch &scratch,
		std::vector<std::vector<u8>> &outLevels, std::vector<CheckAlphaResult> &outAlpha) {
	outLevels.resize(levels.size());
	outAlpha.resize(levels.size());

	// Matches LoadTextureLevel's own stride choice for the non-scaled case (this path never
	// scales -- see BuildTexture's eligibility gate): tightly packed, dstFmt's own pixel size.
	int bpp = (int)Draw::DataFormatSizeInBytes(dstFmt);

	for (size_t i = 0; i < levels.size(); i++) {
		const PendingLevelSnapshot &level = levels[i];
		std::vector<u8> &out = outLevels[i];
		int outPitch = level.w * bpp;
		out.resize((size_t)outPitch * level.h);

		outAlpha[i] = owner->DecodeTextureLevel(out.data(), outPitch, format, clutFormat,
			level.srcData.data(), level.texaddr, level.w, level.h, (int)i, level.bufw, flags, gs, scratch);
	}
}

void PendingTextureDecode::Run(TextureCacheCommon *owner) {
	DecodeScratch scratch;
	scratch.unswizzleBuf = &unswizzleScratch_;
	scratch.expandClutBuf = expandClutScratch_.data();
	scratch.clutBuf = clutBufSnapshot.data();
	scratch.clutBufRaw = clutBufRawSnapshot.data();

	PendingTextureDecode::DecodeAllLevels(owner, levels, format, clutFormat, flags, dstFmt, gs, scratch, decodedLevels, alphaResults);

	state_.store(PendingDecodeState::DONE, std::memory_order_release);
}
