#include <algorithm>
#include <cstdint>
#include <vector>

#include "UI/Kiosk/KioskSaveThumbnail.h"
#include "UI/Kiosk/KioskCommon.h"

#include "Common/UI/Context.h"
#include "Common/UI/View.h"
#include "Common/Render/DrawBuffer.h"
#include "Common/GPU/thin3d.h"
#include "Common/File/FileUtil.h"
#include "ext/jpge/jpgd.h"

namespace {

void BoxDownsampleRGBA8(const uint32_t *src, int srcW, int srcH, uint32_t *dst, int dstW, int dstH) {
	for (int y = 0; y < dstH; y++) {
		int sy0 = y * srcH / dstH;
		int sy1 = std::max(sy0 + 1, (y + 1) * srcH / dstH);
		for (int x = 0; x < dstW; x++) {
			int sx0 = x * srcW / dstW;
			int sx1 = std::max(sx0 + 1, (x + 1) * srcW / dstW);
			uint32_t r = 0, g = 0, b = 0, a = 0;
			int count = 0;
			for (int sy = sy0; sy < sy1; sy++) {
				const uint32_t *row = src + sy * srcW;
				for (int sx = sx0; sx < sx1; sx++) {
					uint32_t px = row[sx];
					r += px & 0xFF;
					g += (px >> 8) & 0xFF;
					b += (px >> 16) & 0xFF;
					a += (px >> 24) & 0xFF;
					count++;
				}
			}
			if (count == 0)
				count = 1;
			dst[y * dstW + x] = (r / count) | ((g / count) << 8) | ((b / count) << 16) | ((a / count) << 24);
		}
	}
}

}  // namespace

KioskSaveThumbnail::KioskSaveThumbnail(const Path &path) : path_(path) {}

KioskSaveThumbnail::~KioskSaveThumbnail() {
	if (texture_)
		texture_->Release();
}

void KioskSaveThumbnail::Draw(UIContext &dc, const Bounds &bounds) {
	if (!loaded_)
		Load(dc.GetDrawContext());
	if (texture_) {
		Kiosk::TextureBindScope texScope(dc, texture_);
		dc.Draw()->Rect(bounds.x, bounds.y, bounds.w, bounds.h, 0xFFFFFFFF);
	} else {
		dc.FillRect(UI::Drawable(0xFF000000), bounds);
	}
}

void KioskSaveThumbnail::DeviceLost() {
	if (texture_) {
		texture_->Release();
		texture_ = nullptr;
	}
	loaded_ = false;
}

void KioskSaveThumbnail::Load(Draw::DrawContext *draw) {
	loaded_ = true;

	std::string data;
	if (!File::ReadBinaryFileToString(path_, &data))
		return;

	int w = 0, h = 0, comps = 0;
	unsigned char *rgba = jpgd::decompress_jpeg_image_from_memory((const unsigned char *)data.data(), (int)data.size(), &w, &h, &comps, 4);
	if (!rgba)
		return;

	std::vector<uint32_t> resized((size_t)Kiosk::kSaveThumbW * Kiosk::kSaveThumbH);
	BoxDownsampleRGBA8((const uint32_t *)rgba, w, h, resized.data(), Kiosk::kSaveThumbW, Kiosk::kSaveThumbH);
	free(rgba);

	Draw::TextureDesc desc{};
	desc.type = Draw::TextureType::LINEAR2D;
	desc.format = Draw::DataFormat::R8G8B8A8_UNORM;
	desc.width = Kiosk::kSaveThumbW;
	desc.height = Kiosk::kSaveThumbH;
	desc.depth = 1;
	desc.mipLevels = 1;
	desc.generateMips = false;
	desc.tag = "KioskSaveThumb";
	desc.initData.push_back((const uint8_t *)resized.data());
	texture_ = draw->CreateTexture(desc);
}
