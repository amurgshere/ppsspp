#pragma once

#include "ppsspp_config.h"

#include "Common/Render/Text/draw_text.h"

#if PPSSPP_PLATFORM(SWITCH)

#include <ft2build.h>
#include FT_FREETYPE_H

#include <switch.h>

// Renders text using the Switch's system shared font (via libnx's pl service),
// which includes Japanese/CJK glyph coverage that the bundled Latin-only
// Roboto/Inconsolata TTFs used for the fallback bitmap-atlas path don't have.
class TextDrawerSwitch : public TextDrawer {
public:
	TextDrawerSwitch(Draw::DrawContext *draw);
	~TextDrawerSwitch();

	bool IsReady() const override;
	void SetOrCreateFont(const FontStyle &style) override;
	bool DrawStringBitmap(std::vector<uint8_t> &bitmapData, TextStringEntry &entry, Draw::DataFormat texFormat, std::string_view str, int align, bool fullColor) override;

protected:
	void MeasureStringInternal(std::string_view str, float *w, float *h) override;

	bool SupportsColorEmoji() const override { return false; }
	void ClearFonts() override;

private:
	// Initializes plInitialized_/library_/face_ in order, stopping (and leaving
	// the rest at their default-constructed nullptr/false state, which the
	// destructor already checks individually) at the first failure.
	bool InitPlAndFreeType();

	struct LineMetrics {
		int width = 0;
		int ascent = 1;
		int descent = 0;
	};

	int PixelHeightForCurrentStyle() const;
	// Lays out str (which may contain embedded '\n' line breaks) glyph-by-glyph.
	// Always computes the resulting width/height. If bitmapData is non-null, also
	// rasterizes into it (already sized/cleared by the caller) using texFormat.
	// centerLines requests each line be individually centered within the block's
	// overall width, matching Win32's DT_CENTER - without it, shorter wrapped
	// lines all start flush at column 0 and only the whole block gets centered
	// by the caller, so they'd visually look left-aligned relative to each other.
	void LayoutString(std::string_view str, int pixelHeight, bool centerLines, std::vector<uint8_t> *bitmapData, Draw::DataFormat texFormat, int stride, int *outW, int *outH);
	// First pass of LayoutString: measures each line's actual rendered ascent/
	// descent/width from the real glyph bitmaps (see LayoutString's comment
	// for why metrics alone aren't trustworthy here).
	std::vector<LineMetrics> MeasureLines(std::string_view str);
	// Second pass: blits glyphs into bitmapData using the baselines MeasureLines'
	// results imply, skipping pixels outside [0, totalHeight)/[0, stride).
	void RasterizeLines(std::string_view str, const std::vector<int> &lineBaselineY, const std::vector<int> &lineOffsetX, int totalHeight,
		std::vector<uint8_t> &bitmapData, Draw::DataFormat texFormat, int stride);
	void WriteAlphaPixel(std::vector<uint8_t> &bitmapData, Draw::DataFormat texFormat, int stride, int x, int y, uint8_t alpha);

	bool plInitialized_ = false;
	FT_Library library_ = nullptr;
	PlFontData sharedFont_{};
	FT_Face face_ = nullptr;
};

#endif
