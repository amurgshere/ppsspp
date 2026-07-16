#pragma once

#include <cstdint>
#include <string>

#include "Common/UI/ViewGroup.h"
#include "Common/Render/TextureAtlas.h"
#include "Common/File/Path.h"
#include "Common/UI/Screen.h"
#include "UI/Kiosk/KioskHintBar.h"

class UIContext;
class GameSettingsScreen;
struct Bounds;
struct KeyInput;
namespace Draw {
class Texture;
}

namespace Kiosk {

// PSP icon0 dimensions, the native size of a game tile's artwork.
constexpr int kTileNativeW = 144;
constexpr int kTileNativeH = 80;
constexpr float kTileAspect = (float)kTileNativeW / (float)kTileNativeH;
constexpr float kTilesVisible = 3.5f;
constexpr float kTileGap = 24.0f;
constexpr float kHintBarThickness = 64.0f;

constexpr float kTileBorderThickness = 4.0f;
constexpr float kTileBorderGap = 6.0f;
constexpr float kTileBorderRadius = 10.0f;
constexpr float kFocusRingExtent = kTileBorderThickness + kTileBorderGap;

// GridLayout measures itself against the full width a ScrollView hands it,
// which doesn't reserve room for the ScrollView's own overlaid scrollbar -
// so a reflowing grid inside a vertical ScrollView needs this much margin on
// its scrolling side, or the last column can render underneath the bar.
constexpr float kScrollbarGutter = 24.0f;

constexpr double kHoldToInfoSeconds = 0.6;

// Some hint-bar atlas glyphs (L/R shoulder composite, Start) read as visibly
// smaller than the face-button glyphs (X/O/Square/Triangle) even at the same
// bounding-box size, so they get a size bump on top of the bar's base icon scale.
constexpr float kSmallGlyphIconScale = 1.75f;

constexpr int kMaxSaveSlots = 5;

// Icon fade-in speed multiplier: time-since-load (seconds) is multiplied by this before
// being fed to ease(), so a value of N means the fade completes in 1/N seconds.
constexpr double kIconFadeInSpeed = 2.0;

// ROM title text (Carousel's title above the tiles, All ROMs' selected-game
// name banner) is drawn 20% larger in landscape, which has the vertical room
// for it; portrait keeps the base size to avoid crowding its tighter layout.
constexpr float kLandscapeTitleScale = 1.2f;

// Ground-contact shadow band geometry, shared between KioskCarouselView's
// shadow rendering and KioskCarouselScreen's "Displaying XX of YY ROMs"
// caption, which sits just below the band. Expressed as percentages of the
// client height (screen height minus kHintBarThickness, i.e. excluding the
// hint bar) rather than fixed dp or tile-relative sizes, calibrated to land in
// the same place as the original fixed-dp layout did on a 1920x1080 window.
constexpr float kGradientShadowHeightPercent = 0.143420f;
constexpr float kGroundLinePositionPercent = 0.910855f;
constexpr float kGroundLineSolidHeightPercent = 0.023622f;
constexpr float kGroundLineSoftEdgePercent = 0.011811f;

struct GroundShadowBand {
	float lineY;
	float solidHeight;
	float softEdge;
	float bottom;
};
GroundShadowBand ComputeGroundShadowBand(float clientHeight);

UI::GridLayoutSettings MakeReflowGridSettings(int columnWidth, int rowHeight, int spacing);

// Takes the current screen bounds explicitly (from UIContext::GetBounds(), refreshed
// every frame from the real backbuffer size) rather than reading the g_display globals
// directly - those are only updated via the platform resize-notification path, which on
// Windows runs on a different thread than the UI layout code that calls these, and can
// briefly read stale/zero dimensions right when a resize-triggered CreateViews() runs.
bool IsPortrait(const Bounds &screenBounds);

// The hint bar is a right-hand column in portrait (full screen height) and a
// bottom row in landscape (full screen width, kHintBarThickness tall), so
// these are the space left over for everything else.
float ClientWidth(const Bounds &screenBounds, bool portrait);
float ClientHeight(const Bounds &screenBounds, bool portrait);
float ClientCenterY(const Bounds &screenBounds, bool portrait);

// RAII helpers for the GPU draw-state dances repeated throughout the Kiosk UI's custom
// Draw() overrides: each flushes any pending batched draws before changing state, and
// flushes again (restoring the prior state) when it goes out of scope.

// Binds a one-off texture (e.g. a decoded thumbnail) for the draw calls made in the
// scope's lifetime, then restores the UI atlas texture binding on destruction.
class TextureBindScope {
public:
	TextureBindScope(UIContext &dc, Draw::Texture *texture);
	~TextureBindScope();
private:
	UIContext &dc_;
};

// Rebinds the UI atlas texture (without changing the render pipeline) so untextured
// fills - which sample it but discard the sample - can be interleaved with atlas-drawn
// content.
class UntexturedFillScope {
public:
	explicit UntexturedFillScope(UIContext &dc);
	~UntexturedFillScope();
private:
	UIContext &dc_;
};

// Switches to the untextured render pipeline for primitives (e.g. CircleSegment, raw
// vertex fans) that need it, restoring the normal textured pipeline on destruction.
class NoTexPipelineScope {
public:
	explicit NoTexPipelineScope(UIContext &dc);
	~NoTexPipelineScope();
private:
	UIContext &dc_;
};

void DrawGameArt(UIContext &dc, Draw::Texture *texture, const Bounds &bounds, uint32_t color = 0xFFFFFFFF);
void DrawFocusRing(UIContext &dc, const Bounds &bounds, float thickness, float gap, float radius, uint32_t color = 0xFFFFFFFF);

// Deliberately stretches the whole bin-packed UI atlas (every icon plus the
// PPSSPP wordmark) across bounds, faded vertically between colorTop and
// colorBottom. Originally an accidental effect (a stray RectVGradient call
// left the atlas texture bound), kept intentionally as decorative texture.
void DrawAtlasCollageFade(UIContext &dc, const Bounds &bounds, uint32_t colorTop, uint32_t colorBottom);

// The ground's shadow as it fades up into the hint bar - conceptually part of
// the screen's landscape ground effect, not the hint bar (a view drawing
// outside its own bounds is what caused this to wrongly leak into portrait
// mode when it used to live in KioskHintBar::Draw()). Landscape-only by
// construction - callers should simply not invoke this in portrait.
void DrawLandscapeGroundShade(UIContext &dc);

ImageID ConfirmGlyph();
ImageID BackGlyph();
// The PSP control constant (CTRL_CIRCLE/CTRL_CROSS) matching BackGlyph()'s icon.
int BackButton();

bool RouteKeyToHintBar(bool baseHandled, const KeyInput &key, KioskHintBar *hintBar);

// Hint-bar entries shared across the carousel, grid, and game-info-popup screens.
KioskHintBar::Entry LaunchEntry(UI::Event &onLaunch, const std::string &label);
KioskHintBar::Entry BackEntry(UI::Event &onBack, const std::string &label, std::vector<int> pspButtons = {});
KioskHintBar::Entry SettingsEntry(UI::Event &onSettings, const std::string &gameId);

bool HasGameSpecificConfig(const std::string &gameId);
std::string SettingsHintLabel(const std::string &gameId);
GameSettingsScreen *MakeSettingsScreen(const Path &gamePath, const std::string &gameId);

// True only when the just-closed dialog is KioskGameInfoPopup and it finished via its
// Delete Game flow (the only path there that finishes with DR_OK - Back finishes with
// DR_BACK). The carousel/grid screens use this to tell "a ROM actually vanished, the
// tile list needs rebuilding" apart from "a dialog closed with nothing to refresh but a
// hint bar label" - the latter, far more common case, should leave focus/scroll alone.
bool WasGameDeleted(const Screen *dialog, DialogResult result);

}  // namespace Kiosk
