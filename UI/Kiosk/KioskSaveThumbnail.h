#pragma once

#include "Common/File/Path.h"

class UIContext;
struct Bounds;
namespace Draw {
class DrawContext;
class Texture;
}

namespace Kiosk {

constexpr int kSaveThumbNativeW = 960;
constexpr int kSaveThumbNativeH = 544;
constexpr float kSaveThumbScale = 0.25f;
constexpr int kSaveThumbW = (int)(kSaveThumbNativeW * kSaveThumbScale);
constexpr int kSaveThumbH = (int)(kSaveThumbNativeH * kSaveThumbScale);

}  // namespace Kiosk

// Decodes a save-state screenshot JPEG on first Draw(), downsamples it to thumbnail
// size, and uploads it as a texture; kept alive for the lifetime of the owning
// KioskSaveSlot so repeated Draw() calls don't redecode it.
class KioskSaveThumbnail {
public:
	explicit KioskSaveThumbnail(const Path &path);
	~KioskSaveThumbnail();

	void Draw(UIContext &dc, const Bounds &bounds);
	void DeviceLost();

private:
	void Load(Draw::DrawContext *draw);

	Path path_;
	Draw::Texture *texture_ = nullptr;
	bool loaded_ = false;
};
