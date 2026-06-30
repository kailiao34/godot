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
#include "scene/resources/text_line.h"
#include "scene/resources/text_paragraph.h"
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


int WebList::_marker_column_width() const {
	int marker_width = theme_cache.marker_min_width;
	for (int i = 0; i < items.size(); i++) {
		marker_width = MAX(marker_width, (int)Math::ceil(_get_text_size(_get_item_marker_text(i), true).width));
	}
	return marker_width;
}

real_t WebList::_item_text_available(real_t p_item_width) const {
	const bool outside = list_style_position == LIST_STYLE_POSITION_OUTSIDE;
	return MAX((real_t)0.0, p_item_width - (outside ? 0 : _marker_column_width() + theme_cache.marker_gap));
}

Ref<TextParagraph> WebList::_shape_item_paragraph(int p_idx, real_t p_available) const {
	Ref<TextParagraph> paragraph;
	paragraph.instantiate();
	paragraph->add_string(_get_translated_item_text(p_idx), theme_cache.font, theme_cache.font_size);
	paragraph->set_break_flags(TextServer::BREAK_MANDATORY | TextServer::BREAK_WORD_BOUND |
			TextServer::BREAK_TRIM_START_EDGE_SPACES | TextServer::BREAK_TRIM_END_EDGE_SPACES);
	paragraph->set_width(p_available > 0 ? p_available : -1);
	return paragraph;
}

int WebList::_item_line_count(int p_idx, real_t p_available) const {
	if (!autowrap || theme_cache.font.is_null() || p_available <= 0) {
		return 1;
	}
	return MAX(1, _shape_item_paragraph(p_idx, p_available)->get_line_count());
}

Rect2 WebList::_get_item_rect_for_width(int p_idx, real_t p_width) const {
	ERR_FAIL_INDEX_V(p_idx, items.size(), Rect2());
	const Ref<StyleBox> box = theme_cache.normal_style;
	const Size2 min = box.is_valid() ? box->get_minimum_size() : Size2();
	const bool rtl = is_layout_rtl();
	const float left = (box.is_valid() ? box->get_margin(SIDE_LEFT) : 0) + (rtl ? theme_cache.padding_inline_end : theme_cache.padding_inline_start);
	const float top = (box.is_valid() ? box->get_margin(SIDE_TOP) : 0) + (theme_cache.margin_block_start < 0 ? theme_cache.font_size : theme_cache.margin_block_start) + theme_cache.padding_block_start;
	const float right = (box.is_valid() ? box->get_margin(SIDE_RIGHT) : 0) + (rtl ? theme_cache.padding_inline_start : theme_cache.padding_inline_end);
	const int line_height = _get_line_height();
	const float width = MAX(0.0, p_width - left - right);
	const real_t available = _item_text_available(width);
	float y = top;
	for (int i = 0; i < p_idx; i++) {
		y += _item_line_count(i, available) * line_height + theme_cache.item_spacing;
	}
	return Rect2(left, y, width, _item_line_count(p_idx, available) * line_height + min.height);
}

Rect2 WebList::_get_item_rect(int p_idx) const {
	return _get_item_rect_for_width(p_idx, get_size().width);
}

real_t WebList::_content_height_for_width(real_t p_width) const {
	real_t height = 0;
	if (!items.is_empty()) {
		const Rect2 last = _get_item_rect_for_width(items.size() - 1, p_width);
		const Ref<StyleBox> box = theme_cache.normal_style;
		const real_t top = (box.is_valid() ? box->get_margin(SIDE_TOP) : 0) + (theme_cache.margin_block_start < 0 ? theme_cache.font_size : theme_cache.margin_block_start) + theme_cache.padding_block_start;
		height = last.position.y + last.size.y - (box.is_valid() ? box->get_minimum_size().height : 0) - top;
	}
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
	real_t width = 0;
	for (int i = 0; i < items.size(); i++) {
		width = MAX(width, _get_text_size(_get_translated_item_text(i)).width);
	}
	if (list_style_position == LIST_STYLE_POSITION_INSIDE) {
		width += _marker_column_width() + theme_cache.marker_gap;
	} else {
		width += theme_cache.padding_inline_start + theme_cache.padding_inline_end;
	}
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
	update_minimum_size();
	queue_redraw();
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
	if (autowrap) {
		// Like an autowrapping Label: no width of its own (the text wraps at
		// whatever width the list is given), as tall as that width makes it.
		return Size2(0, _content_height_for_width(get_size().width));
	}
	Size2 content;
	int marker_width = theme_cache.marker_min_width;
	for (int i = 0; i < items.size(); i++) {
		marker_width = MAX(marker_width, (int)Math::ceil(_get_text_size(_get_item_marker_text(i), true).width));
		content.width = MAX(content.width, _get_text_size(_get_translated_item_text(i)).width);
	}

	if (list_style_position == LIST_STYLE_POSITION_INSIDE) {
		content.width += marker_width + theme_cache.marker_gap;
	} else {
		// Outside markers occupy the list's inline-start padding, not an extra text column.
		content.width += theme_cache.padding_inline_start + theme_cache.padding_inline_end;
	}

	const int line_height = _get_line_height();
	if (!items.is_empty()) {
		content.height = items.size() * line_height + MAX(0, items.size() - 1) * theme_cache.item_spacing;
	}
	content.height += (theme_cache.margin_block_start < 0 ? theme_cache.font_size : theme_cache.margin_block_start) +
			(theme_cache.margin_block_end < 0 ? theme_cache.font_size : theme_cache.margin_block_end) +
			theme_cache.padding_block_start + theme_cache.padding_block_end;

	if (theme_cache.normal_style.is_valid()) {
		content += theme_cache.normal_style->get_minimum_size();
	}

	return content;
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

			int marker_width = theme_cache.marker_min_width;
			for (int i = 0; i < items.size(); i++) {
				marker_width = MAX(marker_width, (int)Math::ceil(_get_text_size(_get_item_marker_text(i), true).width));
			}

			const bool rtl = is_layout_rtl();
			const int line_height = _get_line_height();
			const int font_size = theme_cache.font_size;
			const Ref<Font> marker_font = theme_cache.marker_font.is_valid() ? theme_cache.marker_font : theme_cache.font;
			const int marker_font_size = theme_cache.marker_font_size > 0 ? theme_cache.marker_font_size : font_size;
			const float baseline = Math::floor((line_height - (theme_cache.font.is_valid() ? theme_cache.font->get_height(font_size) : line_height)) * 0.5 + (theme_cache.font.is_valid() ? theme_cache.font->get_ascent(font_size) : font_size));
			const float marker_baseline = marker_font == theme_cache.font && marker_font_size == font_size ? baseline : Math::floor((line_height - (marker_font.is_valid() ? marker_font->get_height(marker_font_size) : line_height)) * 0.5 + (marker_font.is_valid() ? marker_font->get_ascent(marker_font_size) : marker_font_size));

			for (int i = 0; i < items.size(); i++) {
				Rect2 item_rect = _get_item_rect(i);
				Ref<StyleBox> item_style = i == hovered_item && theme_cache.item_hover_style.is_valid() ? theme_cache.item_hover_style : theme_cache.item_normal_style;
				if (item_style.is_valid()) {
					item_style->draw(ci, item_rect);
				}

				const String marker = _get_item_marker_text(i);
				const String text = _get_translated_item_text(i);
				const Color text_color = (i == hovered_item ? theme_cache.font_hover_color : theme_cache.font_color) * opacity_modulate;
				const Color marker_color = (i == hovered_item ? theme_cache.marker_hover_color : theme_cache.marker_color) * opacity_modulate;
				Ref<StyleBox> marker_style = i == hovered_item && theme_cache.marker_hover_style.is_valid() ? theme_cache.marker_hover_style : theme_cache.marker_normal_style;

				const bool outside = list_style_position == LIST_STYLE_POSITION_OUTSIDE;
				const float marker_gap = outside ? MAX(4.0f, marker_font_size * 0.28f) : theme_cache.marker_gap;
				float marker_x = outside ? item_rect.position.x - marker_gap - marker_width : item_rect.position.x;
				float text_x = outside ? item_rect.position.x : item_rect.position.x + marker_width + theme_cache.marker_gap;
				if (rtl) {
					const float inline_end = item_rect.position.x + item_rect.size.x;
					marker_x = outside ? inline_end + marker_gap : inline_end - marker_width;
					text_x = outside ? inline_end - _get_text_size(text).width : marker_x - theme_cache.marker_gap - _get_text_size(text).width;
				}

				if (marker_style.is_valid() && !marker.is_empty()) {
					marker_style->draw(ci, Rect2(marker_x, item_rect.position.y, marker_width, item_rect.size.y));
				}

				const bool native_bullet = outside && list_tag == LIST_TAG_UNORDERED && items[i].custom_marker_text.is_empty() &&
						(list_style_type == LIST_STYLE_TYPE_DISC || list_style_type == LIST_STYLE_TYPE_CIRCLE || list_style_type == LIST_STYLE_TYPE_SQUARE);
				if (native_bullet) {
					// Chromium paints the three unordered markers at the same size and
					// position. Font glyphs (especially U+2022) do not share those metrics.
					const float diameter = MAX(2.0f, Math::round(marker_font_size * 0.36f));
					const float radius = (diameter - 1.0f) * 0.5f;
					const float inline_start = rtl ? item_rect.position.x + item_rect.size.x : item_rect.position.x;
					const float marker_offset = 6.0f + marker_font_size * 0.535f;
					const Point2 center(inline_start + (rtl ? 1.0f : -1.0f) * marker_offset,
							item_rect.position.y + line_height * 0.5f + marker_font_size * 0.0625f);
					if (list_style_type == LIST_STYLE_TYPE_DISC) {
						draw_circle(center, radius, marker_color, true, -1.0f, true);
					} else if (list_style_type == LIST_STYLE_TYPE_CIRCLE) {
						const float stroke = MAX(1.0f, marker_font_size * 0.03f);
						draw_circle(center, radius - stroke * 0.5f, marker_color, false, stroke, true);
					} else {
						draw_rect(Rect2(center - Vector2(radius, radius), Size2(diameter, diameter)), marker_color);
					}
				} else if (marker_font.is_valid() && !marker.is_empty()) {
					marker_font->draw_string(ci, Point2(marker_x, item_rect.position.y + marker_baseline), marker, rtl ? HORIZONTAL_ALIGNMENT_LEFT : HORIZONTAL_ALIGNMENT_RIGHT, marker_width, marker_font_size, marker_color);
				}
				if (theme_cache.font.is_valid()) {
					const float available = MAX(0.0, item_rect.size.x - (outside ? 0 : marker_width + theme_cache.marker_gap));
					if (items[i].selection_to > items[i].selection_from) {
						Ref<TextLine> line = _shape_item_text(i);
						Rect2 rect = get_item_text_rect(i);
						for (const Vector2 &range : TS->shaped_text_get_selection(line->get_rid(), items[i].selection_from, items[i].selection_to)) {
							draw_rect(Rect2(rect.position + Point2(range.x, 0), Size2(range.y - range.x, rect.size.y)).intersection(Rect2(Point2(text_x, item_rect.position.y), Size2(available, rect.size.y))), get_theme_color(SNAME("selection_color"), SNAME("TextEdit")) * opacity_modulate);
						}
					}
					if (autowrap && available > 0) {
						// One line box per wrapped line, each aligned on its own.
						Ref<TextParagraph> paragraph = _shape_item_paragraph(i, available);
						for (int l = 0; l < paragraph->get_line_count(); l++) {
							const RID line_rid = paragraph->get_line_rid(l);
							const real_t line_width = paragraph->get_line_width(l);
							real_t lx = rtl ? text_x + _get_text_size(text).width - line_width : text_x;
							if (!rtl && text_alignment == HORIZONTAL_ALIGNMENT_CENTER) {
								lx += Math::floor((available - line_width) * 0.5);
							} else if (!rtl && text_alignment == HORIZONTAL_ALIGNMENT_RIGHT) {
								lx += available - line_width;
							}
							const Point2 pen(lx, item_rect.position.y + baseline + l * line_height);
							if (theme_cache.outline_size > 0) {
								TS->shaped_text_draw_outline(line_rid, ci, pen, -1, -1, theme_cache.outline_size, theme_cache.font_outline_color * opacity_modulate);
							}
							TS->shaped_text_draw(line_rid, ci, pen, -1, -1, text_color);
							_draw_decorations(ci, line_rid, lx, pen.y, line_width, text_color);
						}
					} else {
						if (theme_cache.outline_size > 0) {
							theme_cache.font->draw_string_outline(ci, Point2(text_x, item_rect.position.y + baseline), text, text_alignment, available, font_size, theme_cache.outline_size, theme_cache.font_outline_color * opacity_modulate);
						}
						theme_cache.font->draw_string(ci, Point2(text_x, item_rect.position.y + baseline), text, text_alignment, available, font_size, text_color);
						Ref<TextLine> single = _shape_item_text(i);
						const real_t single_width = MIN((real_t)single->get_line_width(), available);
						real_t sx = text_x;
						if (text_alignment == HORIZONTAL_ALIGNMENT_CENTER) {
							sx += Math::floor((available - single_width) * 0.5);
						} else if (text_alignment == HORIZONTAL_ALIGNMENT_RIGHT) {
							sx += available - single_width;
						}
						_draw_decorations(ci, single->get_rid(), sx, item_rect.position.y + baseline, single_width, text_color);
					}
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
			update_minimum_size();
			queue_redraw();
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
	update_minimum_size();
	queue_redraw();
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
	update_minimum_size();
	queue_redraw();
}

WebList::OrderedType WebList::get_ordered_type() const {
	return ordered_type;
}

void WebList::set_start(int p_start) {
	if (start == p_start) {
		return;
	}
	start = p_start;
	update_minimum_size();
	queue_redraw();
}

int WebList::get_start() const {
	return start;
}

void WebList::set_reversed(bool p_reversed) {
	if (reversed == p_reversed) {
		return;
	}
	reversed = p_reversed;
	update_minimum_size();
	queue_redraw();
}

bool WebList::is_reversed() const {
	return reversed;
}

void WebList::set_list_style_type(ListStyleType p_type) {
	if (list_style_type == p_type) {
		return;
	}
	list_style_type = p_type;
	update_minimum_size();
	queue_redraw();
}

WebList::ListStyleType WebList::get_list_style_type() const {
	return list_style_type;
}

void WebList::set_list_style_position(ListStylePosition p_position) {
	if (list_style_position == p_position) {
		return;
	}
	list_style_position = p_position;
	update_minimum_size();
	queue_redraw();
}

WebList::ListStylePosition WebList::get_list_style_position() const {
	return list_style_position;
}

void WebList::set_box_sizing(BoxSizing p_box_sizing) {
	if (box_sizing == p_box_sizing) {
		return;
	}
	box_sizing = p_box_sizing;
	update_minimum_size();
	queue_redraw();
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
	queue_redraw();
}

Control::TextDirection WebList::get_text_direction() const {
	return text_direction;
}

void WebList::set_language(const String &p_language) {
	if (language == p_language) {
		return;
	}
	language = p_language;
	queue_redraw();
}

String WebList::get_language() const {
	return language;
}

void WebList::add_item(const String &p_text) {
	Item item;
	item.text = p_text;
	items.push_back(item);
	notify_property_list_changed();
	update_minimum_size();
	queue_redraw();
}

void WebList::set_item_count(int p_count) {
	ERR_FAIL_COND(p_count < 0);
	if (items.size() == p_count) {
		return;
	}
	items.resize(p_count);
	notify_property_list_changed();
	update_minimum_size();
	queue_redraw();
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
	update_minimum_size();
	queue_redraw();
}

void WebList::clear() {
	items.clear();
	hovered_item = -1;
	notify_property_list_changed();
	update_minimum_size();
	queue_redraw();
}

void WebList::set_item_text(int p_idx, const String &p_text) {
	ERR_FAIL_INDEX(p_idx, items.size());
	if (items[p_idx].text == p_text) {
		return;
	}
	items.write[p_idx].text = p_text;
	items.write[p_idx].selection_from = items.write[p_idx].selection_to = 0;
	update_minimum_size();
	queue_redraw();
}

String WebList::get_item_text(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, items.size(), String());
	return items[p_idx].text;
}

void WebList::set_item_value(int p_idx, int p_value) {
	ERR_FAIL_INDEX(p_idx, items.size());
	items.write[p_idx].value = p_value;
	update_minimum_size();
	queue_redraw();
}

int WebList::get_item_value(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, items.size(), 0);
	return items[p_idx].value;
}

void WebList::set_item_value_enabled(int p_idx, bool p_enabled) {
	ERR_FAIL_INDEX(p_idx, items.size());
	items.write[p_idx].value_enabled = p_enabled;
	update_minimum_size();
	queue_redraw();
}

bool WebList::is_item_value_enabled(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, items.size(), false);
	return items[p_idx].value_enabled;
}

void WebList::set_item_custom_marker_text(int p_idx, const String &p_marker) {
	ERR_FAIL_INDEX(p_idx, items.size());
	items.write[p_idx].custom_marker_text = p_marker;
	update_minimum_size();
	queue_redraw();
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
	update_minimum_size();
	queue_redraw();
}

Node::AutoTranslateMode WebList::get_item_auto_translate_mode(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, items.size(), AUTO_TRANSLATE_MODE_INHERIT);
	return items[p_idx].auto_translate_mode;
}

Rect2 WebList::get_item_rect(int p_idx) const {
	return _get_item_rect(p_idx);
}

Ref<TextLine> WebList::_shape_item_text(int p_idx) const {
	Ref<TextLine> line = memnew(TextLine(_get_translated_item_text(p_idx), theme_cache.font, theme_cache.font_size));
	return line;
}

Rect2 WebList::get_item_text_rect(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, items.size(), Rect2());
	if (theme_cache.font.is_null()) { return Rect2(); }
	Rect2 rect = _get_item_rect(p_idx);
	int marker_width = theme_cache.marker_min_width;
	for (int i = 0; i < items.size(); i++) {
		marker_width = MAX(marker_width, (int)Math::ceil(_get_text_size(_get_item_marker_text(i), true).width));
	}
	bool outside = list_style_position == LIST_STYLE_POSITION_OUTSIDE;
	float available = MAX(0.0f, rect.size.x - (outside ? 0 : marker_width + theme_cache.marker_gap));
	Ref<TextLine> line = _shape_item_text(p_idx);
	float width = line->get_line_width();
	float x = outside ? rect.position.x : rect.position.x + marker_width + theme_cache.marker_gap;
	if (is_layout_rtl()) {
		x = rect.position.x + rect.size.x - _get_text_size(_get_translated_item_text(p_idx)).width - (outside ? 0 : marker_width + theme_cache.marker_gap);
	}
	if (text_alignment == HORIZONTAL_ALIGNMENT_CENTER && width <= available) {
		x += Math::floor((available - width) * 0.5);
	} else if (text_alignment == HORIZONTAL_ALIGNMENT_RIGHT) {
		x += available - width;
	}
	float baseline = Math::floor((_get_line_height() - theme_cache.font->get_height(theme_cache.font_size)) * 0.5 + theme_cache.font->get_ascent(theme_cache.font_size));
	return Rect2(Point2(x, rect.position.y + baseline - line->get_line_ascent()), line->get_size());
}

int WebList::get_item_text_column_at_pos(int p_idx, const Point2 &p_position) const {
	ERR_FAIL_INDEX_V(p_idx, items.size(), 0);
	return _shape_item_text(p_idx)->hit_test(p_position.x - get_item_text_rect(p_idx).position.x);
}

Point2 WebList::get_item_text_caret_position(int p_idx, int p_column) const {
	ERR_FAIL_INDEX_V(p_idx, items.size(), Point2());
	Ref<TextLine> line = _shape_item_text(p_idx);
	CaretInfo caret = TS->shaped_text_get_carets(line->get_rid(), CLAMP(p_column, 0, _get_translated_item_text(p_idx).length()));
	Rect2 rect = get_item_text_rect(p_idx);
	return rect.position + Point2(caret.l_caret.position.x, rect.size.y * 0.5);
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
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebList, marker_gap);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebList, marker_min_width);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebList, item_spacing);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebList, line_height);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebList, letter_spacing);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebList, font_weight);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebList, marker_font_weight);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebList, text_align);

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
