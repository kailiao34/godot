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

Rect2 WebList::_get_item_rect(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, items.size(), Rect2());

	const Ref<StyleBox> box = theme_cache.normal_style;
	const Size2 min = box.is_valid() ? box->get_minimum_size() : Size2();
	const float left = (box.is_valid() ? box->get_margin(SIDE_LEFT) : 0) + theme_cache.padding_inline_start;
	const float top = (box.is_valid() ? box->get_margin(SIDE_TOP) : 0) + theme_cache.margin_block_start + theme_cache.padding_block_start;
	const float right = (box.is_valid() ? box->get_margin(SIDE_RIGHT) : 0) + theme_cache.padding_inline_end;
	const int line_height = _get_line_height();
	const float y = top + p_idx * (line_height + theme_cache.item_spacing);
	const float width = MAX(0.0, get_size().width - left - right);
	return Rect2(left, y, width, line_height + min.height);
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
	Size2 content;
	int marker_width = theme_cache.marker_min_width;
	for (int i = 0; i < items.size(); i++) {
		marker_width = MAX(marker_width, (int)Math::ceil(_get_text_size(_get_item_marker_text(i), true).width));
		content.width = MAX(content.width, _get_text_size(_get_translated_item_text(i)).width);
	}

	if (list_style_position == LIST_STYLE_POSITION_INSIDE) {
		content.width += marker_width + theme_cache.marker_gap;
	} else {
		content.width += theme_cache.padding_inline_start + marker_width + theme_cache.marker_gap + theme_cache.padding_inline_end;
	}

	const int line_height = _get_line_height();
	if (!items.is_empty()) {
		content.height = items.size() * line_height + MAX(0, items.size() - 1) * theme_cache.item_spacing;
	}
	content.height += theme_cache.margin_block_start + theme_cache.margin_block_end + theme_cache.padding_block_start + theme_cache.padding_block_end;

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
			const int marker_font_size = theme_cache.marker_font_size > 0 ? theme_cache.marker_font_size : font_size;
			const float baseline = Math::floor((line_height - (theme_cache.font.is_valid() ? theme_cache.font->get_height(font_size) : line_height)) * 0.5 + (theme_cache.font.is_valid() ? theme_cache.font->get_ascent(font_size) : font_size));
			const float marker_baseline = Math::floor((line_height - ((theme_cache.marker_font.is_valid() ? theme_cache.marker_font : theme_cache.font).is_valid() ? (theme_cache.marker_font.is_valid() ? theme_cache.marker_font : theme_cache.font)->get_height(marker_font_size) : line_height)) * 0.5 + ((theme_cache.marker_font.is_valid() ? theme_cache.marker_font : theme_cache.font).is_valid() ? (theme_cache.marker_font.is_valid() ? theme_cache.marker_font : theme_cache.font)->get_ascent(marker_font_size) : marker_font_size));

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

				float marker_x = item_rect.position.x;
				float text_x = item_rect.position.x + marker_width + theme_cache.marker_gap;
				if (list_style_position == LIST_STYLE_POSITION_OUTSIDE) {
					marker_x = item_rect.position.x;
				} else {
					marker_x = item_rect.position.x;
					text_x = marker_x + marker_width + theme_cache.marker_gap;
				}
				if (rtl) {
					marker_x = item_rect.position.x + item_rect.size.x - marker_width;
					text_x = marker_x - theme_cache.marker_gap - _get_text_size(text).width;
				}

				if (marker_style.is_valid() && !marker.is_empty()) {
					marker_style->draw(ci, Rect2(marker_x, item_rect.position.y, marker_width, item_rect.size.y));
				}

				if (theme_cache.marker_font.is_valid() && !marker.is_empty()) {
					theme_cache.marker_font->draw_string(ci, Point2(marker_x, item_rect.position.y + marker_baseline), marker, HORIZONTAL_ALIGNMENT_RIGHT, marker_width, marker_font_size, marker_color);
				}
				if (theme_cache.font.is_valid()) {
					const float available = MAX(0.0, item_rect.size.x - marker_width - theme_cache.marker_gap);
					if (theme_cache.outline_size > 0) {
						theme_cache.font->draw_string_outline(ci, Point2(text_x, item_rect.position.y + baseline), text, text_alignment, available, font_size, theme_cache.outline_size, theme_cache.font_outline_color * opacity_modulate);
					}
					theme_cache.font->draw_string(ci, Point2(text_x, item_rect.position.y + baseline), text, text_alignment, available, font_size, text_color);
				}
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
