/**************************************************************************/
/*  web_select.cpp                                                        */
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

#include "web_select.h"

#include "core/input/input_event.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/time.h"
#include "scene/scene_string_names.h"
#include "scene/resources/style_box_flat.h"
#include "scene/resources/texture.h"
#include "scene/theme/theme_db.h"
#include "servers/display/accessibility_server.h"

bool WebSelect::_is_listbox() const {
	return multiple || size_rows > 1;
}

bool WebSelect::_is_item_selectable(int p_idx) const {
	return p_idx >= 0 && p_idx < items.size() && !items[p_idx].disabled && !items[p_idx].separator;
}

int WebSelect::_get_first_selectable_item() const {
	for (int i = 0; i < items.size(); i++) {
		if (_is_item_selectable(i)) {
			return i;
		}
	}
	return -1;
}

int WebSelect::_get_next_selectable_item(int p_from, int p_dir) const {
	if (items.is_empty()) {
		return -1;
	}

	int idx = CLAMP(p_from, 0, items.size() - 1);
	for (int i = 0; i < items.size(); i++) {
		idx = Math::posmod(idx + p_dir, items.size());
		if (_is_item_selectable(idx)) {
			return idx;
		}
	}
	return -1;
}

int WebSelect::_get_visible_rows() const {
	if (!_is_listbox()) {
		return 1;
	}
	return MAX(1, size_rows > 0 ? size_rows : MIN(4, MAX(1, items.size())));
}

int WebSelect::_get_option_height() const {
	return MAX(theme_cache.option_min_height, theme_cache.font->get_height(theme_cache.font_size) + theme_cache.option->get_minimum_size().height);
}

Rect2 WebSelect::_get_list_content_rect() const {
	Ref<StyleBox> style = theme_cache.listbox;
	Rect2 rect(Point2(), get_size());
	return Rect2(rect.position + style->get_offset(), rect.size - style->get_minimum_size());
}

Rect2 WebSelect::_get_option_rect(int p_idx) const {
	if (!_is_listbox() || p_idx < scroll_offset || p_idx >= scroll_offset + _get_visible_rows()) {
		return Rect2();
	}
	Rect2 content = _get_list_content_rect();
	const int option_height = _get_option_height();
	return Rect2(content.position.x, content.position.y + (p_idx - scroll_offset) * option_height, content.size.x, option_height);
}

Ref<StyleBox> WebSelect::_get_button_stylebox() const {
	if (disabled) {
		return theme_cache.disabled;
	}
	if (popup->is_visible()) {
		return theme_cache.open.is_valid() ? theme_cache.open : theme_cache.pressed;
	}
	if (hovered >= -1 && has_point(get_local_mouse_position())) {
		return theme_cache.hover;
	}
	return theme_cache.normal;
}

Dictionary WebSelect::_stylebox_metrics(const Ref<StyleBox> &p_style) const {
	Dictionary metrics;
	metrics["valid"] = p_style.is_valid();
	if (p_style.is_null()) {
		return metrics;
	}

	metrics["class"] = p_style->get_class();
	metrics["minimum_size"] = p_style->get_minimum_size();
	metrics["offset"] = p_style->get_offset();

	Dictionary margins;
	margins["left"] = p_style->get_margin(SIDE_LEFT);
	margins["top"] = p_style->get_margin(SIDE_TOP);
	margins["right"] = p_style->get_margin(SIDE_RIGHT);
	margins["bottom"] = p_style->get_margin(SIDE_BOTTOM);
	metrics["margins"] = margins;

	StyleBoxFlat *flat = Object::cast_to<StyleBoxFlat>(p_style.ptr());
	if (flat) {
		metrics["bg_color"] = flat->get_bg_color();
		metrics["border_color"] = flat->get_border_color();

		Dictionary border_widths;
		border_widths["left"] = flat->get_border_width(SIDE_LEFT);
		border_widths["top"] = flat->get_border_width(SIDE_TOP);
		border_widths["right"] = flat->get_border_width(SIDE_RIGHT);
		border_widths["bottom"] = flat->get_border_width(SIDE_BOTTOM);
		metrics["border_widths"] = border_widths;

		Dictionary corner_radii;
		corner_radii["top_left"] = flat->get_corner_radius(CORNER_TOP_LEFT);
		corner_radii["top_right"] = flat->get_corner_radius(CORNER_TOP_RIGHT);
		corner_radii["bottom_right"] = flat->get_corner_radius(CORNER_BOTTOM_RIGHT);
		corner_radii["bottom_left"] = flat->get_corner_radius(CORNER_BOTTOM_LEFT);
		metrics["corner_radii"] = corner_radii;
	}

	return metrics;
}

void WebSelect::_queue_redraw_and_minimum_size_update() {
	queue_redraw();
	update_minimum_size();
}

void WebSelect::_sync_popup() {
	popup->clear();
	for (int i = 0; i < items.size(); i++) {
		const Item &item = items[i];
		if (item.separator) {
			popup->add_separator(item.optgroup.is_empty() ? String() : item.text);
			continue;
		}

		if (appearance == APPEARANCE_BASE_SELECT) {
			if (item.selected && theme_cache.checkmark_icon.is_valid()) {
				popup->add_icon_item(theme_cache.checkmark_icon, item.text, i);
			} else if (item.icon.is_valid()) {
				popup->add_icon_item(item.icon, item.text, i);
			} else {
				popup->add_item(item.text, i);
			}
		} else if (item.icon.is_valid()) {
			popup->add_icon_item(item.icon, item.text, i);
		} else {
			popup->add_item(item.text, i);
		}
		popup->set_item_disabled(i, item.disabled);
		popup->set_item_metadata(i, item.metadata);
		popup->set_item_tooltip(i, item.tooltip);
	}
	if (current >= 0 && current < popup->get_item_count()) {
		popup->set_focused_item(current);
	}
}

void WebSelect::_apply_popup_theme() {
	popup->add_theme_style_override(SceneStringName(panel), theme_cache.picker);
	popup->add_theme_style_override(SceneStringName(hover), theme_cache.option_hover);
	popup->add_theme_style_override("separator", theme_cache.separator);
	popup->add_theme_style_override("labeled_separator_left", theme_cache.optgroup);
	popup->add_theme_style_override("labeled_separator_right", theme_cache.optgroup);
	popup->add_theme_color_override(SceneStringName(font_color), theme_cache.font_color);
	popup->add_theme_color_override("font_hover_color", theme_cache.font_hover_color);
	popup->add_theme_color_override("font_disabled_color", theme_cache.font_disabled_color);
	popup->add_theme_color_override("font_separator_color", theme_cache.font_disabled_color);
	popup->add_theme_font_override(SceneStringName(font), theme_cache.font);
	popup->add_theme_font_size_override(SceneStringName(font_size), theme_cache.font_size);
	popup->add_theme_icon_override("radio_checked", theme_cache.checkmark_icon);
	popup->add_theme_icon_override("radio_checked_disabled", theme_cache.checkmark_icon);
	popup->add_theme_constant_override("h_separation", theme_cache.icon_separation);
	popup->add_theme_constant_override("v_separation", 0);
	popup->add_theme_constant_override("item_start_padding", MAX(0, theme_cache.option->get_margin(SIDE_LEFT)));
	popup->add_theme_constant_override("item_end_padding", MAX(0, theme_cache.option->get_margin(SIDE_RIGHT)));
	popup->add_theme_constant_override("icon_max_width", 0);
}

void WebSelect::_popup_index_pressed(int p_index) {
	if (p_index < 0 || p_index >= popup->get_item_count()) {
		return;
	}
	const int id = popup->get_item_id(p_index);
	if (_is_item_selectable(id)) {
		_select_single(id, true);
	}
}

void WebSelect::_select(int p_idx, bool p_selected, bool p_emit) {
	ERR_FAIL_INDEX(p_idx, items.size());
	if (!_is_item_selectable(p_idx)) {
		return;
	}

	if (!multiple) {
		_select_single(p_idx, p_emit);
		return;
	}

	if (items.write[p_idx].selected == p_selected && !allow_reselect) {
		return;
	}
	items.write[p_idx].selected = p_selected;
	current = p_idx;
	focused_item = p_idx;
	queue_accessibility_update();
	_queue_redraw_and_minimum_size_update();
	if (p_emit) {
		emit_signal(SNAME("item_selected"), p_idx);
		emit_signal(SNAME("changed"));
	}
}

void WebSelect::_select_single(int p_idx, bool p_emit) {
	ERR_FAIL_INDEX(p_idx, items.size());
	if (!_is_item_selectable(p_idx)) {
		return;
	}
	if (current == p_idx && items[p_idx].selected && !allow_reselect) {
		bool only_selected = true;
		for (int i = 0; i < items.size(); i++) {
			if (i != p_idx && items[i].selected) {
				only_selected = false;
				break;
			}
		}
		if (only_selected) {
			return;
		}
	}

	for (int i = 0; i < items.size(); i++) {
		items.write[i].selected = i == p_idx;
	}
	current = p_idx;
	focused_item = p_idx;
	queue_accessibility_update();
	_queue_redraw_and_minimum_size_update();
	if (p_emit) {
		emit_signal(SNAME("item_selected"), p_idx);
		emit_signal(SNAME("changed"));
	}
}

void WebSelect::_ensure_focused_visible() {
	if (!_is_listbox() || focused_item < 0) {
		return;
	}
	if (focused_item < scroll_offset) {
		scroll_offset = focused_item;
	} else if (focused_item >= scroll_offset + _get_visible_rows()) {
		scroll_offset = focused_item - _get_visible_rows() + 1;
	}
	scroll_offset = CLAMP(scroll_offset, 0, MAX(0, items.size() - _get_visible_rows()));
}

void WebSelect::_activate_focused() {
	if (!_is_item_selectable(focused_item)) {
		return;
	}
	if (multiple) {
		_select(focused_item, !items[focused_item].selected, true);
	} else {
		_select_single(focused_item, true);
		hide_picker();
	}
}

void WebSelect::_search_item(const String &p_text) {
	if (p_text.is_empty()) {
		return;
	}

	const uint64_t now = Time::get_singleton()->get_ticks_msec();
	if (now - search_time_msec > 1000) {
		search_string.clear();
	}
	search_time_msec = now;
	search_string += p_text.to_lower();

	const int start = focused_item >= 0 ? focused_item : current;
	for (int i = 1; i <= items.size(); i++) {
		const int idx = Math::posmod(start + i, items.size());
		if (_is_item_selectable(idx) && items[idx].text.to_lower().begins_with(search_string)) {
			focused_item = idx;
			if (!_is_listbox()) {
				_select_single(idx, true);
			}
			_ensure_focused_visible();
			queue_redraw();
			return;
		}
	}
}

void WebSelect::_draw_closed_select() {
	Ref<StyleBox> style = _get_button_stylebox();
	const Rect2 rect(Point2(), get_size());
	draw_style_box(style, rect);

	if (has_focus()) {
		draw_style_box(theme_cache.focus, rect);
	}

	Rect2 content = Rect2(style->get_offset(), get_size() - style->get_minimum_size());
	if (theme_cache.picker_icon.is_valid()) {
		const Size2 icon_size = theme_cache.picker_icon->get_size();
		const Point2 icon_pos(content.position.x + content.size.x - icon_size.x - theme_cache.picker_icon_margin, content.position.y + (content.size.y - icon_size.y) * 0.5);
		draw_texture(theme_cache.picker_icon, icon_pos, disabled ? theme_cache.font_disabled_color : theme_cache.font_color);
		content.size.x -= icon_size.x + theme_cache.h_separation + theme_cache.picker_icon_margin;
	}

	if (current >= 0 && current < items.size()) {
		const Item &item = items[current];
		Point2 text_pos = content.position;
		if (item.icon.is_valid()) {
			const Size2 icon_size = item.icon->get_size();
			draw_texture(item.icon, Point2(content.position.x, content.position.y + (content.size.y - icon_size.y) * 0.5), disabled ? Color(1, 1, 1, 0.45) : Color(1, 1, 1));
			text_pos.x += icon_size.x + theme_cache.icon_separation;
		}
		const Color font_color = disabled ? theme_cache.font_disabled_color : theme_cache.font_color;
		theme_cache.font->draw_string(get_canvas_item(), Point2(text_pos.x, content.position.y + (content.size.y - theme_cache.font->get_height(theme_cache.font_size)) * 0.5 + theme_cache.font->get_ascent(theme_cache.font_size)), item.text, HORIZONTAL_ALIGNMENT_LEFT, MAX(0, content.size.x - (text_pos.x - content.position.x)), theme_cache.font_size, font_color);
	} else {
		theme_cache.font->draw_string(get_canvas_item(), Point2(content.position.x, content.position.y + (content.size.y - theme_cache.font->get_height(theme_cache.font_size)) * 0.5 + theme_cache.font->get_ascent(theme_cache.font_size)), String(), HORIZONTAL_ALIGNMENT_LEFT, content.size.x, theme_cache.font_size, theme_cache.font_placeholder_color);
	}
}

void WebSelect::_draw_option(int p_idx, const Rect2 &p_rect, bool p_in_popup) {
	if (p_idx < 0 || p_idx >= items.size()) {
		return;
	}
	const Item &item = items[p_idx];
	Ref<StyleBox> style = theme_cache.option;
	Color font_color = theme_cache.font_color;

	if (item.disabled) {
		style = theme_cache.option_disabled;
		font_color = theme_cache.font_disabled_color;
	} else if (item.selected && hovered == p_idx) {
		style = theme_cache.option_selected_hover;
		font_color = theme_cache.font_selected_color;
	} else if (item.selected) {
		style = theme_cache.option_selected;
		font_color = theme_cache.font_selected_color;
	} else if (hovered == p_idx) {
		style = theme_cache.option_hover;
		font_color = theme_cache.font_hover_color;
	} else if (!p_in_popup && focused_item == p_idx) {
		style = theme_cache.option_focus;
		font_color = theme_cache.font_focus_color;
	}

	if (item.separator) {
		const bool is_optgroup = !item.optgroup.is_empty();
		Ref<StyleBox> separator_style = is_optgroup ? theme_cache.optgroup : theme_cache.separator;
		Rect2 separator_rect = p_rect;
		if (!is_optgroup) {
			separator_rect.position.x += theme_cache.separator_margin;
			separator_rect.size.x = MAX(0, separator_rect.size.x - theme_cache.separator_margin * 2);
		}
		draw_style_box(separator_style, separator_rect);
		if (is_optgroup && !item.text.is_empty()) {
			Rect2 content = Rect2(p_rect.position + separator_style->get_offset(), p_rect.size - separator_style->get_minimum_size());
			content.position.x += theme_cache.optgroup_indent;
			content.size.x = MAX(0, content.size.x - theme_cache.optgroup_indent);
			theme_cache.font->draw_string(get_canvas_item(), Point2(content.position.x, content.position.y + (content.size.y - theme_cache.font->get_height(theme_cache.font_size)) * 0.5 + theme_cache.font->get_ascent(theme_cache.font_size)), item.text, HORIZONTAL_ALIGNMENT_LEFT, content.size.x, theme_cache.font_size, theme_cache.font_disabled_color);
		}
		return;
	}

	draw_style_box(style, p_rect);
	Rect2 content = Rect2(p_rect.position + style->get_offset(), p_rect.size - style->get_minimum_size());
	content.position.x += item.optgroup.is_empty() ? theme_cache.option_indent : theme_cache.optgroup_indent;

	if (appearance == APPEARANCE_BASE_SELECT && item.selected && theme_cache.checkmark_icon.is_valid()) {
		const Size2 check_size = theme_cache.checkmark_icon->get_size();
		draw_texture(theme_cache.checkmark_icon, Point2(content.position.x, content.position.y + (content.size.y - check_size.y) * 0.5), item.disabled ? theme_cache.font_disabled_color : theme_cache.font_selected_color);
		content.position.x += check_size.x + theme_cache.checkmark_margin;
		content.size.x -= check_size.x + theme_cache.checkmark_margin;
	}

	if (item.icon.is_valid()) {
		const Size2 icon_size = item.icon->get_size();
		draw_texture(item.icon, Point2(content.position.x, content.position.y + (content.size.y - icon_size.y) * 0.5), item.disabled ? Color(1, 1, 1, 0.45) : Color(1, 1, 1));
		content.position.x += icon_size.x + theme_cache.icon_separation;
		content.size.x -= icon_size.x + theme_cache.icon_separation;
	}

	theme_cache.font->draw_string(get_canvas_item(), Point2(content.position.x, content.position.y + (content.size.y - theme_cache.font->get_height(theme_cache.font_size)) * 0.5 + theme_cache.font->get_ascent(theme_cache.font_size)), item.text, HORIZONTAL_ALIGNMENT_LEFT, content.size.x, theme_cache.font_size, font_color);
}

void WebSelect::_draw_listbox() {
	draw_style_box(theme_cache.listbox, Rect2(Point2(), get_size()));
	if (has_focus()) {
		draw_style_box(theme_cache.focus, Rect2(Point2(), get_size()));
	}

	for (int i = scroll_offset; i < MIN(items.size(), scroll_offset + _get_visible_rows()); i++) {
		_draw_option(i, _get_option_rect(i));
	}
}

Size2 WebSelect::get_minimum_size() const {
	Size2 text_size;
	for (int i = 0; i < items.size(); i++) {
		if (items[i].separator) {
			continue;
		}
		text_size.width = MAX(text_size.width, theme_cache.font->get_string_size(items[i].text, HORIZONTAL_ALIGNMENT_LEFT, -1, theme_cache.font_size).width);
		text_size.height = MAX(text_size.height, theme_cache.font->get_height(theme_cache.font_size));
		if (items[i].icon.is_valid()) {
			text_size.width += items[i].icon->get_width() + theme_cache.icon_separation;
			text_size.height = MAX(text_size.height, items[i].icon->get_height());
		}
	}

	if (_is_listbox()) {
		const int option_height = _get_option_height();
		return Size2(text_size.width + theme_cache.listbox->get_minimum_size().width + theme_cache.option->get_minimum_size().width + theme_cache.checkmark_margin + 24, option_height * _get_visible_rows() + theme_cache.listbox->get_minimum_size().height);
	}

	Size2 minsize = text_size + theme_cache.normal->get_minimum_size();
	if (theme_cache.picker_icon.is_valid()) {
		minsize.width += theme_cache.picker_icon->get_width() + theme_cache.h_separation + theme_cache.picker_icon_margin;
		minsize.height = MAX(minsize.height, theme_cache.picker_icon->get_height() + theme_cache.normal->get_minimum_size().height);
	}
	return minsize.max(Size2(40, theme_cache.font->get_height(theme_cache.font_size) + theme_cache.normal->get_minimum_size().height));
}

void WebSelect::gui_input(const Ref<InputEvent> &p_event) {
	if (disabled) {
		return;
	}

	Ref<InputEventMouseMotion> mm = p_event;
	if (mm.is_valid() && _is_listbox()) {
		const int idx = scroll_offset + int((mm->get_position().y - _get_list_content_rect().position.y) / MAX(1, _get_option_height()));
		hovered = _is_item_selectable(idx) ? idx : -1;
		queue_redraw();
		return;
	}

	Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_valid() && mb->is_pressed() && mb->get_button_index() == MouseButton::LEFT) {
		grab_focus();
		if (_is_listbox()) {
			const int idx = scroll_offset + int((mb->get_position().y - _get_list_content_rect().position.y) / MAX(1, _get_option_height()));
			if (_is_item_selectable(idx)) {
				focused_item = idx;
				if (multiple) {
					_select(idx, !items[idx].selected, true);
				} else {
					_select_single(idx, true);
				}
			}
		} else {
			if (popup->is_visible()) {
				hide_picker();
			} else {
				show_picker();
			}
		}
		accept_event();
		return;
	}

	Ref<InputEventKey> key = p_event;
	if (key.is_valid() && key->is_pressed() && !key->is_echo()) {
		switch (key->get_keycode()) {
			case Key::SPACE:
			case Key::ENTER: {
				if (_is_listbox() || popup->is_visible()) {
					_activate_focused();
				} else {
					show_picker();
				}
				accept_event();
			} break;
			case Key::ESCAPE: {
				hide_picker();
				accept_event();
			} break;
			case Key::UP:
			case Key::DOWN: {
				const int dir = key->get_keycode() == Key::DOWN ? 1 : -1;
				const int base = focused_item >= 0 ? focused_item : current;
				const int next = _get_next_selectable_item(base, dir);
				if (next >= 0) {
					focused_item = next;
					if (!_is_listbox() && !popup->is_visible()) {
						_select_single(next, true);
					}
					_ensure_focused_visible();
					queue_redraw();
				}
				accept_event();
			} break;
			case Key::HOME:
			case Key::END: {
				const int next = key->get_keycode() == Key::HOME ? _get_first_selectable_item() : _get_next_selectable_item(0, -1);
				if (next >= 0) {
					focused_item = next;
					_ensure_focused_visible();
					queue_redraw();
				}
				accept_event();
			} break;
			default: {
				const String text = key->get_unicode() > 0 ? String::chr(key->get_unicode()) : String();
				if (!text.is_empty()) {
					_search_item(text);
				}
			}
		}
	}
}

void WebSelect::shortcut_input(const Ref<InputEvent> &p_event) {
	if (popup && popup->is_visible() && popup->activate_item_by_event(p_event, false)) {
		accept_event();
		return;
	}
	Control::shortcut_input(p_event);
}

void WebSelect::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ACCESSIBILITY_UPDATE: {
			RID ae = get_accessibility_element();
			ERR_FAIL_COND(ae.is_null());
			AccessibilityServer::get_singleton()->update_set_role(ae, _is_listbox() ? AccessibilityServerEnums::AccessibilityRole::ROLE_LIST : AccessibilityServerEnums::AccessibilityRole::ROLE_BUTTON);
			AccessibilityServer::get_singleton()->update_set_value(ae, get_value());
			if (!_is_listbox()) {
				AccessibilityServer::get_singleton()->update_set_popup_type(ae, AccessibilityServerEnums::AccessibilityPopupType::POPUP_LIST);
			}
		} break;
		case NOTIFICATION_MOUSE_ENTER:
		case NOTIFICATION_MOUSE_EXIT: {
			if (!_is_listbox()) {
				queue_redraw();
			}
		} break;
		case NOTIFICATION_DRAW: {
			if (_is_listbox()) {
				_draw_listbox();
			} else {
				_draw_closed_select();
			}
		} break;
		case NOTIFICATION_THEME_CHANGED: {
			_queue_redraw_and_minimum_size_update();
		} break;
		case NOTIFICATION_VISIBILITY_CHANGED: {
			if (!is_visible_in_tree()) {
				hide_picker();
			}
		} break;
	}
}

bool WebSelect::_set(const StringName &p_name, const Variant &p_value) {
	int index;
	const String sname = p_name;
	if (property_helper.is_property_valid(sname, &index)) {
		return property_helper.property_set_value(sname, p_value);
	}
	return false;
}

void WebSelect::_bind_methods() {
	ClassDB::bind_method(D_METHOD("add_item", "text", "value", "id"), &WebSelect::add_item, DEFVAL(String()), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("add_icon_item", "icon", "text", "value", "id"), &WebSelect::add_icon_item, DEFVAL(String()), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("add_separator", "text"), &WebSelect::add_separator, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("add_optgroup", "label"), &WebSelect::add_optgroup);
	ClassDB::bind_method(D_METHOD("clear"), &WebSelect::clear);
	ClassDB::bind_method(D_METHOD("remove_item", "idx"), &WebSelect::remove_item);
	ClassDB::bind_method(D_METHOD("set_item_count", "count"), &WebSelect::set_item_count);
	ClassDB::bind_method(D_METHOD("get_item_count"), &WebSelect::get_item_count);

	ClassDB::bind_method(D_METHOD("set_item_text", "idx", "text"), &WebSelect::set_item_text);
	ClassDB::bind_method(D_METHOD("get_item_text", "idx"), &WebSelect::get_item_text);
	ClassDB::bind_method(D_METHOD("set_item_value", "idx", "value"), &WebSelect::set_item_value);
	ClassDB::bind_method(D_METHOD("get_item_value", "idx"), &WebSelect::get_item_value);
	ClassDB::bind_method(D_METHOD("set_item_icon", "idx", "icon"), &WebSelect::set_item_icon);
	ClassDB::bind_method(D_METHOD("get_item_icon", "idx"), &WebSelect::get_item_icon);
	ClassDB::bind_method(D_METHOD("set_item_id", "idx", "id"), &WebSelect::set_item_id);
	ClassDB::bind_method(D_METHOD("get_item_id", "idx"), &WebSelect::get_item_id);
	ClassDB::bind_method(D_METHOD("get_item_index", "id"), &WebSelect::get_item_index);
	ClassDB::bind_method(D_METHOD("set_item_metadata", "idx", "metadata"), &WebSelect::set_item_metadata);
	ClassDB::bind_method(D_METHOD("get_item_metadata", "idx"), &WebSelect::get_item_metadata);
	ClassDB::bind_method(D_METHOD("set_item_disabled", "idx", "disabled"), &WebSelect::set_item_disabled);
	ClassDB::bind_method(D_METHOD("is_item_disabled", "idx"), &WebSelect::is_item_disabled);
	ClassDB::bind_method(D_METHOD("set_item_separator", "idx", "separator"), &WebSelect::set_item_separator);
	ClassDB::bind_method(D_METHOD("set_item_tooltip", "idx", "tooltip"), &WebSelect::set_item_tooltip);
	ClassDB::bind_method(D_METHOD("get_item_tooltip", "idx"), &WebSelect::get_item_tooltip);
	ClassDB::bind_method(D_METHOD("set_item_optgroup", "idx", "optgroup"), &WebSelect::set_item_optgroup);
	ClassDB::bind_method(D_METHOD("get_item_optgroup", "idx"), &WebSelect::get_item_optgroup);
	ClassDB::bind_method(D_METHOD("is_item_separator", "idx"), &WebSelect::is_item_separator);

	ClassDB::bind_method(D_METHOD("select", "idx"), &WebSelect::select);
	ClassDB::bind_method(D_METHOD("deselect", "idx"), &WebSelect::deselect);
	ClassDB::bind_method(D_METHOD("deselect_all"), &WebSelect::deselect_all);
	ClassDB::bind_method(D_METHOD("set_item_selected", "idx", "selected"), &WebSelect::set_item_selected);
	ClassDB::bind_method(D_METHOD("is_item_selected", "idx"), &WebSelect::is_item_selected);
	ClassDB::bind_method(D_METHOD("get_selected"), &WebSelect::get_selected);
	ClassDB::bind_method(D_METHOD("get_selected_indices"), &WebSelect::get_selected_indices);
	ClassDB::bind_method(D_METHOD("get_value"), &WebSelect::get_value);
	ClassDB::bind_method(D_METHOD("get_values"), &WebSelect::get_values);
	ClassDB::bind_method(D_METHOD("has_selectable_items"), &WebSelect::has_selectable_items);

	ClassDB::bind_method(D_METHOD("set_multiple", "multiple"), &WebSelect::set_multiple);
	ClassDB::bind_method(D_METHOD("is_multiple"), &WebSelect::is_multiple);
	ClassDB::bind_method(D_METHOD("set_size_rows", "size_rows"), &WebSelect::set_size_rows);
	ClassDB::bind_method(D_METHOD("get_size_rows"), &WebSelect::get_size_rows);
	ClassDB::bind_method(D_METHOD("set_required", "required"), &WebSelect::set_required);
	ClassDB::bind_method(D_METHOD("is_required"), &WebSelect::is_required);
	ClassDB::bind_method(D_METHOD("is_valid"), &WebSelect::is_valid);
	ClassDB::bind_method(D_METHOD("set_disabled", "disabled"), &WebSelect::set_disabled);
	ClassDB::bind_method(D_METHOD("is_disabled"), &WebSelect::is_disabled);
	ClassDB::bind_method(D_METHOD("set_allow_reselect", "allow"), &WebSelect::set_allow_reselect);
	ClassDB::bind_method(D_METHOD("get_allow_reselect"), &WebSelect::get_allow_reselect);
	ClassDB::bind_method(D_METHOD("set_name_attribute", "name"), &WebSelect::set_name_attribute);
	ClassDB::bind_method(D_METHOD("get_name_attribute"), &WebSelect::get_name_attribute);
	ClassDB::bind_method(D_METHOD("set_appearance", "appearance"), &WebSelect::set_appearance);
	ClassDB::bind_method(D_METHOD("get_appearance"), &WebSelect::get_appearance);
	ClassDB::bind_method(D_METHOD("show_picker"), &WebSelect::show_picker);
	ClassDB::bind_method(D_METHOD("hide_picker"), &WebSelect::hide_picker);
	ClassDB::bind_method(D_METHOD("is_picker_visible"), &WebSelect::is_picker_visible);
	ClassDB::bind_method(D_METHOD("get_popup"), &WebSelect::get_popup);
	ClassDB::bind_method(D_METHOD("get_style_metrics"), &WebSelect::get_style_metrics);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "appearance", PROPERTY_HINT_ENUM, "Auto,Base Select"), "set_appearance", "get_appearance");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "multiple"), "set_multiple", "is_multiple");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "size_rows", PROPERTY_HINT_RANGE, "0,32,1,or_greater"), "set_size_rows", "get_size_rows");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "required"), "set_required", "is_required");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "disabled"), "set_disabled", "is_disabled");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "name_attribute"), "set_name_attribute", "get_name_attribute");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "allow_reselect"), "set_allow_reselect", "get_allow_reselect");

	ADD_ARRAY_COUNT("Items", "item_count", "set_item_count", "get_item_count", "item_");

	ADD_SIGNAL(MethodInfo("item_selected", PropertyInfo(Variant::INT, "index")));
	ADD_SIGNAL(MethodInfo("changed"));

	BIND_ENUM_CONSTANT(APPEARANCE_AUTO);
	BIND_ENUM_CONSTANT(APPEARANCE_BASE_SELECT);

	BIND_THEME_ITEM(Theme::DATA_TYPE_STYLEBOX, WebSelect, normal);
	BIND_THEME_ITEM(Theme::DATA_TYPE_STYLEBOX, WebSelect, hover);
	BIND_THEME_ITEM(Theme::DATA_TYPE_STYLEBOX, WebSelect, pressed);
	BIND_THEME_ITEM(Theme::DATA_TYPE_STYLEBOX, WebSelect, open);
	BIND_THEME_ITEM(Theme::DATA_TYPE_STYLEBOX, WebSelect, disabled);
	BIND_THEME_ITEM(Theme::DATA_TYPE_STYLEBOX, WebSelect, focus);
	BIND_THEME_ITEM(Theme::DATA_TYPE_STYLEBOX, WebSelect, picker);
	BIND_THEME_ITEM(Theme::DATA_TYPE_STYLEBOX, WebSelect, listbox);
	BIND_THEME_ITEM(Theme::DATA_TYPE_STYLEBOX, WebSelect, option);
	BIND_THEME_ITEM(Theme::DATA_TYPE_STYLEBOX, WebSelect, option_hover);
	BIND_THEME_ITEM(Theme::DATA_TYPE_STYLEBOX, WebSelect, option_focus);
	BIND_THEME_ITEM(Theme::DATA_TYPE_STYLEBOX, WebSelect, option_selected);
	BIND_THEME_ITEM(Theme::DATA_TYPE_STYLEBOX, WebSelect, option_selected_hover);
	BIND_THEME_ITEM(Theme::DATA_TYPE_STYLEBOX, WebSelect, option_disabled);
	BIND_THEME_ITEM(Theme::DATA_TYPE_STYLEBOX, WebSelect, optgroup);
	BIND_THEME_ITEM(Theme::DATA_TYPE_STYLEBOX, WebSelect, separator);

	BIND_THEME_ITEM(Theme::DATA_TYPE_FONT, WebSelect, font);
	BIND_THEME_ITEM(Theme::DATA_TYPE_FONT_SIZE, WebSelect, font_size);
	BIND_THEME_ITEM(Theme::DATA_TYPE_COLOR, WebSelect, font_color);
	BIND_THEME_ITEM(Theme::DATA_TYPE_COLOR, WebSelect, font_hover_color);
	BIND_THEME_ITEM(Theme::DATA_TYPE_COLOR, WebSelect, font_focus_color);
	BIND_THEME_ITEM(Theme::DATA_TYPE_COLOR, WebSelect, font_disabled_color);
	BIND_THEME_ITEM(Theme::DATA_TYPE_COLOR, WebSelect, font_selected_color);
	BIND_THEME_ITEM(Theme::DATA_TYPE_COLOR, WebSelect, font_placeholder_color);

	BIND_THEME_ITEM(Theme::DATA_TYPE_ICON, WebSelect, picker_icon);
	BIND_THEME_ITEM(Theme::DATA_TYPE_ICON, WebSelect, checkmark_icon);

	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebSelect, h_separation);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebSelect, icon_separation);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebSelect, picker_icon_margin);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebSelect, checkmark_margin);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebSelect, option_min_height);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebSelect, optgroup_indent);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebSelect, option_indent);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebSelect, separator_margin);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebSelect, picker_max_height);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebSelect, picker_offset);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebSelect, outline_size);

	Item defaults;
	base_property_helper.set_prefix("item_");
	base_property_helper.set_array_length_getter(&WebSelect::get_item_count);
	base_property_helper.register_property(PropertyInfo(Variant::STRING, "text"), defaults.text, &WebSelect::set_item_text, &WebSelect::get_item_text);
	base_property_helper.register_property(PropertyInfo(Variant::STRING, "value"), defaults.value, &WebSelect::set_item_value, &WebSelect::get_item_value);
	base_property_helper.register_property(PropertyInfo(Variant::OBJECT, "icon", PROPERTY_HINT_RESOURCE_TYPE, Texture2D::get_class_static()), defaults.icon, &WebSelect::set_item_icon, &WebSelect::get_item_icon);
	base_property_helper.register_property(PropertyInfo(Variant::INT, "id", PROPERTY_HINT_RANGE, "-1,4096,1,or_greater"), defaults.id, &WebSelect::set_item_id, &WebSelect::get_item_id);
	base_property_helper.register_property(PropertyInfo(Variant::BOOL, "disabled"), defaults.disabled, &WebSelect::set_item_disabled, &WebSelect::is_item_disabled);
	base_property_helper.register_property(PropertyInfo(Variant::BOOL, "selected"), defaults.selected, &WebSelect::set_item_selected, &WebSelect::is_item_selected);
	base_property_helper.register_property(PropertyInfo(Variant::STRING, "optgroup"), defaults.optgroup, &WebSelect::set_item_optgroup, &WebSelect::get_item_optgroup);
	base_property_helper.register_property(PropertyInfo(Variant::BOOL, "separator"), defaults.separator, &WebSelect::set_item_separator, &WebSelect::is_item_separator);
	PropertyListHelper::register_base_helper(WebSelect::get_class_static(), &base_property_helper);

	ADD_CLASS_DEPENDENCY("PopupMenu");
}

void WebSelect::add_item(const String &p_text, const String &p_value, int p_id) {
	Item item;
	item.text = p_text;
	item.value = p_value.is_empty() ? p_text : p_value;
	item.value_explicit = !p_value.is_empty();
	item.id = p_id;
	items.push_back(item);
	if (current < 0) {
		_select_single(items.size() - 1, false);
	}
	_queue_redraw_and_minimum_size_update();
	notify_property_list_changed();
}

void WebSelect::add_icon_item(const Ref<Texture2D> &p_icon, const String &p_text, const String &p_value, int p_id) {
	add_item(p_text, p_value, p_id);
	items.write[items.size() - 1].icon = p_icon;
}

void WebSelect::add_separator(const String &p_text) {
	Item item;
	item.text = p_text;
	item.separator = true;
	item.disabled = true;
	items.push_back(item);
	_queue_redraw_and_minimum_size_update();
	notify_property_list_changed();
}

void WebSelect::add_optgroup(const String &p_label) {
	add_separator(p_label);
	items.write[items.size() - 1].optgroup = p_label;
}

void WebSelect::clear() {
	items.clear();
	current = -1;
	focused_item = -1;
	hovered = -1;
	scroll_offset = 0;
	_queue_redraw_and_minimum_size_update();
	notify_property_list_changed();
}

void WebSelect::remove_item(int p_idx) {
	ERR_FAIL_INDEX(p_idx, items.size());
	items.remove_at(p_idx);
	current = -1;
	for (int i = 0; i < items.size(); i++) {
		if (items[i].selected) {
			current = i;
			break;
		}
	}
	if (current < 0 && !multiple) {
		const int first = _get_first_selectable_item();
		if (first >= 0) {
			_select_single(first, false);
		}
	}
	_queue_redraw_and_minimum_size_update();
	notify_property_list_changed();
}

void WebSelect::set_item_count(int p_count) {
	ERR_FAIL_COND(p_count < 0);
	const int old_size = items.size();
	items.resize(p_count);
	for (int i = old_size; i < p_count; i++) {
		items.write[i].id = -1;
	}
	if (current >= p_count) {
		current = -1;
	}
	if (current < 0 && p_count > 0 && !multiple) {
		const int first = _get_first_selectable_item();
		if (first >= 0) {
			_select_single(first, false);
		}
	}
	_queue_redraw_and_minimum_size_update();
	notify_property_list_changed();
}

int WebSelect::get_item_count() const {
	return items.size();
}

void WebSelect::set_item_text(int p_idx, const String &p_text) {
	ERR_FAIL_INDEX(p_idx, items.size());
	items.write[p_idx].text = p_text;
	if (!items[p_idx].value_explicit) {
		items.write[p_idx].value = p_text;
	}
	_queue_redraw_and_minimum_size_update();
}

String WebSelect::get_item_text(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, items.size(), String());
	return items[p_idx].text;
}

void WebSelect::set_item_value(int p_idx, const String &p_value) {
	ERR_FAIL_INDEX(p_idx, items.size());
	items.write[p_idx].value = p_value;
	items.write[p_idx].value_explicit = true;
}

String WebSelect::get_item_value(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, items.size(), String());
	return items[p_idx].value_explicit ? items[p_idx].value : items[p_idx].text;
}

void WebSelect::set_item_icon(int p_idx, const Ref<Texture2D> &p_icon) {
	ERR_FAIL_INDEX(p_idx, items.size());
	items.write[p_idx].icon = p_icon;
	_queue_redraw_and_minimum_size_update();
}

Ref<Texture2D> WebSelect::get_item_icon(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, items.size(), Ref<Texture2D>());
	return items[p_idx].icon;
}

void WebSelect::set_item_id(int p_idx, int p_id) {
	ERR_FAIL_INDEX(p_idx, items.size());
	items.write[p_idx].id = p_id;
}

int WebSelect::get_item_id(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, items.size(), -1);
	return items[p_idx].id;
}

int WebSelect::get_item_index(int p_id) const {
	for (int i = 0; i < items.size(); i++) {
		if (items[i].id == p_id) {
			return i;
		}
	}
	return -1;
}

void WebSelect::set_item_metadata(int p_idx, const Variant &p_metadata) {
	ERR_FAIL_INDEX(p_idx, items.size());
	items.write[p_idx].metadata = p_metadata;
}

Variant WebSelect::get_item_metadata(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, items.size(), Variant());
	return items[p_idx].metadata;
}

void WebSelect::set_item_disabled(int p_idx, bool p_disabled) {
	ERR_FAIL_INDEX(p_idx, items.size());
	items.write[p_idx].disabled = p_disabled;
	if (p_disabled && !multiple && current == p_idx) {
		const int first = _get_first_selectable_item();
		current = -1;
		if (first >= 0) {
			_select_single(first, false);
		}
	}
	_queue_redraw_and_minimum_size_update();
}

bool WebSelect::is_item_disabled(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, items.size(), false);
	return items[p_idx].disabled;
}

void WebSelect::set_item_separator(int p_idx, bool p_separator) {
	ERR_FAIL_INDEX(p_idx, items.size());
	items.write[p_idx].separator = p_separator;
	if (p_separator) {
		items.write[p_idx].disabled = true;
		items.write[p_idx].selected = false;
		if (current == p_idx) {
			current = -1;
			const int first = _get_first_selectable_item();
			if (first >= 0 && !multiple) {
				_select_single(first, false);
			}
		}
	}
	_queue_redraw_and_minimum_size_update();
}

void WebSelect::set_item_tooltip(int p_idx, const String &p_tooltip) {
	ERR_FAIL_INDEX(p_idx, items.size());
	items.write[p_idx].tooltip = p_tooltip;
}

String WebSelect::get_item_tooltip(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, items.size(), String());
	return items[p_idx].tooltip;
}

void WebSelect::set_item_optgroup(int p_idx, const String &p_optgroup) {
	ERR_FAIL_INDEX(p_idx, items.size());
	items.write[p_idx].optgroup = p_optgroup;
	_queue_redraw_and_minimum_size_update();
}

String WebSelect::get_item_optgroup(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, items.size(), String());
	return items[p_idx].optgroup;
}

bool WebSelect::is_item_separator(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, items.size(), false);
	return items[p_idx].separator;
}

void WebSelect::select(int p_idx) {
	_select(p_idx, true, false);
}

void WebSelect::deselect(int p_idx) {
	if (!multiple) {
		return;
	}
	_select(p_idx, false, false);
}

void WebSelect::deselect_all() {
	for (int i = 0; i < items.size(); i++) {
		items.write[i].selected = false;
	}
	current = -1;
	_queue_redraw_and_minimum_size_update();
}

void WebSelect::set_item_selected(int p_idx, bool p_selected) {
	_select(p_idx, p_selected, false);
}

bool WebSelect::is_item_selected(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, items.size(), false);
	return items[p_idx].selected;
}

int WebSelect::get_selected() const {
	return current;
}

PackedInt32Array WebSelect::get_selected_indices() const {
	PackedInt32Array selected;
	for (int i = 0; i < items.size(); i++) {
		if (items[i].selected) {
			selected.push_back(i);
		}
	}
	return selected;
}

String WebSelect::get_value() const {
	if (multiple) {
		for (int i = 0; i < items.size(); i++) {
			if (items[i].selected) {
				return get_item_value(i);
			}
		}
		return String();
	}
	if (current >= 0 && current < items.size()) {
		return get_item_value(current);
	}
	return String();
}

PackedStringArray WebSelect::get_values() const {
	PackedStringArray values;
	for (int i = 0; i < items.size(); i++) {
		if (items[i].selected) {
			values.push_back(get_item_value(i));
		}
	}
	return values;
}

bool WebSelect::has_selectable_items() const {
	return _get_first_selectable_item() >= 0;
}

void WebSelect::set_multiple(bool p_multiple) {
	if (multiple == p_multiple) {
		return;
	}
	multiple = p_multiple;
	if (!multiple && get_selected_indices().size() > 1) {
		_select_single(current >= 0 ? current : _get_first_selectable_item(), false);
	}
	hide_picker();
	queue_accessibility_update();
	_queue_redraw_and_minimum_size_update();
}

bool WebSelect::is_multiple() const {
	return multiple;
}

void WebSelect::set_size_rows(int p_size) {
	ERR_FAIL_COND(p_size < 0);
	size_rows = p_size;
	hide_picker();
	_queue_redraw_and_minimum_size_update();
}

int WebSelect::get_size_rows() const {
	return size_rows;
}

void WebSelect::set_required(bool p_required) {
	required = p_required;
	queue_accessibility_update();
}

bool WebSelect::is_required() const {
	return required;
}

bool WebSelect::is_valid() const {
	return !required || !get_value().is_empty();
}

void WebSelect::set_disabled(bool p_disabled) {
	disabled = p_disabled;
	if (disabled) {
		hide_picker();
	}
	queue_accessibility_update();
	queue_redraw();
}

bool WebSelect::is_disabled() const {
	return disabled;
}

void WebSelect::set_allow_reselect(bool p_allow) {
	allow_reselect = p_allow;
}

bool WebSelect::get_allow_reselect() const {
	return allow_reselect;
}

void WebSelect::set_name_attribute(const String &p_name) {
	name = p_name;
}

String WebSelect::get_name_attribute() const {
	return name;
}

void WebSelect::set_appearance(Appearance p_appearance) {
	appearance = p_appearance;
	queue_redraw();
}

WebSelect::Appearance WebSelect::get_appearance() const {
	return appearance;
}

void WebSelect::show_picker() {
	if (disabled || _is_listbox()) {
		return;
	}
	_sync_popup();
	_apply_popup_theme();
	const Rect2i rect = Rect2i(get_screen_position() + Point2(0, get_size().y + theme_cache.picker_offset), Size2i(get_size().x, 0));
	popup->set_size(Size2i(get_size().x, 0));
	popup->popup(rect);
	queue_redraw();
}

void WebSelect::hide_picker() {
	if (popup) {
		popup->hide();
	}
	queue_redraw();
}

bool WebSelect::is_picker_visible() const {
	return popup && popup->is_visible();
}

PopupMenu *WebSelect::get_popup() const {
	return popup;
}

Dictionary WebSelect::get_style_metrics() const {
	Dictionary metrics;
	metrics["rect"] = Rect2(Point2(), get_size());
	metrics["minimum_size"] = get_minimum_size();
	metrics["font_size"] = theme_cache.font_size;
	metrics["font_color"] = theme_cache.font_color;
	metrics["font_disabled_color"] = theme_cache.font_disabled_color;
	metrics["appearance"] = appearance;
	metrics["multiple"] = multiple;
	metrics["size_rows"] = size_rows;
	metrics["hovered"] = hovered;
	metrics["focused_item"] = focused_item;
	metrics["picker_visible"] = is_picker_visible();
	metrics["picker_rect"] = popup ? Rect2(popup->get_position(), popup->get_size()) : Rect2();
	metrics["picker_icon_size"] = theme_cache.picker_icon.is_valid() ? theme_cache.picker_icon->get_size() : Size2();
	metrics["checkmark_icon_size"] = theme_cache.checkmark_icon.is_valid() ? theme_cache.checkmark_icon->get_size() : Size2();

	Dictionary styleboxes;
	styleboxes["normal"] = _stylebox_metrics(theme_cache.normal);
	styleboxes["hover"] = _stylebox_metrics(theme_cache.hover);
	styleboxes["pressed"] = _stylebox_metrics(theme_cache.pressed);
	styleboxes["open"] = _stylebox_metrics(theme_cache.open);
	styleboxes["disabled"] = _stylebox_metrics(theme_cache.disabled);
	styleboxes["focus"] = _stylebox_metrics(theme_cache.focus);
	styleboxes["picker"] = _stylebox_metrics(theme_cache.picker);
	styleboxes["listbox"] = _stylebox_metrics(theme_cache.listbox);
	styleboxes["option"] = _stylebox_metrics(theme_cache.option);
	styleboxes["option_hover"] = _stylebox_metrics(theme_cache.option_hover);
	styleboxes["option_focus"] = _stylebox_metrics(theme_cache.option_focus);
	styleboxes["option_selected"] = _stylebox_metrics(theme_cache.option_selected);
	styleboxes["option_selected_hover"] = _stylebox_metrics(theme_cache.option_selected_hover);
	styleboxes["option_disabled"] = _stylebox_metrics(theme_cache.option_disabled);
	styleboxes["optgroup"] = _stylebox_metrics(theme_cache.optgroup);
	styleboxes["separator"] = _stylebox_metrics(theme_cache.separator);
	metrics["styleboxes"] = styleboxes;

	Dictionary theme;
	theme["font_size"] = theme_cache.font_size;
	theme["font_color"] = theme_cache.font_color;
	theme["font_hover_color"] = theme_cache.font_hover_color;
	theme["font_focus_color"] = theme_cache.font_focus_color;
	theme["font_disabled_color"] = theme_cache.font_disabled_color;
	theme["font_selected_color"] = theme_cache.font_selected_color;
	theme["font_placeholder_color"] = theme_cache.font_placeholder_color;
	theme["h_separation"] = theme_cache.h_separation;
	theme["icon_separation"] = theme_cache.icon_separation;
	theme["picker_icon_margin"] = theme_cache.picker_icon_margin;
	theme["checkmark_margin"] = theme_cache.checkmark_margin;
	theme["option_min_height"] = theme_cache.option_min_height;
	theme["optgroup_indent"] = theme_cache.optgroup_indent;
	theme["option_indent"] = theme_cache.option_indent;
	theme["separator_margin"] = theme_cache.separator_margin;
	theme["picker_max_height"] = theme_cache.picker_max_height;
	theme["picker_offset"] = theme_cache.picker_offset;
	theme["outline_size"] = theme_cache.outline_size;
	metrics["theme"] = theme;
	metrics["selected"] = get_selected_indices();
	metrics["value"] = get_value();
	Array option_rects;
	for (int i = 0; i < items.size(); i++) {
		Dictionary item;
		item["index"] = i;
		item["text"] = items[i].text;
		item["value"] = get_item_value(i);
		item["selected"] = items[i].selected;
		item["disabled"] = items[i].disabled;
		item["separator"] = items[i].separator;
		item["optgroup"] = items[i].optgroup;
		item["role"] = items[i].separator ? (items[i].optgroup.is_empty() ? "separator" : "optgroup") : "option";
		item["has_icon"] = items[i].icon.is_valid();
		item["icon_size"] = items[i].icon.is_valid() ? items[i].icon->get_size() : Size2();
		item["checkmark_visible"] = appearance == APPEARANCE_BASE_SELECT && items[i].selected && theme_cache.checkmark_icon.is_valid();
		item["rect"] = _is_listbox() ? _get_option_rect(i) : Rect2();
		option_rects.push_back(item);
	}
	metrics["options"] = option_rects;
	return metrics;
}

WebSelect::WebSelect() {
	set_focus_mode(FOCUS_ALL);
	set_mouse_filter(MOUSE_FILTER_STOP);
	popup = memnew(PopupMenu);
	popup->set_hide_on_item_selection(true);
	add_child(popup, false, INTERNAL_MODE_FRONT);
	popup->connect("index_pressed", callable_mp(this, &WebSelect::_popup_index_pressed));
	property_helper.setup_for_instance(base_property_helper, this);
}

WebSelect::~WebSelect() {
}
