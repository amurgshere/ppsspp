#include "ppsspp_config.h"

#include <algorithm>

#include "Common/Log.h"
#include "Common/GPU/thin3d.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/Render/Text/draw_text_switch.h"

#if PPSSPP_PLATFORM(SWITCH)

TextDrawerSwitch::TextDrawerSwitch(Draw::DrawContext *draw) : TextDrawer(draw) {
	dpiScale_ = CalculateDPIScale();
	if (InitPlAndFreeType())
		INFO_LOG(Log::G3D, "TextDrawerSwitch: loaded system shared font (%d bytes)", (int)sharedFont_.size);
}

bool TextDrawerSwitch::InitPlAndFreeType() {
	Result rc = plInitialize(PlServiceType_User);
	if (R_FAILED(rc)) {
		ERROR_LOG(Log::G3D, "TextDrawerSwitch: plInitialize failed: 0x%x", rc);
		return false;
	}
	plInitialized_ = true;

	rc = plGetSharedFontByType(&sharedFont_, PlSharedFontType_Standard);
	if (R_FAILED(rc)) {
		ERROR_LOG(Log::G3D, "TextDrawerSwitch: plGetSharedFontByType failed: 0x%x", rc);
		return false;
	}

	if (FT_Init_FreeType(&library_)) {
		ERROR_LOG(Log::G3D, "TextDrawerSwitch: FT_Init_FreeType failed");
		return false;
	}

	if (FT_New_Memory_Face(library_, (const FT_Byte *)sharedFont_.address, (FT_Long)sharedFont_.size, 0, &face_)) {
		ERROR_LOG(Log::G3D, "TextDrawerSwitch: FT_New_Memory_Face failed");
		face_ = nullptr;
		return false;
	}

	return true;
}

TextDrawerSwitch::~TextDrawerSwitch() {
	ClearCache();
	ClearFonts();

	if (library_) {
		FT_Done_FreeType(library_);
		library_ = nullptr;
	}
	if (plInitialized_) {
		plExit();
		plInitialized_ = false;
	}
}

bool TextDrawerSwitch::IsReady() const {
	return face_ != nullptr;
}

void TextDrawerSwitch::SetOrCreateFont(const FontStyle &style) {
	// The Switch's shared font is a single regular-weight face - there's no
	// separate font file per family/style to load, so just remember the style
	// (used for the size) for the next Measure/Draw call.
	fontStyle_ = style;
}

int TextDrawerSwitch::PixelHeightForCurrentStyle() const {
	// The Switch's shared font is a regular-width sans-serif, unlike the
	// bitmap atlas font it replaced (Roboto Condensed) - at the same nominal
	// point size it renders visibly larger/wider, overflowing UI elements
	// sized for the old, narrower font. This extra factor brings it back
	// in line; tune further here if things still don't fit.
	constexpr float kSwitchFontSizeAdjust = 0.82f;
	return std::max(1, (int)(fontStyle_.sizePts / dpiScale_ * 1.25f * kSwitchFontSizeAdjust));
}

void TextDrawerSwitch::WriteAlphaPixel(std::vector<uint8_t> &bitmapData, Draw::DataFormat texFormat, int stride, int x, int y, uint8_t alpha) {
	switch (texFormat) {
	case Draw::DataFormat::R8_UNORM:
		bitmapData[stride * y + x] = alpha;
		break;
	case Draw::DataFormat::R4G4B4A4_UNORM_PACK16:
	case Draw::DataFormat::B4G4R4A4_UNORM_PACK16:
	{
		uint16_t *bitmapData16 = (uint16_t *)&bitmapData[0];
		bitmapData16[stride * y + x] = AlphaToPremul4444(alpha);
		break;
	}
	default:
	{
		uint32_t *bitmapData32 = (uint32_t *)&bitmapData[0];
		bitmapData32[stride * y + x] = AlphaToPremul8888(alpha);
		break;
	}
	}
}

std::vector<TextDrawerSwitch::LineMetrics> TextDrawerSwitch::MeasureLines(std::string_view str) {
	std::vector<LineMetrics> lines(1);

	int penX = 0;
	UTF8 utf8(str);
	while (!utf8.end()) {
		uint32_t codepoint = utf8.next();
		if (codepoint == '\n') {
			lines.back().width = penX;
			penX = 0;
			lines.emplace_back();
			continue;
		}
		if (FT_Load_Char(face_, codepoint, FT_LOAD_RENDER)) {
			continue;
		}
		FT_GlyphSlot slot = face_->glyph;
		lines.back().ascent = std::max(lines.back().ascent, (int)slot->bitmap_top);
		lines.back().descent = std::max(lines.back().descent, (int)slot->bitmap.rows - (int)slot->bitmap_top);
		penX += (int)(slot->advance.x >> 6);
	}
	lines.back().width = penX;
	return lines;
}

void TextDrawerSwitch::RasterizeLines(std::string_view str, const std::vector<int> &lineBaselineY, const std::vector<int> &lineOffsetX, int totalHeight,
		std::vector<uint8_t> &bitmapData, Draw::DataFormat texFormat, int stride) {
	int lineIndex = 0;
	int penX = 0;
	UTF8 utf8(str);
	while (!utf8.end()) {
		uint32_t codepoint = utf8.next();
		if (codepoint == '\n') {
			penX = 0;
			lineIndex++;
			continue;
		}
		if (FT_Load_Char(face_, codepoint, FT_LOAD_RENDER)) {
			continue;
		}
		FT_GlyphSlot slot = face_->glyph;
		const FT_Bitmap &bmp = slot->bitmap;
		int originX = lineOffsetX[lineIndex] + penX + slot->bitmap_left;
		int originY = lineBaselineY[lineIndex] - slot->bitmap_top;
		for (unsigned int gy = 0; gy < bmp.rows; gy++) {
			int py = originY + gy;
			if (py < 0 || py >= totalHeight)
				continue;
			for (unsigned int gx = 0; gx < bmp.width; gx++) {
				int px = originX + gx;
				if (px < 0 || px >= stride)
					continue;
				uint8_t alpha = bmp.buffer[gy * bmp.pitch + gx];
				if (alpha)
					WriteAlphaPixel(bitmapData, texFormat, stride, px, py, alpha);
			}
		}
		penX += (int)(slot->advance.x >> 6);
	}
}

// Font metrics (ascender/descender/height) don't reliably bound the actual
// rendered glyph bitmaps at small hinted pixel sizes - the Switch's shared
// font in particular renders some descenders (g, p, y) deeper than its own
// nominal descender metric reports. So instead of trusting the metrics for
// vertical placement, this always walks the string twice: once to measure
// each line's *actual* rendered ascent/descent from the real glyph bitmaps,
// then again to blit using those measured, guaranteed-to-fit line heights.
void TextDrawerSwitch::LayoutString(std::string_view str, int pixelHeight, bool centerLines, std::vector<uint8_t> *bitmapData, Draw::DataFormat texFormat, int stride, int *outW, int *outH) {
	FT_Set_Pixel_Sizes(face_, 0, pixelHeight);

	std::vector<LineMetrics> lines = MeasureLines(str);

	// Per-line ascent/descent alone packs multi-line strings with zero leading between
	// lines, unlike a font renderer's normal line spacing - fine for a single line, but
	// makes consecutive lines of a multi-line string look cramped. Add a proportional
	// gap between lines only (line count - 1 gaps), leaving single-line height untouched.
	const int lineGap = std::max(1, pixelHeight / 6);

	std::vector<int> lineBaselineY(lines.size());
	int maxLineWidth = 0;
	int y = 0;
	for (size_t i = 0; i < lines.size(); i++) {
		if (i > 0) {
			y += lineGap;
		}
		lineBaselineY[i] = y + lines[i].ascent;
		y += lines[i].ascent + lines[i].descent;
		maxLineWidth = std::max(maxLineWidth, lines[i].width);
	}

	const int totalWidth = std::max(1, maxLineWidth);
	const int totalHeight = std::max(1, y);
	*outW = totalWidth;
	*outH = totalHeight;

	if (!bitmapData) {
		return;
	}

	std::vector<int> lineOffsetX(lines.size(), 0);
	if (centerLines) {
		for (size_t i = 0; i < lines.size(); i++) {
			lineOffsetX[i] = (totalWidth - lines[i].width) / 2;
		}
	}

	RasterizeLines(str, lineBaselineY, lineOffsetX, totalHeight, *bitmapData, texFormat, stride);
}

void TextDrawerSwitch::MeasureStringInternal(std::string_view str, float *w, float *h) {
	if (!face_) {
		*w = 1.0f;
		*h = 1.0f;
		return;
	}

	int width = 0, height = 0;
	LayoutString(str, PixelHeightForCurrentStyle(), false, nullptr, Draw::DataFormat::R8_UNORM, 0, &width, &height);
	*w = (float)width;
	*h = (float)height;
}

bool TextDrawerSwitch::DrawStringBitmap(std::vector<uint8_t> &bitmapData, TextStringEntry &entry, Draw::DataFormat texFormat, std::string_view str, int align, bool fullColor) {
	_dbg_assert_(!fullColor);

	if (str.empty() || !face_) {
		bitmapData.clear();
		return false;
	}

	const int pixelHeight = PixelHeightForCurrentStyle();
	const bool centerLines = (align & ALIGN_HCENTER) != 0;
	int width = 0, height = 0;
	LayoutString(str, pixelHeight, centerLines, nullptr, texFormat, 0, &width, &height);

	// Round the stride up for texture upload alignment, matching the other backends.
	const int stride = (width + 3) & ~3;

	entry.texture = nullptr;
	entry.bmWidth = entry.width = stride;
	entry.bmHeight = entry.height = height;
	entry.lastUsedFrame = frameCount_;

	size_t bytesPerPixel = 1;
	if (texFormat == Draw::DataFormat::R4G4B4A4_UNORM_PACK16 || texFormat == Draw::DataFormat::B4G4R4A4_UNORM_PACK16)
		bytesPerPixel = 2;
	else if (texFormat != Draw::DataFormat::R8_UNORM)
		bytesPerPixel = 4;

	bitmapData.assign((size_t)stride * height * bytesPerPixel, 0);

	int unusedW = 0, unusedH = 0;
	LayoutString(str, pixelHeight, centerLines, &bitmapData, texFormat, stride, &unusedW, &unusedH);
	return true;
}

void TextDrawerSwitch::ClearFonts() {
	if (face_) {
		FT_Done_Face(face_);
		face_ = nullptr;
	}
}

#endif
