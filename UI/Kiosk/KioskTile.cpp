#include "UI/Kiosk/KioskTile.h"
#include "UI/Kiosk/KioskCommon.h"
#include "UI/GameInfoCache.h"

#include "Common/UI/Context.h"
#include "Common/Render/DrawBuffer.h"
#include "Common/Data/Color/RGBAUtil.h"
#include "Common/Math/curves.h"
#include "Common/Math/math_util.h"
#include "Common/TimeUtil.h"

KioskTile::KioskTile(const Path &gamePath, UI::LayoutParams *layoutParams)
	: KioskFocusHighlightable<UI::Clickable>(layoutParams), gamePath_(gamePath) {}

KioskTile::KioskTile(UI::LayoutParams *layoutParams)
	: KioskFocusHighlightable<UI::Clickable>(layoutParams), isAllRoms_(true) {}

void KioskTile::Draw(UIContext &dc) {
	std::shared_ptr<GameInfo> ginfo;
	Draw::Texture *texture = nullptr;
	if (!isAllRoms_) {
		ginfo = g_gameInfoCache->GetInfo(dc.GetDrawContext(), gamePath_, GameInfoFlags::PARAM_SFO | GameInfoFlags::ICON);
		texture = (ginfo->Ready(GameInfoFlags::ICON) && ginfo->icon.texture) ? ginfo->icon.texture : nullptr;
	}

	bool focused = IsHighlighted();
	UI::Style style = focused ? dc.GetTheme().itemFocusedStyle : dc.GetTheme().itemStyle;

	Bounds drawBounds = bounds_;

	if (focused) {
		Kiosk::DrawFocusRing(dc, drawBounds, Kiosk::kTileBorderThickness, Kiosk::kTileBorderGap, Kiosk::kTileBorderRadius);
	}

	{
		Kiosk::UntexturedFillScope fillScope(dc);
		dc.FillRect(style.background, drawBounds);
	}

	if (texture) {
		u32 color = fadeInOnLoad_ ? whiteAlpha(ease((time_now_d() - ginfo->icon.timeLoaded) * Kiosk::kIconFadeInSpeed)) : 0xFFFFFFFF;
		Kiosk::DrawGameArt(dc, texture, drawBounds, color);
	} else if (isAllRoms_) {
		Kiosk::UntexturedFillScope fillScope(dc);
		const float marginX = drawBounds.w * 0.16f;
		const float marginY = drawBounds.h * 0.16f;
		const float gapX = drawBounds.w * 0.06f;
		const float gapY = drawBounds.h * 0.06f;
		const float cellW = (drawBounds.w - marginX * 2 - gapX * 2) / 3.0f;
		const float cellH = (drawBounds.h - marginY * 2 - gapY * 2) / 3.0f;
		UI::Drawable box(0xFFFFFFFF);
		for (int row = 0; row < 3; row++) {
			for (int col = 0; col < 3; col++) {
				float x = drawBounds.x + marginX + col * (cellW + gapX);
				float y = drawBounds.y + marginY + row * (cellH + gapY);
				dc.FillRect(box, Bounds(x, y, cellW, cellH));
			}
		}
	}
}

void KioskTile::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	w = Kiosk::kTileNativeW;
	h = Kiosk::kTileNativeH;
}

bool KioskTile::Touch(const TouchInput &input) {
	bool retval = UI::Clickable::Touch(input);
	if (bounds_.Contains(input.x, input.y) && (input.flags & TOUCH_DOWN))
		holdStart_ = time_now_d();
	if (input.flags & TOUCH_UP)
		holdStart_ = 0;
	return retval;
}

void KioskTile::Update() {
	if (holdStart_ != 0.0 && holdStart_ < time_now_d() - Kiosk::kHoldToInfoSeconds) {
		holdStart_ = 0.0;
		UI::EventParams e{};
		e.v = this;
		e.s = gamePath_.ToString();
		OnHoldClick.Trigger(e);
	}
}
