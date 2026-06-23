/**************************************************************************/
/*  web_flex_container.h                                                  */
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

#include "scene/gui/container.h"

// A Container that lays out its children exactly like the CSS Flexible Box
// Layout (flexbox) module as implemented by web browsers.
class WebFlexContainer : public Container {
	GDCLASS(WebFlexContainer, Container);

public:
	// Mirrors CSS `flex-direction`.
	enum FlexDirection {
		FLEX_DIRECTION_ROW,
		FLEX_DIRECTION_ROW_REVERSE,
		FLEX_DIRECTION_COLUMN,
		FLEX_DIRECTION_COLUMN_REVERSE,
	};

	// Mirrors CSS `flex-wrap`.
	enum FlexWrap {
		FLEX_WRAP_NOWRAP,
		FLEX_WRAP_WRAP,
		FLEX_WRAP_WRAP_REVERSE,
	};

	// Mirrors CSS `justify-content` (main axis).
	enum JustifyContent {
		JUSTIFY_FLEX_START,
		JUSTIFY_FLEX_END,
		JUSTIFY_CENTER,
		JUSTIFY_SPACE_BETWEEN,
		JUSTIFY_SPACE_AROUND,
		JUSTIFY_SPACE_EVENLY,
	};

	// Mirrors CSS `align-items` (cross axis, per item default).
	enum AlignItems {
		ALIGN_ITEMS_STRETCH,
		ALIGN_ITEMS_FLEX_START,
		ALIGN_ITEMS_FLEX_END,
		ALIGN_ITEMS_CENTER,
		ALIGN_ITEMS_BASELINE,
	};

	// Mirrors CSS `align-content` (cross axis, line packing for multi-line).
	enum AlignContent {
		ALIGN_CONTENT_STRETCH,
		ALIGN_CONTENT_FLEX_START,
		ALIGN_CONTENT_FLEX_END,
		ALIGN_CONTENT_CENTER,
		ALIGN_CONTENT_SPACE_BETWEEN,
		ALIGN_CONTENT_SPACE_AROUND,
		ALIGN_CONTENT_SPACE_EVENLY,
	};

	// Mirrors CSS `align-self` (per item cross axis override).
	enum AlignSelf {
		ALIGN_SELF_AUTO,
		ALIGN_SELF_STRETCH,
		ALIGN_SELF_FLEX_START,
		ALIGN_SELF_FLEX_END,
		ALIGN_SELF_CENTER,
		ALIGN_SELF_BASELINE,
	};

	// Mirrors CSS `box-sizing`.
	enum BoxSizing {
		BOX_SIZING_CONTENT_BOX,
		BOX_SIZING_BORDER_BOX,
	};

private:
	struct ThemeCache {
		Ref<StyleBox> normal;
		Ref<StyleBox> hover;
		int box_sizing = BOX_SIZING_CONTENT_BOX;
		int opacity = 1000;
		int hover_opacity = 1000;
		int padding_top = 0;
		int padding_right = 0;
		int padding_bottom = 0;
		int padding_left = 0;
		int hover_padding_top = -1;
		int hover_padding_right = -1;
		int hover_padding_bottom = -1;
		int hover_padding_left = -1;
	} theme_cache;

	FlexDirection flex_direction = FLEX_DIRECTION_ROW;
	FlexWrap flex_wrap = FLEX_WRAP_NOWRAP;
	JustifyContent justify_content = JUSTIFY_FLEX_START;
	AlignItems align_items = ALIGN_ITEMS_STRETCH;
	AlignContent align_content = ALIGN_CONTENT_STRETCH;
	float row_gap = 0.0;
	float column_gap = 0.0;
	bool hovered = false;

public:
	// Per flex item resolved state used during a single layout pass.
	struct FlexItem {
		Control *control = nullptr;
		int order = 0;
		int child_index = 0; // Stable tie-breaker for equal `order`.

		float flex_grow = 0.0;
		float flex_shrink = 1.0;
		float flex_basis = -1.0; // < 0 means `auto`.
		AlignSelf align_self = ALIGN_SELF_AUTO;

		float base_size = 0.0; // Flex base size (main axis).
		float hypothetical_main = 0.0; // base clamped to [min,max].
		float target_main = 0.0; // Resolved main size.
		float main_min = 0.0;
		float main_max = 0.0;
		float outer_cross = 0.0; // Natural cross size (used as hypothetical).
		float cross_min = 0.0;
		float cross_max = 0.0;

		bool frozen = false;

		float main_pos = 0.0;
		float cross_pos = 0.0;
		float cross_size = 0.0;
	};

	struct FlexLine {
		int first = 0;
		int count = 0;
		float main_size = 0.0; // Sum of item target main sizes + gaps.
		float cross_size = 0.0;
		float cross_pos = 0.0;
	};

private:
	bool is_row_direction() const { return flex_direction == FLEX_DIRECTION_ROW || flex_direction == FLEX_DIRECTION_ROW_REVERSE; }
	bool is_reverse_direction() const { return flex_direction == FLEX_DIRECTION_ROW_REVERSE || flex_direction == FLEX_DIRECTION_COLUMN_REVERSE; }

	float main_of(const Size2 &p_size) const { return is_row_direction() ? p_size.x : p_size.y; }
	float cross_of(const Size2 &p_size) const { return is_row_direction() ? p_size.y : p_size.x; }
	float main_gap() const { return is_row_direction() ? column_gap : row_gap; }
	float cross_gap() const { return is_row_direction() ? row_gap : column_gap; }

	void _resort();
	void _resolve_flexible_lengths(Vector<FlexItem> &p_items, const FlexLine &p_line, float p_container_main) const;
	AlignSelf _resolve_align(AlignSelf p_self) const;
	Ref<StyleBox> _get_current_stylebox() const;
	Rect2 _get_stylebox_draw_rect() const;
	Rect2 _get_content_rect() const;
	Size2 _get_stylebox_minimum_size() const;
	float _get_current_padding(Side p_side) const;
	float _get_box_margin(Side p_side) const;
	Point2 _get_box_offset() const;
	void _update_theme_opacity();

	// Per-child flex item properties are stored as metadata on the child (keys
	// "_flex_align_self", "_flex_grow", ...) so they follow the child on
	// reparent/duplicate and serialize with the scene.
	Control *_find_child_by_name(const String &p_name) const;

protected:
	virtual void add_child_notify(Node *p_child) override;
	virtual void move_child_notify(Node *p_child) override;
	virtual void remove_child_notify(Node *p_child) override;

	void _notification(int p_what);
	static void _bind_methods();

	bool _set(const StringName &p_name, const Variant &p_value);
	bool _get(const StringName &p_name, Variant &r_ret) const;
	void _get_property_list(List<PropertyInfo> *p_list) const;
	bool _property_can_revert(const StringName &p_name) const;
	bool _property_get_revert(const StringName &p_name, Variant &r_property) const;

public:
	void set_flex_direction(FlexDirection p_direction);
	FlexDirection get_flex_direction() const;

	void set_flex_wrap(FlexWrap p_wrap);
	FlexWrap get_flex_wrap() const;

	void set_justify_content(JustifyContent p_justify);
	JustifyContent get_justify_content() const;

	void set_align_items(AlignItems p_align);
	AlignItems get_align_items() const;

	void set_align_content(AlignContent p_align);
	AlignContent get_align_content() const;

	void set_row_gap(float p_gap);
	float get_row_gap() const;

	void set_column_gap(float p_gap);
	float get_column_gap() const;

	void set_box_sizing(BoxSizing p_box_sizing);
	BoxSizing get_box_sizing() const;

	// Per-child accessors (also reachable from script via the child node name).
	void set_item_align_self(Control *p_child, AlignSelf p_align_self);
	AlignSelf get_item_align_self(Control *p_child) const;
	void set_item_flex_grow(Control *p_child, float p_grow);
	float get_item_flex_grow(Control *p_child) const;
	void set_item_flex_shrink(Control *p_child, float p_shrink);
	float get_item_flex_shrink(Control *p_child) const;
	void set_item_flex_basis(Control *p_child, float p_basis);
	float get_item_flex_basis(Control *p_child) const;
	void set_item_order(Control *p_child, int p_order);
	int get_item_order(Control *p_child) const;

	virtual Size2 get_minimum_size() const override;

	WebFlexContainer();
};

VARIANT_ENUM_CAST(WebFlexContainer::FlexDirection);
VARIANT_ENUM_CAST(WebFlexContainer::FlexWrap);
VARIANT_ENUM_CAST(WebFlexContainer::JustifyContent);
VARIANT_ENUM_CAST(WebFlexContainer::AlignItems);
VARIANT_ENUM_CAST(WebFlexContainer::AlignContent);
VARIANT_ENUM_CAST(WebFlexContainer::AlignSelf);
VARIANT_ENUM_CAST(WebFlexContainer::BoxSizing);
