/**************************************************************************/
/*  web_select.h                                                          */
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

#pragma once

#include "scene/gui/control.h"
#include "scene/gui/popup_menu.h"
#include "scene/property_list_helper.h"

class WebSelect : public Control {
	GDCLASS(WebSelect, Control);

public:
	enum Appearance {
		APPEARANCE_AUTO,
		APPEARANCE_BASE_SELECT,
	};

private:
	struct Item {
		String text;
		String value;
		Ref<Texture2D> icon;
		int id = -1;
		Variant metadata;
		String tooltip;
		String optgroup;
		bool disabled = false;
		bool separator = false;
		bool selected = false;
		bool value_explicit = false;
	};

	struct ThemeCache {
		Ref<StyleBox> normal;
		Ref<StyleBox> hover;
		Ref<StyleBox> pressed;
		Ref<StyleBox> open;
		Ref<StyleBox> disabled;
		Ref<StyleBox> focus;
		Ref<StyleBox> picker;
		Ref<StyleBox> listbox;
		Ref<StyleBox> option;
		Ref<StyleBox> option_hover;
		Ref<StyleBox> option_focus;
		Ref<StyleBox> option_selected;
		Ref<StyleBox> option_selected_hover;
		Ref<StyleBox> option_disabled;
		Ref<StyleBox> optgroup;
		Ref<StyleBox> separator;

		Ref<Font> font;
		int font_size = 0;
		Color font_color;
		Color font_hover_color;
		Color font_focus_color;
		Color font_disabled_color;
		Color font_selected_color;
		Color font_placeholder_color;

		Ref<Texture2D> picker_icon;
		Ref<Texture2D> checkmark_icon;

		int h_separation = 0;
		int icon_separation = 0;
		int picker_icon_margin = 0;
		int checkmark_margin = 0;
		int option_min_height = 0;
		int optgroup_indent = 0;
		int option_indent = 0;
		int separator_margin = 0;
		int picker_max_height = 0;
		int picker_offset = 0;
		int outline_size = 0;
	} theme_cache;

	static inline PropertyListHelper base_property_helper;
	PropertyListHelper property_helper;

	Vector<Item> items;
	PopupMenu *popup = nullptr;
	Appearance appearance = APPEARANCE_BASE_SELECT;
	bool multiple = false;
	bool required = false;
	bool disabled = false;
	bool allow_reselect = false;
	int size_rows = 0;
	int current = -1;
	int hovered = -1;
	int focused_item = -1;
	int scroll_offset = 0;
	String name;
	String search_string;
	uint64_t search_time_msec = 0;

	bool _is_listbox() const;
	bool _is_item_selectable(int p_idx) const;
	int _get_first_selectable_item() const;
	int _get_next_selectable_item(int p_from, int p_dir) const;
	int _get_visible_rows() const;
	int _get_option_height() const;
	Rect2 _get_list_content_rect() const;
	Rect2 _get_option_rect(int p_idx) const;
	Ref<StyleBox> _get_button_stylebox() const;
	Dictionary _stylebox_metrics(const Ref<StyleBox> &p_style) const;
	void _queue_redraw_and_minimum_size_update();
	void _sync_popup();
	void _apply_popup_theme();
	void _popup_index_pressed(int p_index);
	void _select(int p_idx, bool p_selected, bool p_emit);
	void _select_single(int p_idx, bool p_emit);
	void _ensure_focused_visible();
	void _activate_focused();
	void _search_item(const String &p_text);
	void _draw_closed_select();
	void _draw_listbox();
	void _draw_option(int p_idx, const Rect2 &p_rect, bool p_in_popup = false);
	void _dummy_setter() {}

protected:
	Size2 get_minimum_size() const override;
	void gui_input(const Ref<InputEvent> &p_event) override;
	void shortcut_input(const Ref<InputEvent> &p_event) override;
	void _notification(int p_what);
	bool _set(const StringName &p_name, const Variant &p_value);
	bool _get(const StringName &p_name, Variant &r_ret) const { return property_helper.property_get_value(p_name, r_ret); }
	void _get_property_list(List<PropertyInfo> *p_list) const { property_helper.get_property_list(p_list); }
	bool _property_can_revert(const StringName &p_name) const { return property_helper.property_can_revert(p_name); }
	bool _property_get_revert(const StringName &p_name, Variant &r_property) const { return property_helper.property_get_revert(p_name, r_property); }
	static void _bind_methods();

public:
	static const int ITEM_PROPERTY_SIZE = 8;

	void add_item(const String &p_text, const String &p_value = String(), int p_id = -1);
	void add_icon_item(const Ref<Texture2D> &p_icon, const String &p_text, const String &p_value = String(), int p_id = -1);
	void add_separator(const String &p_text = String());
	void add_optgroup(const String &p_label);
	void clear();
	void remove_item(int p_idx);
	void set_item_count(int p_count);
	int get_item_count() const;

	void set_item_text(int p_idx, const String &p_text);
	String get_item_text(int p_idx) const;
	void set_item_value(int p_idx, const String &p_value);
	String get_item_value(int p_idx) const;
	void set_item_icon(int p_idx, const Ref<Texture2D> &p_icon);
	Ref<Texture2D> get_item_icon(int p_idx) const;
	void set_item_id(int p_idx, int p_id);
	int get_item_id(int p_idx) const;
	int get_item_index(int p_id) const;
	void set_item_metadata(int p_idx, const Variant &p_metadata);
	Variant get_item_metadata(int p_idx) const;
	void set_item_disabled(int p_idx, bool p_disabled);
	bool is_item_disabled(int p_idx) const;
	void set_item_separator(int p_idx, bool p_separator);
	void set_item_tooltip(int p_idx, const String &p_tooltip);
	String get_item_tooltip(int p_idx) const;
	void set_item_optgroup(int p_idx, const String &p_optgroup);
	String get_item_optgroup(int p_idx) const;
	bool is_item_separator(int p_idx) const;

	void select(int p_idx);
	void deselect(int p_idx);
	void deselect_all();
	void set_item_selected(int p_idx, bool p_selected);
	bool is_item_selected(int p_idx) const;
	int get_selected() const;
	PackedInt32Array get_selected_indices() const;
	String get_value() const;
	PackedStringArray get_values() const;
	bool has_selectable_items() const;

	void set_multiple(bool p_multiple);
	bool is_multiple() const;
	void set_size_rows(int p_size);
	int get_size_rows() const;
	void set_required(bool p_required);
	bool is_required() const;
	bool is_valid() const;
	void set_disabled(bool p_disabled);
	bool is_disabled() const;
	void set_allow_reselect(bool p_allow);
	bool get_allow_reselect() const;
	void set_name_attribute(const String &p_name);
	String get_name_attribute() const;
	void set_appearance(Appearance p_appearance);
	Appearance get_appearance() const;

	void show_picker();
	void hide_picker();
	bool is_picker_visible() const;
	PopupMenu *get_popup() const;
	Dictionary get_style_metrics() const;

	WebSelect();
	~WebSelect();
};

VARIANT_ENUM_CAST(WebSelect::Appearance);
