/**************************************************************************/
/*  web_list.cpp                                                          */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "web_list.h"

#include "core/input/input_event.h"
#include "core/object/class_db.h"
#include "scene/resources/font.h"
#include "scene/resources/style_box.h"
#include "scene/theme/theme_db.h"
#include "servers/rendering/rendering_server.h"
#include "servers/text/text_server.h"

Size2 WebList::_get_text_size(const String &p_text, bool p_marker) const {
	Ref<Font> font = p_marker && theme_cache.marker_font.is_valid() ? theme_cache.marker_font : theme_cache.font;
	const int font_size = p_marker && theme_cache.marker_font_size > 0 ? theme_cache.marker_font_size : theme_cache.font_size;
	if (font.is_null()) {
		return Size2();
	}
	return font->get_string_size(p_text, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size);
}

int WebList::_get_line_height() const {
	if (theme_cache.line_height > 0) {
		return theme_cache.line_height;
	}
	if (theme_cache.font.is_valid()) {
		return Math::ceil(theme_cache.font->get_height(theme_cache.font_size) * 1.2);
	}
	return theme_cache.font_size > 0 ? Math::ceil(theme_cache.font_size * 1.2) : 19;
}

String WebList::_int_to_alpha(int p_number, bool p_uppercase) const {
	if (p_number <= 0) {
		return String::num_int64(p_number);
	}

	String result;
	int value = p_number;
	while (value > 0) {
		value--;
		result = String::chr((p_uppercase ? 'A' : 'a') + (value % 26)) + result;
		value /= 26;
	}
	return result;
}

String WebList::_int_to_roman(int p_number, bool p_uppercase) const {
	if (p_number <= 0 || p_number > 3999) {
		return String::num_int64(p_number);
	}

	struct RomanPart {
		int value;
		const char *symbol;
	};
	static const RomanPart parts[] = {
		{ 1000, "M" }, { 900, "CM" }, { 500, "D" }, { 400, "CD" },
		{ 100, "C" }, { 90, "XC" }, { 50, "L" }, { 40, "XL" },
		{ 10, "X" }, { 9, "IX" }, { 5, "V" }, { 4, "IV" }, { 1, "I" }
	};

	String result;
	int value = p_number;
	for (const RomanPart &part : parts) {
		while (value >= part.value) {
			result += part.symbol;
			value -= part.value;
		}
	}
	return p_uppercase ? result : result.to_lower();
}

String WebList::_get_ordered_marker_text(int p_number, OrderedType p_type) const {
	switch (p_type) {
		case ORDERED_TYPE_LOWER_ALPHA:
			return _int_to_alpha(p_number, false) + ".";
		case ORDERED_TYPE_UPPER_ALPHA:
			return _int_to_alpha(p_number, true) + ".";
		case ORDERED_TYPE_LOWER_ROMAN:
			return _int_to_roman(p_number, false) + ".";
		case ORDERED_TYPE_UPPER_ROMAN:
			return _int_to_roman(p_number, true) + ".";
		case ORDERED_TYPE_DECIMAL_LEADING_ZERO: {
			// CSS `decimal-leading-zero` pads to a minimum of two digits.
			const String digits = String::num_int64(Math::abs(p_number));
			const String sign = p_number < 0 ? "-" : "";
			return sign + (digits.length() < 2 ? "0" + digits : digits) + ".";
		}
		case ORDERED_TYPE_DECIMAL:
		default:
			return String::num_int64(p_number) + ".";
	}
}

int WebList::_get_item_number(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, items.size(), 0);
	if (items[p_idx].value_enabled) {
		return items[p_idx].value;
	}
	if (reversed) {
		return start - p_idx;
	}
	return start + p_idx;
}

String WebList::_get_item_marker_text(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, items.size(), String());

	if (!items[p_idx].custom_marker_text.is_empty()) {
		return items[p_idx].custom_marker_text;
	}

	ListStyleType effective_type = list_style_type;
	if (list_tag == LIST_TAG_ORDERED && effective_type == LIST_STYLE_TYPE_DISC) {
		effective_type = LIST_STYLE_TYPE_DECIMAL;
	}

	switch (effective_type) {
		case LIST_STYLE_TYPE_NONE:
			return String();
		case LIST_STYLE_TYPE_CIRCLE:
			return String::utf8("○");
		case LIST_STYLE_TYPE_SQUARE:
			return String::utf8("■");
		case LIST_STYLE_TYPE_DECIMAL:
			return _get_ordered_marker_text(_get_item_number(p_idx), ORDERED_TYPE_DECIMAL);
		case LIST_STYLE_TYPE_DECIMAL_LEADING_ZERO:
			return _get_ordered_marker_text(_get_item_number(p_idx), ORDERED_TYPE_DECIMAL_LEADING_ZERO);
		case LIST_STYLE_TYPE_LOWER_ALPHA:
			return _get_ordered_marker_text(_get_item_number(p_idx), ORDERED_TYPE_LOWER_ALPHA);
		case LIST_STYLE_TYPE_UPPER_ALPHA:
			return _get_ordered_marker_text(_get_item_number(p_idx), ORDERED_TYPE_UPPER_ALPHA);
		case LIST_STYLE_TYPE_LOWER_ROMAN:
			return _get_ordered_marker_text(_get_item_number(p_idx), ORDERED_TYPE_LOWER_ROMAN);
		case LIST_STYLE_TYPE_UPPER_ROMAN:
			return _get_ordered_marker_text(_get_item_number(p_idx), ORDERED_TYPE_UPPER_ROMAN);
		case LIST_STYLE_TYPE_DISC:
		default:
			return String::utf8("•");
	}
}

String WebList::_get_translated_item_text(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, items.size(), String());
	switch (items[p_idx].auto_translate_mode) {
		case AUTO_TRANSLATE_MODE_INHERIT:
			return atr(items[p_idx].text);
		case AUTO_TRANSLATE_MODE_ALWAYS:
			return tr(items[p_idx].text);
		case AUTO_TRANSLATE_MODE_DISABLED:
			return items[p_idx].text;
	}
	ERR_FAIL_V(items[p_idx].text);
}


int WebList::_text_baseline() const {
	if (theme_cache.font.is_null()) {
		return theme_cache.font_size;
	}
	return Math::floor((_get_line_height() - theme_cache.font->get_height(theme_cache.font_size)) * 0.5 + theme_cache.font->get_ascent(theme_cache.font_size));
}

// Blink's ListMarker symbol box (disc, circle, square): whole pixels derived
// from the marker font's rounded ascent.
static _FORCE_INLINE_ int _web_list_bullet_size(int p_ascent) {
	return (p_ascent * 2 / 3 + 1) / 2;
}

bool WebList::_is_symbol_marker(int p_idx) const {
	return list_tag == LIST_TAG_UNORDERED && items[p_idx].custom_marker_text.is_empty() &&
			(list_style_type == LIST_STYLE_TYPE_DISC || list_style_type == LIST_STYLE_TYPE_CIRCLE || list_style_type == LIST_STYLE_TYPE_SQUARE);
}

String WebList::_inside_marker_text(int p_idx) const {
	const String marker = _get_item_marker_text(p_idx);
	// A counter marker is "1. ": its suffix ends in the space the text follows.
	return marker.is_empty() || !items[p_idx].custom_marker_text.is_empty() ? marker : marker + " ";
}

WebList::MarkerBox WebList::_marker_box() const {
	MarkerBox box;
	const Ref<Font> font = theme_cache.marker_font.is_valid() ? theme_cache.marker_font : theme_cache.font;
	box.font_size = theme_cache.marker_font_size > 0 ? theme_cache.marker_font_size : theme_cache.font_size;
	if (theme_cache.marker_ascent > 0) {
		// The host knows the browser's rounded metrics of the marker's inline box.
		box.ascent = theme_cache.marker_ascent;
		box.above = theme_cache.marker_above;
		box.below = theme_cache.marker_below;
		return box;
	}
	const int line_height = _get_line_height();
	if (font.is_null() || (font == theme_cache.font && box.font_size == theme_cache.font_size)) {
		box.ascent = font.is_valid() ? (int)Math::ceil(font->get_ascent(box.font_size)) : box.font_size;
		box.above = _text_baseline();
		box.below = line_height - box.above;
		return box;
	}
	// An inline box of another size takes its own share of the line: the same
	// line-height, centred on its own ascent and descent.
	const real_t height = font->get_height(box.font_size);
	const real_t marker_line = theme_cache.line_height > 0 ? (real_t)theme_cache.line_height : Math::ceil(height * 1.2);
	box.ascent = (int)Math::ceil(font->get_ascent(box.font_size));
	box.above = (int)Math::floor((marker_line - height) * 0.5 + font->get_ascent(box.font_size));
	box.below = (int)marker_line - box.above;
	return box;
}

real_t WebList::_inside_marker_advance(int p_idx, const MarkerBox &p_box) const {
	if (_get_item_marker_text(p_idx).is_empty()) {
		return 0;
	}
	if (_is_symbol_marker(p_idx)) {
		// Blink: a -1px start margin, the symbol box (the bullet plus 2px) and a 1em end margin.
		return -1 + _web_list_bullet_size(p_box.ascent) + 2 + p_box.font_size;
	}
	return _get_text_size(_inside_marker_text(p_idx), true).width;
}

// An outside marker is a box of its own that ends at the item's border edge (a symbol box,
// or a counter's text with its trailing space).
real_t WebList::_outside_marker_width(int p_idx, const MarkerBox &p_box) const {
	if (_get_item_marker_text(p_idx).is_empty()) {
		return 0;
	}
	if (_is_symbol_marker(p_idx)) {
		return _web_list_bullet_size(p_box.ascent) + 2;
	}
	return _get_text_size(_inside_marker_text(p_idx), true).width;
}

void WebList::_free_layout(Layout &r_layout) const {
	for (ItemBox &item : r_layout.items) {
		for (LineBox &line : item.lines) {
			if (line.rid.is_valid()) {
				TS->free_rid(line.rid);
			}
		}
		if (item.text_rid.is_valid()) {
			TS->free_rid(item.text_rid);
		}
	}
	r_layout.items.clear();
	r_layout.valid = false;
}

void WebList::_invalidate_layout() {
	_free_layout(layout_cache[0]);
	_free_layout(layout_cache[1]);
}

void WebList::_content_changed() {
	_invalidate_layout();
	update_minimum_size();
	queue_redraw();
}

// One pass shapes every item at `p_width`. An outside marker hangs in the
// list's start padding, beside the first line (it only raises that line's
// baseline when it reaches higher than the text). An inside marker is
// the first inline box of the first line (CSS `list-style-position: inside`):
// that line starts after it, the following lines at the item's start, and a
// marker taller than the text grows the first line box around their shared
// baseline.
void WebList::_build_layout(Layout &r_layout, real_t p_width) const {
	_free_layout(r_layout);
	r_layout.valid = true;
	r_layout.width = p_width;

	const Ref<StyleBox> box = theme_cache.normal_style;
	const bool rtl = is_layout_rtl();
	const real_t left = (box.is_valid() ? box->get_margin(SIDE_LEFT) : 0) + (rtl ? theme_cache.padding_inline_end : theme_cache.padding_inline_start);
	const real_t top = (box.is_valid() ? box->get_margin(SIDE_TOP) : 0) + (theme_cache.margin_block_start < 0 ? theme_cache.font_size : theme_cache.margin_block_start) + theme_cache.padding_block_start;
	const real_t right = (box.is_valid() ? box->get_margin(SIDE_RIGHT) : 0) + (rtl ? theme_cache.padding_inline_start : theme_cache.padding_inline_end);
	r_layout.left = left;
	r_layout.top = top;
	// `li { padding-left }` moves the item's text (and an inside marker) off the list's content edge.
	const real_t content_width = MAX((real_t)0.0, p_width - left - right);
	r_layout.item_pad = MAX((real_t)0.0, li_padding_inline_start + content_width * li_padding_inline_start_percent / 100);
	r_layout.item_width = MAX((real_t)0.0, content_width - r_layout.item_pad);

	const bool inside = list_style_position == LIST_STYLE_POSITION_INSIDE;
	const int line_height = _get_line_height();
	const int text_above = _text_baseline();
	const int text_below = line_height - text_above;
	const MarkerBox marker_box = _marker_box();
	const BitField<TextServer::LineBreakFlag> break_flags = TextServer::BREAK_MANDATORY | TextServer::BREAK_WORD_BOUND |
			TextServer::BREAK_TRIM_START_EDGE_SPACES | TextServer::BREAK_TRIM_END_EDGE_SPACES;
	const bool wrap = autowrap && r_layout.item_width > 0;

	real_t y = top;
	r_layout.items.resize(items.size());
	for (int i = 0; i < items.size(); i++) {
		ItemBox &item = r_layout.items.write[i];
		item.top = y;
		const bool has_marker = !_get_item_marker_text(i).is_empty();
		const bool inline_marker = inside && has_marker;
		item.marker_advance = inline_marker ? _inside_marker_advance(i, marker_box) : 0;

		Vector<Vector2i> ranges;
		if (theme_cache.font.is_valid()) {
			const String text = _get_translated_item_text(i);
			item.text_rid = TS->create_shaped_text();
			TS->shaped_text_add_string(item.text_rid, text, theme_cache.font->get_rids(), theme_cache.font_size, theme_cache.font->get_opentype_features(), language);
			for (int s = 0; s < TextServer::SPACING_MAX; s++) {
				TS->shaped_text_set_spacing(item.text_rid, TextServer::SpacingType(s), theme_cache.font->get_spacing(TextServer::SpacingType(s)));
			}
			if (text.is_empty()) {
				// One empty line box.
			} else if (!wrap) {
				ranges.push_back(Vector2i(0, text.length()));
			} else if (item.marker_advance > 0) {
				const PackedInt32Array first = TS->shaped_text_get_line_breaks(item.text_rid, MAX((real_t)1.0, r_layout.item_width - item.marker_advance), 0, break_flags);
				if (first.size() >= 2) {
					ranges.push_back(Vector2i(first[0], first[1]));
				}
				if (first.size() >= 4) {
					const PackedInt32Array rest = TS->shaped_text_get_line_breaks(item.text_rid, r_layout.item_width, first[2], break_flags);
					for (int b = 0; b + 1 < rest.size(); b += 2) {
						ranges.push_back(Vector2i(rest[b], rest[b + 1]));
					}
				}
			} else {
				const PackedInt32Array breaks = TS->shaped_text_get_line_breaks(item.text_rid, r_layout.item_width, 0, break_flags);
				for (int b = 0; b + 1 < breaks.size(); b += 2) {
					ranges.push_back(Vector2i(breaks[b], breaks[b + 1]));
				}
			}
		}
		if (ranges.is_empty()) {
			ranges.push_back(Vector2i(0, 0));
		}

		real_t line_top = 0;
		for (int l = 0; l < ranges.size(); l++) {
			LineBox line;
			line.start = ranges[l].x;
			line.end = ranges[l].y;
			if (item.text_rid.is_valid() && line.end > line.start) {
				line.rid = TS->shaped_text_substr(item.text_rid, line.start, line.end - line.start);
				line.width = TS->shaped_text_get_width(line.rid);
			}
			int above = text_above;
			int below = text_below;
			if (l == 0 && inline_marker) {
				above = MAX(above, marker_box.above);
				below = MAX(below, marker_box.below);
				line.x = item.marker_advance;
			} else if (l == 0 && has_marker) {
				// An outside marker keeps to the first line's baseline: one that reaches higher than the text
				// pushes the line down, while what it hangs below the baseline does not grow the line.
				above = MAX(above, marker_box.above);
			}
			line.top = line_top;
			line.baseline = above;
			line.height = above + below;
			line_top += line.height;
			item.lines.push_back(line);
		}
		item.height = line_top;
		y += item.height + theme_cache.item_spacing;
	}
	r_layout.content_bottom = items.is_empty() ? top : y - theme_cache.item_spacing;
}

const WebList::Layout &WebList::_get_layout(real_t p_width) const {
	// Slot 0 follows the control's own width; slot 1 answers a measurement at
	// another width without discarding the one the next draw needs.
	Layout &layout = layout_cache[Math::is_equal_approx(p_width, (real_t)get_size().width) ? 0 : 1];
	if (!layout.valid || !Math::is_equal_approx(layout.width, p_width)) {
		_build_layout(layout, p_width);
	}
	return layout;
}

// Where a line's text starts (x) and where its baseline sits (y).
Point2 WebList::_line_pen(const Layout &p_layout, const ItemBox &p_item, int p_line, bool p_rtl) const {
	const LineBox &line = p_item.lines[p_line];
	const real_t content = line.x + line.width;
	real_t x = p_layout.left + (p_rtl ? 0 : p_layout.item_pad);
	if (p_rtl) {
		// The line hugs the inline end; an inside marker follows the text there.
		x += p_layout.item_width - content;
	} else {
		if (text_alignment == HORIZONTAL_ALIGNMENT_CENTER) {
			x += Math::floor((p_layout.item_width - content) * 0.5);
		} else if (text_alignment == HORIZONTAL_ALIGNMENT_RIGHT) {
			x += p_layout.item_width - content;
		}
		x += line.x;
	}
	return Point2(x, p_item.top + line.top + line.baseline);
}

Rect2 WebList::_get_item_rect_for_width(int p_idx, real_t p_width) const {
	ERR_FAIL_INDEX_V(p_idx, items.size(), Rect2());
	const Layout &layout = _get_layout(p_width);
	ERR_FAIL_INDEX_V(p_idx, layout.items.size(), Rect2());
	const Ref<StyleBox> box = theme_cache.normal_style;
	return Rect2(layout.left, layout.items[p_idx].top, layout.item_width + layout.item_pad, layout.items[p_idx].height + (box.is_valid() ? box->get_minimum_size().height : 0));
}

Rect2 WebList::_get_item_rect(int p_idx) const {
	return _get_item_rect_for_width(p_idx, get_size().width);
}

real_t WebList::_content_height_for_width(real_t p_width) const {
	const Layout &layout = _get_layout(p_width);
	real_t height = layout.content_bottom - layout.top;
	height += (theme_cache.margin_block_start < 0 ? theme_cache.font_size : theme_cache.margin_block_start) +
			(theme_cache.margin_block_end < 0 ? theme_cache.font_size : theme_cache.margin_block_end) +
			theme_cache.padding_block_start + theme_cache.padding_block_end;
	if (theme_cache.normal_style.is_valid()) {
		height += theme_cache.normal_style->get_minimum_size().height;
	}
	return height;
}

real_t WebList::get_content_height_for_width(real_t p_width) const {
	return _content_height_for_width(p_width);
}

real_t WebList::get_max_content_width() const {
	const bool inside = list_style_position == LIST_STYLE_POSITION_INSIDE;
	const MarkerBox marker_box = _marker_box();
	real_t width = 0;
	for (int i = 0; i < items.size(); i++) {
		real_t item_width = _get_text_size(_get_translated_item_text(i)).width + li_padding_inline_start;
		if (inside) {
			item_width += _inside_marker_advance(i, marker_box);
		}
		width = MAX(width, item_width);
	}
	// Outside markers occupy the list's inline-start padding, not a text column.
	width += theme_cache.padding_inline_start + theme_cache.padding_inline_end;
	if (theme_cache.normal_style.is_valid()) {
		width += theme_cache.normal_style->get_minimum_size().width;
	}
	return width;
}

void WebList::set_autowrap(bool p_autowrap) {
	if (autowrap == p_autowrap) {
		return;
	}
	autowrap = p_autowrap;
	_content_changed();
}

bool WebList::is_autowrap() const {
	return autowrap;
}

struct _WebListGapLess {
	_FORCE_INLINE_ bool operator()(const Vector2 &a, const Vector2 &b) const { return a.x < b.x; }
};

// Text decorations after Chromium (Blink TextDecorationInfo / StrokeData):
// `auto` thickness is max(1px, font-size / 10); the underline's top sits
// max(1, ceil(t / 2))px under the baseline, line-through is centred a third of
// the primary face's whole-pixel ascent above it, an overline rests on that
// ascent. Solid and double strokes are whole pixels, dotted/dashed round the
// width and snap the centre, wavy keeps it fractional. Underlines and
// overlines skip ink: they stop where a glyph outline crosses them, dilated
// by the thickness (text-decoration-skip-ink: auto).
void WebList::_draw_decorations(RID p_ci, const RID &p_line, real_t p_x, real_t p_baseline, real_t p_width, const Color &p_text_color) const {
	if (decoration_lines == 0 || p_width <= 0 || theme_cache.font.is_null()) {
		return;
	}
	const real_t font_size = theme_cache.font_size;
	const Color color = decoration_color.a > 0 ? decoration_color : p_text_color;
	const real_t thickness = decoration_thickness > 0 ? decoration_thickness : MAX((real_t)1.0, font_size / 10.0);
	const real_t whole = MAX((real_t)1.0, Math::floor(thickness));
	real_t ascent = 0;
	TypedArray<RID> rids = theme_cache.font->get_rids();
	if (!rids.is_empty()) {
		ascent = Math::round(TS->font_get_ascent(rids[0], 1024) / 1024.0 * font_size); // the primary face, not its fallbacks.
	}

	auto ink_gaps = [&](real_t p_top) {
		Vector<Vector2> gaps;
		if (!p_line.is_valid()) {
			return gaps;
		}
		const real_t band0 = p_top - p_baseline;
		const real_t band1 = band0 + thickness;
		const real_t dilation = MIN(thickness, (real_t)13.0);
		const int count = TS->shaped_text_get_glyph_count(p_line);
		const Glyph *glyphs = TS->shaped_text_get_glyphs(p_line);
		real_t pen = 0;
		for (int i = 0; i < count; i++) {
			const Glyph &g = glyphs[i];
			const real_t gx = pen + g.x_off;
			pen += g.advance * g.repeat;
			if (g.font_rid == RID() || (g.flags & TextServer::GRAPHEME_IS_SPACE) == TextServer::GRAPHEME_IS_SPACE) {
				continue;
			}
			Dictionary outline = TS->font_get_glyph_contours(g.font_rid, g.font_size, g.index);
			PackedVector3Array points = outline.get("points", PackedVector3Array());
			PackedInt32Array ends = outline.get("contours", PackedInt32Array());
			real_t lo = 1e20, hi = -1e20;
			int start = 0;
			for (int c = 0; c < ends.size(); c++) {
				const int end = ends[c];
				for (int k = start; k <= end; k++) {
					const Vector3 a3 = points[k];
					const Vector3 b3 = points[k == end ? start : k + 1];
					const real_t ay = a3.y + g.y_off, by = b3.y + g.y_off;
					if (ay >= band0 && ay <= band1) {
						lo = MIN(lo, (real_t)a3.x);
						hi = MAX(hi, (real_t)a3.x);
					}
					const real_t edges[2] = { band0, band1 };
					for (real_t edge : edges) {
						if ((ay - edge) * (by - edge) < 0) {
							const real_t x = a3.x + (b3.x - a3.x) * (edge - ay) / (by - ay);
							lo = MIN(lo, x);
							hi = MAX(hi, x);
						}
					}
				}
				start = end + 1;
			}
			if (hi >= lo) {
				gaps.push_back(Vector2(p_x + gx + lo - dilation, p_x + gx + hi + dilation));
			}
		}
		gaps.sort_custom<_WebListGapLess>();
		return gaps;
	};

	auto paint_rect = [&](real_t p_from, real_t p_to, real_t p_y, real_t p_h, const Vector<Vector2> &p_gaps) {
		real_t from = p_from;
		for (const Vector2 &gap : p_gaps) {
			if (gap.y <= from || gap.x >= p_to) {
				continue;
			}
			if (gap.x > from) {
				RenderingServer::get_singleton()->canvas_item_add_rect(p_ci, Rect2(from, p_y, gap.x - from, p_h), color);
			}
			from = MAX(from, gap.y);
		}
		if (p_to > from) {
			RenderingServer::get_singleton()->canvas_item_add_rect(p_ci, Rect2(from, p_y, p_to - from, p_h), color);
		}
	};

	auto stroke = [&](real_t p_top, bool p_skip_ink) {
		const Vector<Vector2> gaps = p_skip_ink ? ink_gaps(p_top) : Vector<Vector2>();
		const real_t from = p_x, to = p_x + p_width;
		switch (decoration_style) {
			case 1: {
				const real_t top = Math::round(p_top);
				paint_rect(from, to, top, whole, gaps);
				paint_rect(from, to, top + Math::round(thickness + 1), whole, gaps);
			} break;
			case 2:
			case 3: {
				const real_t w = MAX((real_t)1.0, Math::round(thickness));
				real_t center = p_top + thickness * 0.5;
				center = ((int)w % 2 == 0) ? Math::floor(center) : Math::floor(center) + 0.5;
				const real_t top = center - w * 0.5;
				const bool dotted = decoration_style == 2;
				const real_t dash = dotted ? w : w * (w >= 3 ? 2 : 3);
				const real_t gap = dotted ? w : w * (w >= 3 ? 1 : 2);
				for (real_t x = from; x < to - 0.5; x += dash + gap) {
					const real_t end = MIN(x + dash, to);
					if (dotted && w > 3) {
						bool hidden = false;
						for (const Vector2 &g : gaps) {
							hidden = hidden || (x + w * 0.5 > g.x && x + w * 0.5 < g.y);
						}
						if (!hidden) {
							RenderingServer::get_singleton()->canvas_item_add_circle(p_ci, Vector2(x + w * 0.5, top + w * 0.5), w * 0.5, color, true);
						}
					} else {
						paint_rect(x, end, top, w, gaps);
					}
				}
			} break;
			case 4: {
				const real_t wavelength = 4.0 * thickness + 3.0;
				const real_t amplitude = (thickness + 1.0) * 0.5;
				const real_t center = p_top + thickness + 1.0; // the line top plus the double-line offset.
				const real_t step = MAX((real_t)0.5, wavelength / 16);
				Vector<Vector2> points;
				auto flush = [&]() {
					if (points.size() > 1) {
						Vector<Color> colors;
						colors.push_back(color);
						RenderingServer::get_singleton()->canvas_item_add_polyline(p_ci, points, colors, thickness, true);
					}
					points.clear();
				};
				for (real_t x = from;; x = MIN(x + step, to)) {
					bool hidden = false;
					for (const Vector2 &g : gaps) {
						hidden = hidden || (x > g.x && x < g.y);
					}
					if (hidden) {
						flush();
					} else {
						points.push_back(Vector2(x, center + amplitude * Math::sin((x - from) * Math::TAU / wavelength)));
					}
					if (x >= to) {
						break;
					}
				}
				flush();
			} break;
			default: {
				paint_rect(from, to, Math::round(p_top), whole, gaps);
			} break;
		}
	};

	if (decoration_lines & 1) {
		stroke(p_baseline + MAX((real_t)1.0, Math::ceil(thickness * 0.5)), true);
	}
	if (decoration_lines & 2) {
		stroke(p_baseline - ascent - whole, true);
	}
	if (decoration_lines & 4) {
		stroke(p_baseline - ascent / 3.0 - whole * 0.5, false);
	}
}

void WebList::set_outline_over_fill(bool p_enabled) {
	if (outline_over_fill == p_enabled) {
		return;
	}
	outline_over_fill = p_enabled;
	queue_redraw();
}

bool WebList::is_outline_over_fill() const {
	return outline_over_fill;
}

void WebList::set_text_background_color(const Color &p_color) {
	if (text_background_color == p_color) {
		return;
	}
	text_background_color = p_color;
	queue_redraw();
}

void WebList::set_li_padding_inline_start(real_t p_padding) {
	if (Math::is_equal_approx(li_padding_inline_start, p_padding)) {
		return;
	}
	li_padding_inline_start = p_padding;
	_content_changed();
}

real_t WebList::get_li_padding_inline_start() const {
	return li_padding_inline_start;
}

void WebList::set_li_padding_inline_start_percent(real_t p_percent) {
	if (Math::is_equal_approx(li_padding_inline_start_percent, p_percent)) {
		return;
	}
	li_padding_inline_start_percent = p_percent;
	_content_changed();
}

real_t WebList::get_li_padding_inline_start_percent() const {
	return li_padding_inline_start_percent;
}

Color WebList::get_text_background_color() const {
	return text_background_color;
}

void WebList::set_decoration_lines(int p_lines) {
	decoration_lines = p_lines;
	queue_redraw();
}
int WebList::get_decoration_lines() const {
	return decoration_lines;
}
void WebList::set_decoration_color(const Color &p_color) {
	decoration_color = p_color;
	queue_redraw();
}
Color WebList::get_decoration_color() const {
	return decoration_color;
}
void WebList::set_decoration_style(int p_style) {
	decoration_style = p_style;
	queue_redraw();
}
int WebList::get_decoration_style() const {
	return decoration_style;
}
void WebList::set_decoration_thickness(real_t p_thickness) {
	decoration_thickness = p_thickness;
	queue_redraw();
}
real_t WebList::get_decoration_thickness() const {
	return decoration_thickness;
}



void WebList::_update_hovered_item(const Point2 &p_position) {
	const int item = get_item_at_position(p_position);
	if (item == hovered_item) {
		return;
	}
	hovered_item = item;
	queue_redraw();
}

Size2 WebList::get_minimum_size() const {
	const real_t height = _content_height_for_width(get_size().width);
	if (autowrap) {
		// Like an autowrapping Label: no width of its own (the text wraps at
		// whatever width the list is given), as tall as that width makes it.
		return Size2(0, height);
	}
	return Size2(get_max_content_width(), height);
}

void WebList::gui_input(const Ref<InputEvent> &p_event) {
	Ref<InputEventMouseMotion> mm = p_event;
	if (mm.is_valid()) {
		_update_hovered_item(mm->get_position());
	}
}

void WebList::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_DRAW: {
			const RID ci = get_canvas_item();
			const Size2 size = get_size();
			Color opacity_modulate(1, 1, 1, opacity);

			Ref<StyleBox> container_style = hovered_item >= 0 && theme_cache.hover_style.is_valid() ? theme_cache.hover_style : theme_cache.normal_style;
			if (container_style.is_valid()) {
				container_style->draw(ci, Rect2(Point2(), size));
			}
			if (has_focus(true) && theme_cache.focus_style.is_valid()) {
				theme_cache.focus_style->draw(ci, Rect2(Point2(), size));
			}

			const Layout &layout = _get_layout(size.width);
			const bool rtl = is_layout_rtl();
			const bool outside = list_style_position == LIST_STYLE_POSITION_OUTSIDE;
			const Ref<Font> marker_font = theme_cache.marker_font.is_valid() ? theme_cache.marker_font : theme_cache.font;
			const MarkerBox marker_box = _marker_box();
			const int marker_font_size = marker_box.font_size;
			const real_t min_height = theme_cache.normal_style.is_valid() ? theme_cache.normal_style->get_minimum_size().height : 0;
			const Color outline_color = theme_cache.font_outline_color * opacity_modulate;

			for (int i = 0; i < items.size() && i < layout.items.size(); i++) {
				const ItemBox &item = layout.items[i];
				const Rect2 item_rect(layout.left, item.top, layout.item_width + layout.item_pad, item.height + min_height);
				Ref<StyleBox> item_style = i == hovered_item && theme_cache.item_hover_style.is_valid() ? theme_cache.item_hover_style : theme_cache.item_normal_style;
				if (item_style.is_valid()) {
					item_style->draw(ci, item_rect);
				}

				const String marker = _get_item_marker_text(i);
				const Color text_color = (i == hovered_item ? theme_cache.font_hover_color : theme_cache.font_color) * opacity_modulate;
				const Color marker_color = (i == hovered_item ? theme_cache.marker_hover_color : theme_cache.marker_color) * opacity_modulate;
				Ref<StyleBox> marker_style = i == hovered_item && theme_cache.marker_hover_style.is_valid() ? theme_cache.marker_hover_style : theme_cache.marker_normal_style;

				for (int l = 0; l < item.lines.size(); l++) {
					const LineBox &line = item.lines[l];
					const Point2 pen = _line_pen(layout, item, l, rtl);

					if (l == 0 && !marker.is_empty()) {
						const bool symbol = _is_symbol_marker(i);
						const int ascent = marker_box.ascent;
						const int bullet = _web_list_bullet_size(ascent);
						// The marker's own box and where it paints: inside it is the first inline box of the
						// line (before the text, or after it when the line runs right to left); outside it ends at
						// the item's border edge, and a symbol sits a baseline-derived distance away from it.
						real_t box_x;
						real_t box_width;
						real_t bullet_x = 0;
						if (outside) {
							const real_t edge = rtl ? item_rect.position.x + item_rect.size.x : item_rect.position.x;
							box_width = _outside_marker_width(i, marker_box);
							if (symbol) {
								// Blink: a margin of -(offset + 7 + 1) on a box that holds the symbol 1px in.
								const int offset = ascent * 2 / 3;
								bullet_x = rtl ? edge + offset + 7 - bullet : edge - offset - 7;
								box_x = bullet_x - 1;
							} else {
								box_x = rtl ? edge : edge - box_width;
							}
						} else {
							box_x = rtl ? pen.x + line.width : pen.x - line.x;
							box_width = item.marker_advance;
							bullet_x = rtl ? box_x + item.marker_advance - bullet : box_x;
						}
						if (marker_style.is_valid()) {
							marker_style->draw(ci, Rect2(box_x, item.top, box_width, line.height));
						}
						if (symbol) {
							// Blink (ListMarker::RelativeSymbolMarkerRect): the symbol is a third of the ascent up.
							const real_t bullet_y = pen.y - ascent + 3 * (ascent - ascent * 2 / 3) / 2;
							const Point2 center(bullet_x + bullet * 0.5, bullet_y + bullet * 0.5);
							if (list_style_type == LIST_STYLE_TYPE_DISC) {
								draw_circle(center, bullet * 0.5, marker_color, true, -1.0f, true);
							} else if (list_style_type == LIST_STYLE_TYPE_CIRCLE) {
								draw_circle(center, bullet * 0.5, marker_color, false, 1.0f, true);
							} else {
								draw_rect(Rect2(bullet_x, bullet_y, bullet, bullet), marker_color);
							}
						} else if (marker_font.is_valid()) {
							const String marker_text = _inside_marker_text(i);
							const Point2 marker_pen(box_x, pen.y);
							if (theme_cache.outline_size > 0 && !outline_over_fill) {
								marker_font->draw_string_outline(ci, marker_pen, marker_text, HORIZONTAL_ALIGNMENT_LEFT, -1, marker_font_size, theme_cache.outline_size, outline_color);
							}
							marker_font->draw_string(ci, marker_pen, marker_text, HORIZONTAL_ALIGNMENT_LEFT, -1, marker_font_size, marker_color);
							if (theme_cache.outline_size > 0 && outline_over_fill) {
								marker_font->draw_string_outline(ci, marker_pen, marker_text, HORIZONTAL_ALIGNMENT_LEFT, -1, marker_font_size, theme_cache.outline_size, outline_color);
							}
						}
					}

					if (!line.rid.is_valid()) {
						continue;
					}
					// Without wrapping a line is cut at the item's inline end.
					const real_t clip_r = autowrap ? (real_t)-1 : item_rect.position.x + item_rect.size.x;
					const real_t line_ascent = TS->shaped_text_get_ascent(line.rid);
					if (text_background_color.a > 0 && line.width > 0) {
						// An inline box's background covers its font's content area.
						const real_t ascent = theme_cache.text_ascent > 0 ? (real_t)theme_cache.text_ascent : line_ascent;
						const real_t descent = theme_cache.text_descent > 0 ? (real_t)theme_cache.text_descent : (real_t)TS->shaped_text_get_descent(line.rid);
						draw_rect(Rect2(pen.x, pen.y - ascent, line.width, ascent + descent), text_background_color * opacity_modulate);
					}
					const int selection_from = MAX(items[i].selection_from, line.start);
					const int selection_to = MIN(items[i].selection_to, line.end);
					if (selection_to > selection_from) {
						const real_t line_size = TS->shaped_text_get_size(line.rid).y;
						for (const Vector2 &range : TS->shaped_text_get_selection(line.rid, selection_from, selection_to)) {
							draw_rect(Rect2(pen.x + range.x, pen.y - line_ascent, range.y - range.x, line_size), get_theme_color(SNAME("selection_color"), SNAME("TextEdit")) * opacity_modulate);
						}
					}
					if (theme_cache.outline_size > 0 && !outline_over_fill) {
						TS->shaped_text_draw_outline(line.rid, ci, pen, -1, clip_r, theme_cache.outline_size, outline_color);
					}
					TS->shaped_text_draw(line.rid, ci, pen, -1, clip_r, text_color);
					if (theme_cache.outline_size > 0 && outline_over_fill) {
						// `-webkit-text-stroke` paints over the fill (paint-order: normal).
						TS->shaped_text_draw_outline(line.rid, ci, pen, -1, clip_r, theme_cache.outline_size, outline_color);
					}
					_draw_decorations(ci, line.rid, pen.x, pen.y, line.width, text_color);
				}
			}
		} break;

		case NOTIFICATION_RESIZED: {
			if (autowrap) {
				update_minimum_size();
			}
		} break;

		case NOTIFICATION_MOUSE_EXIT: {
			if (hovered_item != -1) {
				hovered_item = -1;
				queue_redraw();
			}
		} break;

		case NOTIFICATION_THEME_CHANGED:
		case NOTIFICATION_TRANSLATION_CHANGED:
		case NOTIFICATION_LAYOUT_DIRECTION_CHANGED: {
			_content_changed();
		} break;
	}
}

bool WebList::_set(const StringName &p_name, const Variant &p_value) {
	return property_helper.property_set_value(p_name, p_value);
}

void WebList::set_list_tag(ListTag p_tag) {
	if (list_tag == p_tag) {
		return;
	}
	list_tag = p_tag;
	if (list_tag == LIST_TAG_ORDERED && list_style_type == LIST_STYLE_TYPE_DISC) {
		list_style_type = LIST_STYLE_TYPE_DECIMAL;
	} else if (list_tag == LIST_TAG_UNORDERED && list_style_type == LIST_STYLE_TYPE_DECIMAL) {
		list_style_type = LIST_STYLE_TYPE_DISC;
	}
	_content_changed();
}

WebList::ListTag WebList::get_list_tag() const {
	return list_tag;
}

void WebList::set_ordered_type(OrderedType p_type) {
	if (ordered_type == p_type) {
		return;
	}
	ordered_type = p_type;
	switch (ordered_type) {
		case ORDERED_TYPE_DECIMAL:
			list_style_type = LIST_STYLE_TYPE_DECIMAL;
			break;
		case ORDERED_TYPE_DECIMAL_LEADING_ZERO:
			list_style_type = LIST_STYLE_TYPE_DECIMAL_LEADING_ZERO;
			break;
		case ORDERED_TYPE_LOWER_ALPHA:
			list_style_type = LIST_STYLE_TYPE_LOWER_ALPHA;
			break;
		case ORDERED_TYPE_UPPER_ALPHA:
			list_style_type = LIST_STYLE_TYPE_UPPER_ALPHA;
			break;
		case ORDERED_TYPE_LOWER_ROMAN:
			list_style_type = LIST_STYLE_TYPE_LOWER_ROMAN;
			break;
		case ORDERED_TYPE_UPPER_ROMAN:
			list_style_type = LIST_STYLE_TYPE_UPPER_ROMAN;
			break;
	}
	_content_changed();
}

WebList::OrderedType WebList::get_ordered_type() const {
	return ordered_type;
}

void WebList::set_start(int p_start) {
	if (start == p_start) {
		return;
	}
	start = p_start;
	_content_changed();
}

int WebList::get_start() const {
	return start;
}

void WebList::set_reversed(bool p_reversed) {
	if (reversed == p_reversed) {
		return;
	}
	reversed = p_reversed;
	_content_changed();
}

bool WebList::is_reversed() const {
	return reversed;
}

void WebList::set_list_style_type(ListStyleType p_type) {
	if (list_style_type == p_type) {
		return;
	}
	list_style_type = p_type;
	_content_changed();
}

WebList::ListStyleType WebList::get_list_style_type() const {
	return list_style_type;
}

void WebList::set_list_style_position(ListStylePosition p_position) {
	if (list_style_position == p_position) {
		return;
	}
	list_style_position = p_position;
	_content_changed();
}

WebList::ListStylePosition WebList::get_list_style_position() const {
	return list_style_position;
}

void WebList::set_box_sizing(BoxSizing p_box_sizing) {
	if (box_sizing == p_box_sizing) {
		return;
	}
	box_sizing = p_box_sizing;
	_content_changed();
}

WebList::BoxSizing WebList::get_box_sizing() const {
	return box_sizing;
}

void WebList::set_opacity(float p_opacity) {
	opacity = CLAMP(p_opacity, 0.0, 1.0);
	queue_redraw();
}

float WebList::get_opacity() const {
	return opacity;
}

void WebList::set_text_alignment(HorizontalAlignment p_alignment) {
	if (text_alignment == p_alignment) {
		return;
	}
	text_alignment = p_alignment;
	queue_redraw();
}

HorizontalAlignment WebList::get_text_alignment() const {
	return text_alignment;
}

void WebList::set_text_direction(TextDirection p_text_direction) {
	if (text_direction == p_text_direction) {
		return;
	}
	text_direction = p_text_direction;
	_content_changed();
}

Control::TextDirection WebList::get_text_direction() const {
	return text_direction;
}

void WebList::set_language(const String &p_language) {
	if (language == p_language) {
		return;
	}
	language = p_language;
	_content_changed();
}

String WebList::get_language() const {
	return language;
}

void WebList::add_item(const String &p_text) {
	Item item;
	item.text = p_text;
	items.push_back(item);
	notify_property_list_changed();
	_content_changed();
}

void WebList::set_item_count(int p_count) {
	ERR_FAIL_COND(p_count < 0);
	if (items.size() == p_count) {
		return;
	}
	items.resize(p_count);
	notify_property_list_changed();
	_content_changed();
}

int WebList::get_item_count() const {
	return items.size();
}

void WebList::remove_item(int p_idx) {
	ERR_FAIL_INDEX(p_idx, items.size());
	items.remove_at(p_idx);
	if (hovered_item >= items.size()) {
		hovered_item = -1;
	}
	notify_property_list_changed();
	_content_changed();
}

void WebList::clear() {
	items.clear();
	hovered_item = -1;
	notify_property_list_changed();
	_content_changed();
}

void WebList::set_item_text(int p_idx, const String &p_text) {
	ERR_FAIL_INDEX(p_idx, items.size());
	if (items[p_idx].text == p_text) {
		return;
	}
	items.write[p_idx].text = p_text;
	items.write[p_idx].selection_from = items.write[p_idx].selection_to = 0;
	_content_changed();
}

String WebList::get_item_text(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, items.size(), String());
	return items[p_idx].text;
}

void WebList::set_item_value(int p_idx, int p_value) {
	ERR_FAIL_INDEX(p_idx, items.size());
	items.write[p_idx].value = p_value;
	_content_changed();
}

int WebList::get_item_value(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, items.size(), 0);
	return items[p_idx].value;
}

void WebList::set_item_value_enabled(int p_idx, bool p_enabled) {
	ERR_FAIL_INDEX(p_idx, items.size());
	items.write[p_idx].value_enabled = p_enabled;
	_content_changed();
}

bool WebList::is_item_value_enabled(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, items.size(), false);
	return items[p_idx].value_enabled;
}

void WebList::set_item_custom_marker_text(int p_idx, const String &p_marker) {
	ERR_FAIL_INDEX(p_idx, items.size());
	items.write[p_idx].custom_marker_text = p_marker;
	_content_changed();
}

String WebList::get_item_custom_marker_text(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, items.size(), String());
	return items[p_idx].custom_marker_text;
}

void WebList::set_item_metadata(int p_idx, const Variant &p_metadata) {
	ERR_FAIL_INDEX(p_idx, items.size());
	items.write[p_idx].metadata = p_metadata;
}

Variant WebList::get_item_metadata(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, items.size(), Variant());
	return items[p_idx].metadata;
}

void WebList::set_item_tooltip(int p_idx, const String &p_tooltip) {
	ERR_FAIL_INDEX(p_idx, items.size());
	items.write[p_idx].tooltip = p_tooltip;
}

String WebList::get_item_tooltip(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, items.size(), String());
	return items[p_idx].tooltip;
}

void WebList::set_item_auto_translate_mode(int p_idx, AutoTranslateMode p_mode) {
	ERR_FAIL_INDEX(p_idx, items.size());
	if (items[p_idx].auto_translate_mode == p_mode) {
		return;
	}
	items.write[p_idx].auto_translate_mode = p_mode;
	_content_changed();
}

Node::AutoTranslateMode WebList::get_item_auto_translate_mode(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, items.size(), AUTO_TRANSLATE_MODE_INHERIT);
	return items[p_idx].auto_translate_mode;
}

Rect2 WebList::get_item_rect(int p_idx) const {
	return _get_item_rect(p_idx);
}

// The box around every line of an item's text (the marker is not part of it).
Rect2 WebList::get_item_text_rect(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, items.size(), Rect2());
	if (theme_cache.font.is_null()) {
		return Rect2();
	}
	const Layout &layout = _get_layout(get_size().width);
	ERR_FAIL_INDEX_V(p_idx, layout.items.size(), Rect2());
	const ItemBox &item = layout.items[p_idx];
	const bool rtl = is_layout_rtl();
	const real_t font_ascent = theme_cache.font->get_ascent(theme_cache.font_size);
	const real_t font_height = theme_cache.font->get_height(theme_cache.font_size);
	Rect2 rect;
	for (int l = 0; l < item.lines.size(); l++) {
		const LineBox &line = item.lines[l];
		const Point2 pen = _line_pen(layout, item, l, rtl);
		const real_t ascent = line.rid.is_valid() ? (real_t)TS->shaped_text_get_ascent(line.rid) : font_ascent;
		const real_t height = line.rid.is_valid() ? (real_t)TS->shaped_text_get_size(line.rid).y : font_height;
		const Rect2 line_rect(pen.x, pen.y - ascent, line.width, height);
		rect = l == 0 ? line_rect : rect.merge(line_rect);
	}
	return rect;
}

// The line of an item at `p_y`, clamped to the item's first and last lines.
int WebList::_line_at(const ItemBox &p_item, real_t p_y) const {
	for (int l = 0; l < p_item.lines.size(); l++) {
		if (p_y < p_item.top + p_item.lines[l].top + p_item.lines[l].height) {
			return l;
		}
	}
	return MAX(0, p_item.lines.size() - 1);
}

int WebList::get_item_text_column_at_pos(int p_idx, const Point2 &p_position) const {
	ERR_FAIL_INDEX_V(p_idx, items.size(), 0);
	const Layout &layout = _get_layout(get_size().width);
	ERR_FAIL_INDEX_V(p_idx, layout.items.size(), 0);
	const ItemBox &item = layout.items[p_idx];
	if (item.lines.is_empty()) {
		return 0;
	}
	const int l = _line_at(item, p_position.y);
	const LineBox &line = item.lines[l];
	if (!line.rid.is_valid()) {
		return line.start;
	}
	return TS->shaped_text_hit_test_position(line.rid, p_position.x - _line_pen(layout, item, l, is_layout_rtl()).x);
}

Point2 WebList::get_item_text_caret_position(int p_idx, int p_column) const {
	ERR_FAIL_INDEX_V(p_idx, items.size(), Point2());
	const Layout &layout = _get_layout(get_size().width);
	ERR_FAIL_INDEX_V(p_idx, layout.items.size(), Point2());
	const ItemBox &item = layout.items[p_idx];
	if (item.lines.is_empty() || theme_cache.font.is_null()) {
		return Point2();
	}
	const int column = CLAMP(p_column, 0, _get_translated_item_text(p_idx).length());
	// A column on a wrap boundary belongs to the line it starts.
	int l = item.lines.size() - 1;
	for (int i = 0; i < item.lines.size(); i++) {
		if (column < item.lines[i].end || (column == item.lines[i].end && (i + 1 == item.lines.size() || item.lines[i + 1].start > column))) {
			l = i;
			break;
		}
	}
	const LineBox &line = item.lines[l];
	const Point2 pen = _line_pen(layout, item, l, is_layout_rtl());
	if (!line.rid.is_valid()) {
		return Point2(pen.x, pen.y - theme_cache.font->get_ascent(theme_cache.font_size) + theme_cache.font->get_height(theme_cache.font_size) * 0.5);
	}
	const CaretInfo caret = TS->shaped_text_get_carets(line.rid, CLAMP(column, line.start, line.end));
	return Point2(pen.x + caret.l_caret.position.x, pen.y - TS->shaped_text_get_ascent(line.rid) + TS->shaped_text_get_size(line.rid).y * 0.5);
}

void WebList::set_item_text_selection(int p_idx, int p_from, int p_to) {
	ERR_FAIL_INDEX(p_idx, items.size());
	int length = _get_translated_item_text(p_idx).length();
	int from = CLAMP(MIN(p_from, p_to), 0, length);
	int to = CLAMP(MAX(p_from, p_to), 0, length);
	if (from == items[p_idx].selection_from && to == items[p_idx].selection_to) { return; }
	items.write[p_idx].selection_from = from;
	items.write[p_idx].selection_to = to;
	queue_redraw();
}

String WebList::get_item_selected_text(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, items.size(), String());
	return _get_translated_item_text(p_idx).substr(items[p_idx].selection_from, items[p_idx].selection_to - items[p_idx].selection_from);
}

int WebList::get_item_at_position(const Point2 &p_position) const {
	for (int i = 0; i < items.size(); i++) {
		if (_get_item_rect(i).has_point(p_position)) {
			return i;
		}
	}
	return -1;
}

String WebList::get_item_marker_text(int p_idx) const {
	return _get_item_marker_text(p_idx);
}

int WebList::get_hovered_item() const {
	return hovered_item;
}

void WebList::push_mouse_motion(const Ref<InputEventMouseMotion> &p_event) {
	ERR_FAIL_COND(p_event.is_null());
	_update_hovered_item(p_event->get_position());
}

String WebList::get_tooltip(const Point2 &p_pos) const {
	const int item = get_item_at_position(p_pos);
	if (item >= 0 && !items[item].tooltip.is_empty()) {
		return items[item].tooltip;
	}
	return Control::get_tooltip(p_pos);
}

void WebList::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_list_tag", "tag"), &WebList::set_list_tag);
	ClassDB::bind_method(D_METHOD("get_list_tag"), &WebList::get_list_tag);
	ClassDB::bind_method(D_METHOD("set_ordered_type", "type"), &WebList::set_ordered_type);
	ClassDB::bind_method(D_METHOD("get_ordered_type"), &WebList::get_ordered_type);
	ClassDB::bind_method(D_METHOD("set_start", "start"), &WebList::set_start);
	ClassDB::bind_method(D_METHOD("get_start"), &WebList::get_start);
	ClassDB::bind_method(D_METHOD("set_reversed", "reversed"), &WebList::set_reversed);
	ClassDB::bind_method(D_METHOD("is_reversed"), &WebList::is_reversed);
	ClassDB::bind_method(D_METHOD("set_list_style_type", "type"), &WebList::set_list_style_type);
	ClassDB::bind_method(D_METHOD("get_list_style_type"), &WebList::get_list_style_type);
	ClassDB::bind_method(D_METHOD("set_list_style_position", "position"), &WebList::set_list_style_position);
	ClassDB::bind_method(D_METHOD("get_list_style_position"), &WebList::get_list_style_position);
	ClassDB::bind_method(D_METHOD("set_box_sizing", "box_sizing"), &WebList::set_box_sizing);
	ClassDB::bind_method(D_METHOD("get_box_sizing"), &WebList::get_box_sizing);
	ClassDB::bind_method(D_METHOD("set_opacity", "opacity"), &WebList::set_opacity);
	ClassDB::bind_method(D_METHOD("set_autowrap", "autowrap"), &WebList::set_autowrap);
	ClassDB::bind_method(D_METHOD("is_autowrap"), &WebList::is_autowrap);
	ClassDB::bind_method(D_METHOD("get_content_height_for_width", "width"), &WebList::get_content_height_for_width);
	ClassDB::bind_method(D_METHOD("get_max_content_width"), &WebList::get_max_content_width);
	ClassDB::bind_method(D_METHOD("set_decoration_lines", "lines"), &WebList::set_decoration_lines);
	ClassDB::bind_method(D_METHOD("get_decoration_lines"), &WebList::get_decoration_lines);
	ClassDB::bind_method(D_METHOD("set_decoration_color", "color"), &WebList::set_decoration_color);
	ClassDB::bind_method(D_METHOD("get_decoration_color"), &WebList::get_decoration_color);
	ClassDB::bind_method(D_METHOD("set_decoration_style", "style"), &WebList::set_decoration_style);
	ClassDB::bind_method(D_METHOD("get_decoration_style"), &WebList::get_decoration_style);
	ClassDB::bind_method(D_METHOD("set_decoration_thickness", "thickness"), &WebList::set_decoration_thickness);
	ClassDB::bind_method(D_METHOD("get_decoration_thickness"), &WebList::get_decoration_thickness);
	ClassDB::bind_method(D_METHOD("get_opacity"), &WebList::get_opacity);
	ClassDB::bind_method(D_METHOD("set_outline_over_fill", "enabled"), &WebList::set_outline_over_fill);
	ClassDB::bind_method(D_METHOD("is_outline_over_fill"), &WebList::is_outline_over_fill);
	ClassDB::bind_method(D_METHOD("set_text_background_color", "color"), &WebList::set_text_background_color);
	ClassDB::bind_method(D_METHOD("get_text_background_color"), &WebList::get_text_background_color);
	ClassDB::bind_method(D_METHOD("set_li_padding_inline_start", "padding"), &WebList::set_li_padding_inline_start);
	ClassDB::bind_method(D_METHOD("get_li_padding_inline_start"), &WebList::get_li_padding_inline_start);
	ClassDB::bind_method(D_METHOD("set_li_padding_inline_start_percent", "percent"), &WebList::set_li_padding_inline_start_percent);
	ClassDB::bind_method(D_METHOD("get_li_padding_inline_start_percent"), &WebList::get_li_padding_inline_start_percent);
	ClassDB::bind_method(D_METHOD("set_text_alignment", "alignment"), &WebList::set_text_alignment);
	ClassDB::bind_method(D_METHOD("get_text_alignment"), &WebList::get_text_alignment);
	ClassDB::bind_method(D_METHOD("set_text_direction", "direction"), &WebList::set_text_direction);
	ClassDB::bind_method(D_METHOD("get_text_direction"), &WebList::get_text_direction);
	ClassDB::bind_method(D_METHOD("set_language", "language"), &WebList::set_language);
	ClassDB::bind_method(D_METHOD("get_language"), &WebList::get_language);

	ClassDB::bind_method(D_METHOD("add_item", "text"), &WebList::add_item);
	ClassDB::bind_method(D_METHOD("set_item_count", "count"), &WebList::set_item_count);
	ClassDB::bind_method(D_METHOD("get_item_count"), &WebList::get_item_count);
	ClassDB::bind_method(D_METHOD("remove_item", "idx"), &WebList::remove_item);
	ClassDB::bind_method(D_METHOD("clear"), &WebList::clear);
	ClassDB::bind_method(D_METHOD("set_item_text", "idx", "text"), &WebList::set_item_text);
	ClassDB::bind_method(D_METHOD("get_item_text", "idx"), &WebList::get_item_text);
	ClassDB::bind_method(D_METHOD("set_item_value", "idx", "value"), &WebList::set_item_value);
	ClassDB::bind_method(D_METHOD("get_item_value", "idx"), &WebList::get_item_value);
	ClassDB::bind_method(D_METHOD("set_item_value_enabled", "idx", "enabled"), &WebList::set_item_value_enabled);
	ClassDB::bind_method(D_METHOD("is_item_value_enabled", "idx"), &WebList::is_item_value_enabled);
	ClassDB::bind_method(D_METHOD("set_item_custom_marker_text", "idx", "marker"), &WebList::set_item_custom_marker_text);
	ClassDB::bind_method(D_METHOD("get_item_custom_marker_text", "idx"), &WebList::get_item_custom_marker_text);
	ClassDB::bind_method(D_METHOD("set_item_metadata", "idx", "metadata"), &WebList::set_item_metadata);
	ClassDB::bind_method(D_METHOD("get_item_metadata", "idx"), &WebList::get_item_metadata);
	ClassDB::bind_method(D_METHOD("set_item_tooltip", "idx", "tooltip"), &WebList::set_item_tooltip);
	ClassDB::bind_method(D_METHOD("get_item_tooltip", "idx"), &WebList::get_item_tooltip);
	ClassDB::bind_method(D_METHOD("set_item_auto_translate_mode", "idx", "mode"), &WebList::set_item_auto_translate_mode);
	ClassDB::bind_method(D_METHOD("get_item_auto_translate_mode", "idx"), &WebList::get_item_auto_translate_mode);
	ClassDB::bind_method(D_METHOD("get_item_rect", "idx"), &WebList::get_item_rect);
	ClassDB::bind_method(D_METHOD("get_item_text_rect", "idx"), &WebList::get_item_text_rect);
	ClassDB::bind_method(D_METHOD("get_item_text_column_at_pos", "idx", "position"), &WebList::get_item_text_column_at_pos);
	ClassDB::bind_method(D_METHOD("get_item_text_caret_position", "idx", "column"), &WebList::get_item_text_caret_position);
	ClassDB::bind_method(D_METHOD("set_item_text_selection", "idx", "from", "to"), &WebList::set_item_text_selection);
	ClassDB::bind_method(D_METHOD("get_item_selected_text", "idx"), &WebList::get_item_selected_text);
	ClassDB::bind_method(D_METHOD("get_item_at_position", "position"), &WebList::get_item_at_position);
	ClassDB::bind_method(D_METHOD("get_item_marker_text", "idx"), &WebList::get_item_marker_text);
	ClassDB::bind_method(D_METHOD("get_hovered_item"), &WebList::get_hovered_item);
	ClassDB::bind_method(D_METHOD("push_mouse_motion", "event"), &WebList::push_mouse_motion);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "list_tag", PROPERTY_HINT_ENUM, "Unordered <ul>,Ordered <ol>"), "set_list_tag", "get_list_tag");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "ordered_type", PROPERTY_HINT_ENUM, "Decimal,Decimal Leading Zero,Lower Alpha,Upper Alpha,Lower Roman,Upper Roman"), "set_ordered_type", "get_ordered_type");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "start"), "set_start", "get_start");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "reversed"), "set_reversed", "is_reversed");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "list_style_type", PROPERTY_HINT_ENUM, "Disc,Circle,Square,Decimal,Decimal Leading Zero,Lower Alpha,Upper Alpha,Lower Roman,Upper Roman,None"), "set_list_style_type", "get_list_style_type");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "list_style_position", PROPERTY_HINT_ENUM, "Outside,Inside"), "set_list_style_position", "get_list_style_position");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "box_sizing", PROPERTY_HINT_ENUM, "Content Box,Border Box"), "set_box_sizing", "get_box_sizing");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "opacity", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_opacity", "get_opacity");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "autowrap"), "set_autowrap", "is_autowrap");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "decoration_lines", PROPERTY_HINT_FLAGS, "Underline,Overline,Line Through"), "set_decoration_lines", "get_decoration_lines");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "decoration_color"), "set_decoration_color", "get_decoration_color");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "decoration_style", PROPERTY_HINT_ENUM, "Solid,Double,Dotted,Dashed,Wavy"), "set_decoration_style", "get_decoration_style");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "decoration_thickness", PROPERTY_HINT_NONE, "suffix:px"), "set_decoration_thickness", "get_decoration_thickness");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "outline_over_fill"), "set_outline_over_fill", "is_outline_over_fill");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "text_background_color"), "set_text_background_color", "get_text_background_color");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "li_padding_inline_start", PROPERTY_HINT_NONE, "suffix:px"), "set_li_padding_inline_start", "get_li_padding_inline_start");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "li_padding_inline_start_percent", PROPERTY_HINT_NONE, "suffix:%"), "set_li_padding_inline_start_percent", "get_li_padding_inline_start_percent");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "text_alignment", PROPERTY_HINT_ENUM, "Left,Center,Right,Fill"), "set_text_alignment", "get_text_alignment");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "text_direction", PROPERTY_HINT_ENUM, "Auto,Left-to-Right,Right-to-Left,Inherited"), "set_text_direction", "get_text_direction");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "language", PROPERTY_HINT_LOCALE_ID), "set_language", "get_language");

	ADD_ARRAY_COUNT("Items", "item_count", "set_item_count", "get_item_count", "item_");

	BIND_ENUM_CONSTANT(LIST_TAG_UNORDERED);
	BIND_ENUM_CONSTANT(LIST_TAG_ORDERED);
	BIND_ENUM_CONSTANT(ORDERED_TYPE_DECIMAL);
	BIND_ENUM_CONSTANT(ORDERED_TYPE_DECIMAL_LEADING_ZERO);
	BIND_ENUM_CONSTANT(ORDERED_TYPE_LOWER_ALPHA);
	BIND_ENUM_CONSTANT(ORDERED_TYPE_UPPER_ALPHA);
	BIND_ENUM_CONSTANT(ORDERED_TYPE_LOWER_ROMAN);
	BIND_ENUM_CONSTANT(ORDERED_TYPE_UPPER_ROMAN);
	BIND_ENUM_CONSTANT(LIST_STYLE_TYPE_DISC);
	BIND_ENUM_CONSTANT(LIST_STYLE_TYPE_CIRCLE);
	BIND_ENUM_CONSTANT(LIST_STYLE_TYPE_SQUARE);
	BIND_ENUM_CONSTANT(LIST_STYLE_TYPE_DECIMAL);
	BIND_ENUM_CONSTANT(LIST_STYLE_TYPE_DECIMAL_LEADING_ZERO);
	BIND_ENUM_CONSTANT(LIST_STYLE_TYPE_LOWER_ALPHA);
	BIND_ENUM_CONSTANT(LIST_STYLE_TYPE_UPPER_ALPHA);
	BIND_ENUM_CONSTANT(LIST_STYLE_TYPE_LOWER_ROMAN);
	BIND_ENUM_CONSTANT(LIST_STYLE_TYPE_UPPER_ROMAN);
	BIND_ENUM_CONSTANT(LIST_STYLE_TYPE_NONE);
	BIND_ENUM_CONSTANT(LIST_STYLE_POSITION_OUTSIDE);
	BIND_ENUM_CONSTANT(LIST_STYLE_POSITION_INSIDE);
	BIND_ENUM_CONSTANT(BOX_SIZING_CONTENT_BOX);
	BIND_ENUM_CONSTANT(BOX_SIZING_BORDER_BOX);

	BIND_THEME_ITEM_CUSTOM(Theme::DATA_TYPE_STYLEBOX, WebList, normal_style, "normal");
	BIND_THEME_ITEM_CUSTOM(Theme::DATA_TYPE_STYLEBOX, WebList, hover_style, "hover");
	BIND_THEME_ITEM_CUSTOM(Theme::DATA_TYPE_STYLEBOX, WebList, focus_style, "focus");
	BIND_THEME_ITEM_CUSTOM(Theme::DATA_TYPE_STYLEBOX, WebList, item_normal_style, "item_normal");
	BIND_THEME_ITEM_CUSTOM(Theme::DATA_TYPE_STYLEBOX, WebList, item_hover_style, "item_hover");
	BIND_THEME_ITEM_CUSTOM(Theme::DATA_TYPE_STYLEBOX, WebList, marker_normal_style, "marker_normal");
	BIND_THEME_ITEM_CUSTOM(Theme::DATA_TYPE_STYLEBOX, WebList, marker_hover_style, "marker_hover");
	BIND_THEME_ITEM(Theme::DATA_TYPE_FONT, WebList, font);
	BIND_THEME_ITEM(Theme::DATA_TYPE_FONT, WebList, marker_font);
	BIND_THEME_ITEM(Theme::DATA_TYPE_FONT_SIZE, WebList, font_size);
	BIND_THEME_ITEM(Theme::DATA_TYPE_FONT_SIZE, WebList, marker_font_size);
	BIND_THEME_ITEM(Theme::DATA_TYPE_COLOR, WebList, font_color);
	BIND_THEME_ITEM(Theme::DATA_TYPE_COLOR, WebList, font_hover_color);
	BIND_THEME_ITEM(Theme::DATA_TYPE_COLOR, WebList, marker_color);
	BIND_THEME_ITEM(Theme::DATA_TYPE_COLOR, WebList, marker_hover_color);
	BIND_THEME_ITEM(Theme::DATA_TYPE_COLOR, WebList, font_outline_color);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebList, outline_size);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebList, padding_inline_start);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebList, padding_inline_end);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebList, padding_block_start);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebList, padding_block_end);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebList, margin_block_start);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebList, margin_block_end);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebList, item_spacing);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebList, line_height);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebList, letter_spacing);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebList, font_weight);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebList, marker_font_weight);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebList, text_align);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebList, marker_ascent);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebList, marker_above);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebList, marker_below);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebList, text_ascent);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebList, text_descent);

	Item defaults;
	base_property_helper.set_prefix("item_");
	base_property_helper.set_array_length_getter(&WebList::get_item_count);
	base_property_helper.register_property(PropertyInfo(Variant::STRING, "text", PROPERTY_HINT_MULTILINE_TEXT), defaults.text, &WebList::set_item_text, &WebList::get_item_text);
	base_property_helper.register_property(PropertyInfo(Variant::INT, "value"), defaults.value, &WebList::set_item_value, &WebList::get_item_value);
	base_property_helper.register_property(PropertyInfo(Variant::BOOL, "value_enabled"), defaults.value_enabled, &WebList::set_item_value_enabled, &WebList::is_item_value_enabled);
	base_property_helper.register_property(PropertyInfo(Variant::STRING, "custom_marker_text"), defaults.custom_marker_text, &WebList::set_item_custom_marker_text, &WebList::get_item_custom_marker_text);
	base_property_helper.register_property(PropertyInfo(Variant::NIL, "metadata", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_STORE_IF_NULL), defaults.metadata, &WebList::set_item_metadata, &WebList::get_item_metadata);
	base_property_helper.register_property(PropertyInfo(Variant::STRING, "tooltip", PROPERTY_HINT_MULTILINE_TEXT), defaults.tooltip, &WebList::set_item_tooltip, &WebList::get_item_tooltip);
	base_property_helper.register_property(PropertyInfo(Variant::INT, "auto_translate_mode", PROPERTY_HINT_ENUM, "Inherit,Always,Disabled"), defaults.auto_translate_mode, &WebList::set_item_auto_translate_mode, &WebList::get_item_auto_translate_mode);
	PropertyListHelper::register_base_helper(get_class_static(), &base_property_helper);
}

WebList::WebList() {
	set_focus_mode(FOCUS_ACCESSIBILITY);
	property_helper.setup_for_instance(base_property_helper, this);
}

WebList::~WebList() {
	_free_layout(layout_cache[0]);
	_free_layout(layout_cache[1]);
}
