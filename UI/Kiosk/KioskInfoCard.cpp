#include "UI/Kiosk/KioskInfoCard.h"
#include "UI/Kiosk/KioskCommon.h"
#include "UI/GameInfoCache.h"

#include "Common/UI/Context.h"
#include "Common/GPU/thin3d.h"

using namespace UI;

void KioskInfoCard::Draw(UIContext &dc) {
	{
		Kiosk::UntexturedFillScope fillScope(dc);
		dc.FillRect(dc.GetTheme().itemStyle.background, bounds_);

		const float t = 2.0f;
		UI::Drawable border(dc.GetTheme().itemFocusedStyle.fgColor);
		dc.FillRect(border, Bounds(bounds_.x, bounds_.y, bounds_.w, t));
		dc.FillRect(border, Bounds(bounds_.x, bounds_.y2() - t, bounds_.w, t));
		dc.FillRect(border, Bounds(bounds_.x, bounds_.y, t, bounds_.h));
		dc.FillRect(border, Bounds(bounds_.x2() - t, bounds_.y, t, bounds_.h));

		if (dividerAnchor_) {
			float x = dividerAnchor_->GetBounds().x;
			float y = dividerAnchor_->GetBounds().y - 14.0f;
			float x2 = bounds_.x2() - padding.right;
			dc.FillRect(UI::Drawable(0xFFFFFFFF), Bounds(x, y, x2 - x, 2.0f));
		}
	}

	// Weighted children can end up measuring taller than the card's own
	// fixed height when the fixed-size rows above them (title/badges/
	// stats) already consume most of it - LinearLayout::Layout doesn't
	// clamp positions to the parent's bounds, so without this scissor
	// the save-slot grid can render past the card's bottom border.
	dc.PushScissor(bounds_);
	LinearLayout::Draw(dc);
	dc.PopScissor();
}

KioskInfoIcon::KioskInfoIcon(std::shared_ptr<GameInfo> info, UI::LayoutParams *layoutParams)
	: InertView(layoutParams), info_(info) {}

void KioskInfoIcon::Draw(UIContext &dc) {
	Draw::Texture *texture = (info_ && info_->Ready(GameInfoFlags::ICON) && info_->icon.texture) ? info_->icon.texture : nullptr;
	if (!texture)
		return;

	float artH = bounds_.w / Kiosk::kTileAspect;
	float artY = bounds_.y + (bounds_.h - artH) * 0.5f;
	Kiosk::DrawGameArt(dc, texture, Bounds(bounds_.x, artY, bounds_.w, artH));
}
