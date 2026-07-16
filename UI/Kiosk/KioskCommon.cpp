#include <algorithm>

#include "UI/Kiosk/KioskCommon.h"
#include "UI/Kiosk/KioskHintBar.h"
#include "UI/GameSettingsScreen.h"

#include "Common/UI/Context.h"
#include "Common/Render/DrawBuffer.h"
#include "Common/GPU/thin3d.h"
#include "Common/Math/geom2d.h"
#include "Common/Input/InputState.h"
#include "Common/Data/Text/I18n.h"

#include "Core/Config.h"
#include "Core/HLE/sceUtility.h"
#include "Core/HLE/sceCtrl.h"

namespace Kiosk {

TextureBindScope::TextureBindScope(UIContext &dc, Draw::Texture *texture) : dc_(dc) {
	dc_.Draw()->Flush();
	dc_.GetDrawContext()->BindTexture(0, texture);
}

TextureBindScope::~TextureBindScope() {
	dc_.Draw()->Flush();
	dc_.RebindTexture();
}

UntexturedFillScope::UntexturedFillScope(UIContext &dc) : dc_(dc) {
	dc_.Draw()->Flush();
	dc_.RebindTexture();
}

UntexturedFillScope::~UntexturedFillScope() {
	dc_.Draw()->Flush();
}

NoTexPipelineScope::NoTexPipelineScope(UIContext &dc) : dc_(dc) {
	dc_.Flush();
	dc_.BeginNoTex();
}

NoTexPipelineScope::~NoTexPipelineScope() {
	dc_.Flush();
	dc_.Begin();
}

GroundShadowBand ComputeGroundShadowBand(float clientHeight) {
	GroundShadowBand band;
	band.lineY = clientHeight * kGroundLinePositionPercent;
	band.solidHeight = clientHeight * kGroundLineSolidHeightPercent;
	band.softEdge = clientHeight * kGroundLineSoftEdgePercent;
	band.bottom = band.lineY + band.solidHeight + band.softEdge;
	return band;
}

UI::GridLayoutSettings MakeReflowGridSettings(int columnWidth, int rowHeight, int spacing) {
	UI::GridLayoutSettings settings(columnWidth, rowHeight, spacing);
	settings.fillCells = true;
	settings.centerContent = true;
	return settings;
}

bool IsPortrait(const Bounds &screenBounds) {
	return screenBounds.h > screenBounds.w;
}

float ClientWidth(const Bounds &screenBounds, bool portrait) {
	return portrait ? screenBounds.w - kHintBarThickness : screenBounds.w;
}

float ClientHeight(const Bounds &screenBounds, bool portrait) {
	return portrait ? screenBounds.h : screenBounds.h - kHintBarThickness;
}

float ClientCenterY(const Bounds &screenBounds, bool portrait) {
	return ClientHeight(screenBounds, portrait) / 2.0f;
}

// Some games' icons aren't the same aspect ratio as the box-art-shaped tile bounds
// (close to square rather than widescreen) - fit within bounds preserving the
// texture's own aspect ratio (letterboxed/pillarboxed) instead of stretching it.
static Bounds FitPreservingAspect(const Bounds &bounds, int texW, int texH) {
	if (texW <= 0 || texH <= 0)
		return bounds;
	float scale = std::min(bounds.w / texW, bounds.h / texH);
	float fitW = texW * scale;
	float fitH = texH * scale;
	return Bounds(bounds.x + (bounds.w - fitW) * 0.5f, bounds.y + (bounds.h - fitH) * 0.5f, fitW, fitH);
}

void DrawGameArt(UIContext &dc, Draw::Texture *texture, const Bounds &bounds, uint32_t color) {
	if (!texture)
		return;
	Bounds fitBounds = FitPreservingAspect(bounds, texture->Width(), texture->Height());
	TextureBindScope texScope(dc, texture);
	dc.Draw()->DrawTexRect(fitBounds.x, fitBounds.y, fitBounds.x2(), fitBounds.y2(), 0, 0, 1, 1, color);
}

void DrawFocusRing(UIContext &dc, const Bounds &bounds, float thickness, float gap, float radius, uint32_t color) {
	UI::Drawable border(color);
	Bounds ring = bounds.Expand(gap);

	float cxLeft = ring.x + radius - thickness * 0.5f;
	float cxRight = ring.x2() - radius + thickness * 0.5f;
	float cyTop = ring.y + radius - thickness * 0.5f;
	float cyBottom = ring.y2() - radius + thickness * 0.5f;

	{
		UntexturedFillScope fillScope(dc);
		dc.FillRect(border, Bounds(cxLeft, ring.y - thickness, cxRight - cxLeft, thickness));
		dc.FillRect(border, Bounds(cxLeft, ring.y2(), cxRight - cxLeft, thickness));
		dc.FillRect(border, Bounds(ring.x - thickness, cyTop, thickness, cyBottom - cyTop));
		dc.FillRect(border, Bounds(ring.x2(), cyTop, thickness, cyBottom - cyTop));
	}

	NoTexPipelineScope noTex(dc);
	dc.Draw()->CircleSegment(cxLeft, cyTop, radius, thickness, 24, PI, PI * 1.5f, color, 1.0f);
	dc.Draw()->CircleSegment(cxRight, cyTop, radius, thickness, 24, PI * 1.5f, PI * 2.0f, color, 1.0f);
	dc.Draw()->CircleSegment(cxRight, cyBottom, radius, thickness, 24, 0.0f, PI * 0.5f, color, 1.0f);
	dc.Draw()->CircleSegment(cxLeft, cyBottom, radius, thickness, 24, PI * 0.5f, PI, color, 1.0f);
}

void DrawAtlasCollageFade(UIContext &dc, const Bounds &bounds, uint32_t colorTop, uint32_t colorBottom) {
	UntexturedFillScope fillScope(dc);
	dc.Draw()->RectVGradient(bounds.x, bounds.y, bounds.x2(), bounds.y2(), colorTop, colorBottom);
}

void DrawLandscapeGroundShade(UIContext &dc) {
	const uint32_t kFadeTopColor = 0x00000000;
	const uint32_t kFadeBottomColor = 0x8F000000;

	Bounds screenBounds = dc.GetBounds();
	Bounds hintBarBounds(0, screenBounds.h - kHintBarThickness, screenBounds.w, kHintBarThickness);
	float shadeTop = hintBarBounds.y - hintBarBounds.h * 3.0f;
	DrawAtlasCollageFade(dc, Bounds(hintBarBounds.x, shadeTop, hintBarBounds.w, hintBarBounds.y2() - shadeTop), kFadeTopColor, kFadeBottomColor);
}

ImageID ConfirmGlyph() {
	bool useCross = g_Config.iButtonPreference == PSP_SYSTEMPARAM_BUTTON_CROSS;
	return ImageID(useCross ? "I_CROSS" : "I_CIRCLE");
}

ImageID BackGlyph() {
	bool useCross = g_Config.iButtonPreference == PSP_SYSTEMPARAM_BUTTON_CROSS;
	return ImageID(useCross ? "I_CIRCLE" : "I_CROSS");
}

int BackButton() {
	bool useCross = g_Config.iButtonPreference == PSP_SYSTEMPARAM_BUTTON_CROSS;
	return useCross ? CTRL_CIRCLE : CTRL_CROSS;
}

bool RouteKeyToHintBar(bool baseHandled, const KeyInput &key, KioskHintBar *hintBar) {
	if (baseHandled || !(key.flags & KEY_DOWN) || !hintBar)
		return baseHandled;
	return hintBar->RouteKey(key);
}

bool HasGameSpecificConfig(const std::string &gameId) {
	return !gameId.empty() && g_Config.HasGameConfig(gameId);
}

std::string SettingsHintLabel(const std::string &gameId) {
	auto ga = GetI18NCategory(I18NCat::GAME);
	auto ki = GetI18NCategory(I18NCat::MAINMENU);
	return HasGameSpecificConfig(gameId) ? std::string(ga->T("Game Settings")) : std::string(ki->T("Settings"));
}

GameSettingsScreen *MakeSettingsScreen(const Path &gamePath, const std::string &gameId) {
	return new GameSettingsScreen(gamePath, gameId, /* editThenRestore = */ HasGameSpecificConfig(gameId));
}

KioskHintBar::Entry LaunchEntry(UI::Event &onLaunch, const std::string &label) {
	return { ConfirmGlyph(), label, &onLaunch };
}

KioskHintBar::Entry BackEntry(UI::Event &onBack, const std::string &label, std::vector<int> pspButtons) {
	return { BackGlyph(), label, &onBack, ImageID::invalid(), std::move(pspButtons) };
}

KioskHintBar::Entry SettingsEntry(UI::Event &onSettings, const std::string &gameId) {
	return { ImageID("I_TRIANGLE"), SettingsHintLabel(gameId), &onSettings, ImageID::invalid(), { CTRL_TRIANGLE } };
}

bool WasGameDeleted(const Screen *dialog, DialogResult result) {
	return dialog && result == DR_OK && std::string(dialog->tag()) == "KioskGameInfoPopup";
}

}  // namespace Kiosk
