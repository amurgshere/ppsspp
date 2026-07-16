#include <algorithm>
#include <cmath>

#include "UI/Kiosk/KioskHintBar.h"
#include "UI/Kiosk/KioskCommon.h"

#include "Common/UI/Context.h"
#include "Common/Render/DrawBuffer.h"
#include "Common/Data/Color/RGBAUtil.h"
#include "Common/Input/InputState.h"
#include "Common/Math/math_util.h"

#include "Core/KeyMap.h"

using namespace UI;

namespace {
// Doubled vs. a square glyph's natural slot - the L/R composite icons are
// wider than tall, so fitting by max(w,h) leaves them looking undersized.
constexpr float kGlyphSlot = 68.0f;

// 270 degrees clockwise, so vertical hint bar content reads bottom-to-top.
// Landscape draws at angle 0 through the same code path (RotatePointAroundPivot
// and DrawTextRectSqueeze's rotation are both no-ops at 0), so icon/text sizing
// and spacing stay identical between orientations by construction rather than
// needing to be kept in sync by hand across two separate code paths.
constexpr float kVerticalHintAngle = 3.0f * PI / 2.0f;

// Icon shrinks more than text so it doesn't visually dominate the label.
constexpr float kHintIconScale = 0.5f;
constexpr float kHintTextScale = 0.75f;
// Gap between the icon's own slot and the text - scales with the icon rather
// than staying a fixed dp value, so it tightens along with the smaller icon.
constexpr float kIconTextGap = 8.0f;

void DrawFocusGlow(UIContext &dc, const Bounds &bounds) {
	const AtlasImage *img = dc.Draw()->GetAtlas()->getImage(ImageID("I_DROP_SHADOW"));
	if (!img)
		return;
	float scale = (bounds.h * 2.2f) / img->w;
	dc.Draw()->DrawImage(ImageID("I_DROP_SHADOW"), bounds.x + bounds.h * 0.5f, bounds.centerY(), scale, colorAlpha(0xFFFFFFFF, 0.7f), ALIGN_CENTER);
}

// Rotates a point around a pivot, same convention as DrawBuffer's internal
// rot() helper used by DrawImageRotated/DrawImageRotatedStretch.
void RotatePointAroundPivot(float *x, float *y, float angle, float pivotX, float pivotY) {
	float dx = *x - pivotX;
	float dy = *y - pivotY;
	float ca = cosf(angle), sa = sinf(angle);
	*x = dx * ca - dy * sa + pivotX;
	*y = dx * sa + dy * ca + pivotY;
}
}  // namespace

KioskHintButton::KioskHintButton(ImageID icon, ImageID overlayIcon, std::string_view label, bool flipIconH,
		Orientation orientation, float iconScale, UI::LayoutParams *layoutParams)
	: Choice(label, icon, layoutParams), overlayIcon_(overlayIcon), flipIconH_(flipIconH), orientation_(orientation), iconScale_(iconScale) {}

void KioskHintButton::GetContentDimensionsBySpec(const UIContext &dc, MeasureSpec horiz, MeasureSpec vert, float &w, float &h) const {
	// Measure in "logical" (unrotated, icon-left/text-right) space using the
	// strip's along-axis extent as the available run length - horiz (bar
	// width) in landscape, vert (bar height, pre-rotation) in portrait - then
	// swap w/h for portrait since the button draws rotated 270 degrees.
	float iconSlot = (overlayIcon_.isValid() ? kGlyphSlot : ITEM_HEIGHT) * kHintIconScale * iconScale_;
	float paddingLeft = iconSlot + kIconTextGap;
	float paddingRight = kIconTextGap;
	const MeasureSpec &runLengthSpec = vertical() ? vert : horiz;
	float runLengthAvail = (runLengthSpec.type == UNSPECIFIED ? 65535.0f : runLengthSpec.size);
	float availWidth = runLengthAvail - paddingLeft - paddingRight - textPadding_.horiz();
	if (availWidth < 0.0f)
		availWidth = 65535.0f;
	float scale = dc.CalculateTextScale(text_, availWidth) * kHintTextScale;
	float textW = 0.0f, textH = 0.0f;
	dc.MeasureTextRect(dc.GetTheme().uiFont, scale, scale, text_, availWidth, &textW, &textH, FLAG_WRAP_TEXT);
	float logicalW = paddingLeft + textW + paddingRight + textPadding_.horiz();
	float logicalH = std::max(textH + 16.0f, iconSlot);

	if (vertical()) {
		w = logicalH;
		h = logicalW;
	} else {
		w = logicalW;
		h = logicalH;
	}
}

float KioskHintButton::IconSlotSize() const {
	return (overlayIcon_.isValid() ? kGlyphSlot : ITEM_HEIGHT) * kHintIconScale * iconScale_;
}

void KioskHintButton::DrawIcon(UIContext &dc, const Bounds &logicalBounds, float angle, const UI::Style &style) {
	float iconSlot = IconSlotSize();
	float iconX = logicalBounds.x + iconSlot * 0.5f;
	float iconY = logicalBounds.centerY();
	RotatePointAroundPivot(&iconX, &iconY, angle, bounds_.centerX(), bounds_.centerY());

	if (overlayIcon_.isValid()) {
		const AtlasImage *img = dc.Draw()->GetAtlas()->getImage(image_);
		float scale = (img && img->w > 0 && img->h > 0) ? iconSlot / std::max(img->w, img->h) : 1.0f;
		dc.Draw()->DrawImageRotated(image_, iconX, iconY, scale, angle, style.fgColor, flipIconH_);
		dc.Draw()->DrawImageRotated(overlayIcon_, iconX, iconY, scale, angle, style.fgColor, false);
	} else if (image_.isValid()) {
		const AtlasImage *img = dc.Draw()->GetAtlas()->getImage(image_);
		float scale = (img && img->w > 0 && img->h > 0) ? iconSlot / std::max(img->w, img->h) : 1.0f;
		dc.Draw()->DrawImageRotated(image_, iconX, iconY, scale, angle, style.fgColor, false);
	}
}

void KioskHintButton::DrawLabel(UIContext &dc, const Bounds &logicalBounds, float angle, const UI::Style &style) {
	float paddingLeft = IconSlotSize() + kIconTextGap;
	float paddingRight = kIconTextGap;

	dc.SetFontStyle(dc.GetTheme().uiFont);
	dc.SetFontScale(kHintTextScale, kHintTextScale);
	float availWidth = logicalBounds.w - paddingLeft - paddingRight - textPadding_.horiz();
	Bounds textBounds(logicalBounds.x + paddingLeft + textPadding_.left, logicalBounds.y, availWidth, logicalBounds.h);

	// textBounds is left-aligned within logicalBounds, so its own center isn't
	// the button's true center - DrawTextRectSqueeze would pivot around the
	// wrong point. Rotate textBounds's center around the button's actual
	// center first, then draw there, so text rotates in place around the
	// icon's shared centerline instead of swinging away from it.
	float textCenterX = textBounds.centerX();
	float textCenterY = textBounds.centerY();
	RotatePointAroundPivot(&textCenterX, &textCenterY, angle, bounds_.centerX(), bounds_.centerY());
	Bounds rotatedTextBounds(textCenterX - textBounds.w * 0.5f, textCenterY - textBounds.h * 0.5f, textBounds.w, textBounds.h);
	dc.DrawTextRectSqueeze(text_, rotatedTextBounds, style.fgColor, ALIGN_CENTER | FLAG_WRAP_TEXT | drawTextFlags_, angle);
	dc.SetFontScale(1.0f, 1.0f);
}

void KioskHintButton::Draw(UIContext &dc) {
	Style style = dc.GetTheme().itemStyle;
	if (HasFocus()) style = dc.GetTheme().itemFocusedStyle;
	if (down_) style = dc.GetTheme().itemDownStyle;
	if (!IsEnabled()) style = dc.GetTheme().itemDisabledStyle;
	DrawBG(dc, style);

	// Landscape draws at angle 0, portrait at 270 degrees clockwise - both
	// share the same logical (unrotated, icon-left/text-right) layout code,
	// so icon/text sizing and spacing can't drift apart between orientations.
	const float angle = vertical() ? kVerticalHintAngle : 0.0f;

	// Logical (unrotated) bounds: icon-left/text-right layout, centered on
	// the button's true center, matching GetContentDimensionsBySpec's model.
	// In landscape this is just bounds_ itself (no swap, no rotation).
	Bounds logicalBounds = vertical()
		? Bounds(bounds_.centerX() - bounds_.h * 0.5f, bounds_.centerY() - bounds_.w * 0.5f, bounds_.h, bounds_.w)
		: bounds_;

	DrawIcon(dc, logicalBounds, angle, style);
	DrawLabel(dc, logicalBounds, angle, style);
}

KioskHintBar::KioskHintBar(Orientation orientation, UI::LayoutParams *layoutParams)
	: LinearLayout(orientation, layoutParams), orientation_(orientation) {
	SetSpacing(24.0f);
}

void KioskHintBar::SetEntries(const std::vector<Entry> &entries) {
	Clear();
	entries_ = entries;
	buttons_.assign(entries_.size(), nullptr);
	// Pushes buttons toward the trailing edge in both orientations - the
	// right edge horizontally (today's look), the bottom edge vertically.
	Add(new Spacer(new LinearLayoutParams(1.0f)));
	Gravity crossGravity = vertical() ? Gravity::G_HCENTER : Gravity::G_VCENTER;
	for (size_t n = 0; n < entries_.size(); n++) {
		// Vertical mode reads bottom-to-top, so entries are added in reverse
		// visual order - the last entry ends up nearest the trailing (bottom)
		// edge, i.e. "first" in reading order - while buttons_ stays indexed
		// by entries_'s own order for UpdateEntryLabel().
		size_t i = vertical() ? (entries_.size() - 1 - n) : n;
		const Entry &entry = entries_[i];
		KioskHintButton *button = new KioskHintButton(entry.glyph, entry.overlayGlyph, entry.label, entry.flipGlyphH,
			orientation_, entry.iconScale, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT, 0.0f, crossGravity));
		// Padding is applied in the button's logical (unrotated, icon-left/
		// text-right) space regardless of the bar's on-screen orientation,
		// so it doesn't need to change between orientations.
		button->SetTextPadding(UI::Padding(8.0f, 0.0f, 24.0f, 0.0f));
		button->SetDrawBG(false);
		if (entry.event) {
			button->OnClick.Add([entry](UI::EventParams &e) {
				entry.event->Trigger(e);
			});
		}
		Add(button);
		buttons_[i] = button;
	}
	// The leading Spacer above has weight and fills all remaining space in a
	// FILL_PARENT strip regardless of container padding (LinearLayout's
	// weighted-fill math doesn't reserve room for padding.vert()/horiz() when
	// its own size is FILL_PARENT) - so a real margin at the trailing edge
	// (bottom in portrait) needs its own fixed-size, non-weighted child there
	// instead of container padding.
	if (vertical())
		Add(new Spacer(16.0f));
}

void KioskHintBar::UpdateEntryLabel(const UI::Event *event, std::string_view label) {
	for (size_t i = 0; i < entries_.size(); i++) {
		if (entries_[i].event != event)
			continue;
		entries_[i].label = std::string(label);
		if (i < buttons_.size())
			buttons_[i]->SetText(label);
		return;
	}
}

void KioskHintBar::SetButtonsCanBeFocused(bool canBeFocused) {
	for (KioskHintButton *button : buttons_)
		button->SetCanBeFocused(canBeFocused);
}

bool KioskHintBar::RouteKey(const KeyInput &key) const {
	InputMapping mapping(key.deviceId, key.keyCode);
	std::vector<int> pspButtons;
	KeyMap::InputMappingToPspButton(mapping, &pspButtons);
	for (int button : pspButtons) {
		for (const auto &entry : entries_) {
			if (!entry.event)
				continue;
			if (std::find(entry.pspButtons.begin(), entry.pspButtons.end(), button) != entry.pspButtons.end()) {
				UI::EventParams e{};
				entry.event->Trigger(e);
				return true;
			}
		}
	}
	return false;
}

void KioskHintBar::Draw(UIContext &dc) {
	dc.Draw()->Flush();
	dc.FillRect(UI::Drawable(0xCC000000), bounds_);
	dc.Draw()->Flush();

	{
		Kiosk::UntexturedFillScope fillScope(dc);
		dc.PushScissor(bounds_);
		for (int i = 0; i < GetNumSubviews(); i++) {
			UI::View *view = GetViewByIndex(i);
			if (view->HasFocus() || view->IsPressed())
				DrawFocusGlow(dc, view->GetBounds());
		}
		dc.PopScissor();
	}

	LinearLayout::Draw(dc);
}

namespace Kiosk {
KioskHintBar *AddHintBar(UI::ViewGroup *root, Orientation orientation) {
	UI::AnchorLayoutParams *layoutParams = orientation == ORIENT_VERTICAL
		? new AnchorLayoutParams(kHintBarThickness, FILL_PARENT, NONE, 0, 0, 0)
		: new AnchorLayoutParams(FILL_PARENT, kHintBarThickness, 0, NONE, 0, 0);
	KioskHintBar *hintBar = new KioskHintBar(orientation, layoutParams);
	root->Add(hintBar);
	return hintBar;
}
}  // namespace Kiosk
