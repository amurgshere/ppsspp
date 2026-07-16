#include "UI/Kiosk/KioskCarouselView.h"
#include "UI/Kiosk/KioskCommon.h"
#include "UI/Kiosk/KioskTile.h"

#include "Common/UI/Context.h"
#include "Common/Render/DrawBuffer.h"

using namespace UI;

KioskCarouselView::KioskCarouselView(bool isPortraitOrientation, float tileW, float tileH, UI::LayoutParams *layoutParams)
	: KioskTileContainerView<LinearLayout>(ORIENT_VERTICAL, layoutParams), isPortraitOrientation_(isPortraitOrientation), tileW_(tileW), tileH_(tileH) {
	BuildScrollingRow();
}

void KioskCarouselView::BuildScrollingRow() {
	Orientation orient = isPortraitOrientation_ ? ORIENT_VERTICAL : ORIENT_HORIZONTAL;
	scroll_ = new ScrollView(orient, new LinearLayoutParams(FILL_PARENT, FILL_PARENT, 1.0f));

	int8_t crossAxisMargin = (int8_t)Kiosk::kFocusRingExtent;
	Margins rowMargins = isPortraitOrientation_ ? Margins(crossAxisMargin, (int8_t)0) : Margins((int8_t)0, crossAxisMargin);
	row_ = new LinearLayout(orient, new LinearLayoutParams(isPortraitOrientation_ ? FILL_PARENT : WRAP_CONTENT, WRAP_CONTENT, rowMargins));
	row_->SetSpacing(Kiosk::kTileGap);

	scroll_->Add(row_);
	Add(scroll_);
}

UI::Margins KioskCarouselView::LeadingTileMargin() const {
	int8_t rowEdgeMargin = (int8_t)(Kiosk::kTileGap + Kiosk::kFocusRingExtent);
	return isPortraitOrientation_ ? Margins(0, rowEdgeMargin, 0, 0) : Margins(rowEdgeMargin, 0, 0, 0);
}

UI::Margins KioskCarouselView::TrailingTileMargin() const {
	int8_t rowEdgeMargin = (int8_t)(Kiosk::kTileGap + Kiosk::kFocusRingExtent);
	return isPortraitOrientation_ ? Margins(0, 0, 0, rowEdgeMargin) : Margins(0, 0, rowEdgeMargin, 0);
}

UI::LayoutParams *KioskCarouselView::MakeTileLayoutParams(size_t index, bool isAllRomsTile) {
	Gravity crossAxisGravity = isPortraitOrientation_ ? Gravity::G_HCENTER : Gravity::G_VCENTER;
	Margins margins = isAllRomsTile ? TrailingTileMargin() : (index == 0 ? LeadingTileMargin() : Margins());
	return new LinearLayoutParams(tileW_, tileH_, 0.0f, crossAxisGravity, margins);
}

void KioskCarouselView::PopulateFromEntries(const std::vector<KioskGameEntry> &entries,
		KioskTileGroup::TileCallback onLaunch, KioskTileGroup::TileCallback onHoldInfo,
		KioskTileGroup::TileCallback onFocusedTileChanged, const Path &preferredFocusPath) {
	tiles_.PopulateFromEntries(row_, entries,
		[this](size_t index, bool isAllRomsTile) { return MakeTileLayoutParams(index, isAllRomsTile); },
		/* appendAllRomsTile = */ true, std::move(onLaunch), std::move(onHoldInfo), std::move(onFocusedTileChanged),
		/* fadeInOnLoad = */ false, preferredFocusPath);
}

void KioskCarouselView::DrawGroundContactShadows(UIContext &dc) {
	if (isPortraitOrientation_)
		return;

	const uint32_t kGroundLineAlpha = 0x28000000;
	Bounds screenBounds = dc.GetBounds();
	float clientHeight = screenBounds.h - Kiosk::kHintBarThickness;
	float gradientShadowHeight = clientHeight * Kiosk::kGradientShadowHeightPercent;
	Kiosk::GroundShadowBand band = Kiosk::ComputeGroundShadowBand(clientHeight);

	Kiosk::NoTexPipelineScope noTex(dc);
	for (int i = 0; i < row_->GetNumSubviews(); i++) {
		Bounds b = row_->GetViewByIndex(i)->GetBounds();
		if (b.x2() < 0.0f || b.x > screenBounds.w)
			continue;

		float shadowTop = b.y2();
		float shadowBottom = shadowTop + gradientShadowHeight;
		dc.Draw()->RectVGradient(b.x, shadowTop, b.x2(), shadowBottom, 0x40000000, 0x00000000);

		dc.Draw()->RectVGradient(b.x, band.lineY - band.softEdge, b.x2(), band.lineY, 0x00000000, kGroundLineAlpha);
		dc.Draw()->Rect(b.x, band.lineY, b.w, band.solidHeight, kGroundLineAlpha);
		dc.Draw()->RectVGradient(b.x, band.lineY + band.solidHeight, b.x2(), band.lineY + band.solidHeight + band.softEdge, kGroundLineAlpha, 0x00000000);
	}
}
