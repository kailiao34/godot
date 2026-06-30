/**************************************************************************/
/*  web_list.h                                                            */
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
#include "scene/property_list_helper.h"

class InputEventMouseMotion;

class WebList : public Control {
	GDCLASS(WebList, Control);

public:
	enum ListTag {
		LIST_TAG_UNORDERED,
		LIST_TAG_ORDERED,
	};

	enum OrderedType {
		ORDERED_TYPE_DECIMAL,
		ORDERED_TYPE_DECIMAL_LEADING_ZERO,
		ORDERED_TYPE_LOWER_ALPHA,
		ORDERED_TYPE_UPPER_ALPHA,
		ORDERED_TYPE_LOWER_ROMAN,
		ORDERED_TYPE_UPPER_ROMAN,
	};

	enum ListStyleType {
		LIST_STYLE_TYPE_DISC,
		LIST_STYLE_TYPE_CIRCLE,
		LIST_STYLE_TYPE_SQUARE,
		LIST_STYLE_TYPE_DECIMAL,
		LIST_STYLE_TYPE_DECIMAL_LEADING_ZERO,
		LIST_STYLE_TYPE_LOWER_ALPHA,
		LIST_STYLE_TYPE_UPPER_ALPHA,
		LIST_STYLE_TYPE_LOWER_ROMAN,
		LIST_STYLE_TYPE_UPPER_ROMAN,
		LIST_STYLE_TYPE_NONE,
	};

	enum ListStylePosition {
		LIST_STYLE_POSITION_OUTSIDE,
		LIST_STYLE_POSITION_INSIDE,
	};

	enum BoxSizing {
		BOX_SIZING_CONTENT_BOX,
		BOX_SIZING_BORDER_BOX,
	};

private:
	struct Item {
		String text;
		int value = 0;
		bool value_enabled = false;
		String custom_marker_text;
		Variant metadata;
		String tooltip;
		AutoTranslateMode auto_translate_mode = AUTO_TRANSLATE_MODE_INHERIT;
	};

	Vector<Item> items;

	ListTag list_tag = LIST_TAG_UNORDERED;
	OrderedType ordered_type = ORDERED_TYPE_DECIMAL;
	ListStyleType list_style_type = LIST_STYLE_TYPE_DISC;
	ListStylePosition list_style_position = LIST_STYLE_POSITION_OUTSIDE;
	BoxSizing box_sizing = BOX_SIZING_CONTENT_BOX;
	int start = 1;
	bool reversed = false;
	float opacity = 1.0;
	HorizontalAlignment text_alignment = HORIZONTAL_ALIGNMENT_LEFT;
	TextDirection text_direction = TEXT_DIRECTION_AUTO;
	String language;
	int hovered_item = -1;

	struct ThemeCache {
		Ref<StyleBox> normal_style;
		Ref<StyleBox> hover_style;
		Ref<StyleBox> focus_style;
		Ref<StyleBox> item_normal_style;
		Ref<StyleBox> item_hover_style;
		Ref<StyleBox> marker_normal_style;
		Ref<StyleBox> marker_hover_style;

		Ref<Font> font;
		Ref<Font> marker_font;
		int font_size = 0;
		int marker_font_size = 0;

		Color font_color;
		Color font_hover_color;
		Color marker_color;
		Color marker_hover_color;
		Color font_outline_color;
		int outline_size = 0;

		int padding_inline_start = 0;
		int padding_inline_end = 0;
		int padding_block_start = 0;
		int padding_block_end = 0;
		int margin_block_start = 0;
		int margin_block_end = 0;
		int marker_gap = 0;
		int marker_min_width = 0;
		int item_spacing = 0;
		int line_height = 0;
		int letter_spacing = 0;
		int font_weight = 400;
		int marker_font_weight = 400;
		int text_align = 0;
	} theme_cache;

	static inline PropertyListHelper base_property_helper;
	PropertyListHelper property_helper;

	void _dummy_setter() {}
	String _get_item_marker_text(int p_idx) const;
	String _get_ordered_marker_text(int p_number, OrderedType p_type) const;
	String _int_to_alpha(int p_number, bool p_uppercase) const;
	String _int_to_roman(int p_number, bool p_uppercase) const;
	int _get_item_number(int p_idx) const;
	int _get_line_height() const;
	Size2 _get_text_size(const String &p_text, bool p_marker = false) const;
	Rect2 _get_item_rect(int p_idx) const;
	void _update_hovered_item(const Point2 &p_position);
	String _get_translated_item_text(int p_idx) const;

protected:
	virtual void gui_input(const Ref<InputEvent> &p_event) override;
	void _notification(int p_what);
	bool _set(const StringName &p_name, const Variant &p_value);
	bool _get(const StringName &p_name, Variant &r_ret) const { return property_helper.property_get_value(p_name, r_ret); }
	void _get_property_list(List<PropertyInfo> *p_list) const { property_helper.get_property_list(p_list); }
	bool _property_can_revert(const StringName &p_name) const { return property_helper.property_can_revert(p_name); }
	bool _property_get_revert(const StringName &p_name, Variant &r_property) const { return property_helper.property_get_revert(p_name, r_property); }
	static void _bind_methods();

public:
	virtual Size2 get_minimum_size() const override;
	virtual String get_tooltip(const Point2 &p_pos) const override;

	void set_list_tag(ListTag p_tag);
	ListTag get_list_tag() const;

	void set_ordered_type(OrderedType p_type);
	OrderedType get_ordered_type() const;

	void set_start(int p_start);
	int get_start() const;

	void set_reversed(bool p_reversed);
	bool is_reversed() const;

	void set_list_style_type(ListStyleType p_type);
	ListStyleType get_list_style_type() const;

	void set_list_style_position(ListStylePosition p_position);
	ListStylePosition get_list_style_position() const;

	void set_box_sizing(BoxSizing p_box_sizing);
	BoxSizing get_box_sizing() const;

	void set_opacity(float p_opacity);
	float get_opacity() const;

	void set_text_alignment(HorizontalAlignment p_alignment);
	HorizontalAlignment get_text_alignment() const;

	void set_text_direction(TextDirection p_text_direction);
	TextDirection get_text_direction() const;

	void set_language(const String &p_language);
	String get_language() const;

	void add_item(const String &p_text);
	void set_item_count(int p_count);
	int get_item_count() const;
	void remove_item(int p_idx);
	void clear();

	void set_item_text(int p_idx, const String &p_text);
	String get_item_text(int p_idx) const;
	void set_item_value(int p_idx, int p_value);
	int get_item_value(int p_idx) const;
	void set_item_value_enabled(int p_idx, bool p_enabled);
	bool is_item_value_enabled(int p_idx) const;
	void set_item_custom_marker_text(int p_idx, const String &p_marker);
	String get_item_custom_marker_text(int p_idx) const;
	void set_item_metadata(int p_idx, const Variant &p_metadata);
	Variant get_item_metadata(int p_idx) const;
	void set_item_tooltip(int p_idx, const String &p_tooltip);
	String get_item_tooltip(int p_idx) const;
	void set_item_auto_translate_mode(int p_idx, AutoTranslateMode p_mode);
	AutoTranslateMode get_item_auto_translate_mode(int p_idx) const;

	Rect2 get_item_rect(int p_idx) const;
	int get_item_at_position(const Point2 &p_position) const;
	String get_item_marker_text(int p_idx) const;
	int get_hovered_item() const;
	void push_mouse_motion(const Ref<InputEventMouseMotion> &p_event);

	WebList();
};

VARIANT_ENUM_CAST(WebList::ListTag);
VARIANT_ENUM_CAST(WebList::OrderedType);
VARIANT_ENUM_CAST(WebList::ListStyleType);
VARIANT_ENUM_CAST(WebList::ListStylePosition);
VARIANT_ENUM_CAST(WebList::BoxSizing);
