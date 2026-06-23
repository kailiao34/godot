/**************************************************************************/
/*  web_flex_container.cpp                                                */
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

#include "web_flex_container.h"

#include "core/object/class_db.h"
#include "scene/theme/theme_db.h"

#define META_ALIGN_SELF SNAME("_flex_align_self")
#define META_FLEX_GROW SNAME("_flex_grow")
#define META_FLEX_SHRINK SNAME("_flex_shrink")
#define META_FLEX_BASIS SNAME("_flex_basis")
#define META_ORDER SNAME("_flex_order")

// Stable ordering: CSS `order` ascending, then document order as tie-breaker.
struct _FlexItemOrderComparator {
	bool operator()(const WebFlexContainer::FlexItem &a, const WebFlexContainer::FlexItem &b) const {
		if (a.order != b.order) {
			return a.order < b.order;
		}
		return a.child_index < b.child_index;
	}
};

void WebFlexContainer::_resolve_flexible_lengths(Vector<FlexItem> &p_items, const FlexLine &p_line, float p_container_main) const {
	const int first = p_line.first;
	const int count = p_line.count;
	if (count <= 0) {
		return;
	}

	const float gaps = (count > 1) ? (count - 1) * main_gap() : 0.0;
	const float available = p_container_main - gaps;

	float sum_hypothetical = 0.0;
	for (int i = first; i < first + count; i++) {
		sum_hypothetical += p_items[i].hypothetical_main;
	}
	const float initial_free = available - sum_hypothetical;
	const bool grow = initial_free > 0.0;

	// Initialize: freeze items that cannot flex in the required direction.
	for (int i = first; i < first + count; i++) {
		FlexItem &it = p_items.write[i];
		it.target_main = it.hypothetical_main;
		it.frozen = false;

		const float factor = grow ? it.flex_grow : it.flex_shrink;
		if (factor == 0.0) {
			it.frozen = true;
		} else if (grow && it.base_size > it.hypothetical_main) {
			it.frozen = true; // Already clamped to max.
		} else if (!grow && it.base_size < it.hypothetical_main) {
			it.frozen = true; // Already clamped to min.
		}
	}

	while (true) {
		bool any_unfrozen = false;
		for (int i = first; i < first + count; i++) {
			if (!p_items[i].frozen) {
				any_unfrozen = true;
				break;
			}
		}
		if (!any_unfrozen) {
			break;
		}

		// Remaining free space using frozen target sizes and unfrozen base sizes.
		float remaining = available;
		float sum_grow = 0.0;
		float sum_scaled_shrink = 0.0;
		for (int i = first; i < first + count; i++) {
			const FlexItem &it = p_items[i];
			if (it.frozen) {
				remaining -= it.target_main;
			} else {
				remaining -= it.base_size;
				sum_grow += it.flex_grow;
				sum_scaled_shrink += it.flex_shrink * it.base_size;
			}
		}

		// CSS: if the sum of grow factors is below 1, only a fraction of the
		// initial free space is distributed.
		if (grow && sum_grow < 1.0) {
			const float scaled = initial_free * sum_grow;
			if (Math::abs(scaled) < Math::abs(remaining)) {
				remaining = scaled;
			}
		}

		if (!Math::is_zero_approx(remaining)) {
			for (int i = first; i < first + count; i++) {
				FlexItem &it = p_items.write[i];
				if (it.frozen) {
					continue;
				}
				if (grow) {
					if (sum_grow > 0.0) {
						it.target_main = it.base_size + remaining * (it.flex_grow / sum_grow);
					}
				} else {
					if (sum_scaled_shrink > 0.0) {
						const float scaled = it.flex_shrink * it.base_size;
						it.target_main = it.base_size + remaining * (scaled / sum_scaled_shrink);
					}
				}
			}
		} else {
			for (int i = first; i < first + count; i++) {
				FlexItem &it = p_items.write[i];
				if (!it.frozen) {
					it.target_main = it.base_size;
				}
			}
		}

		// Fix min/max violations and decide which items to freeze.
		float total_violation = 0.0;
		for (int i = first; i < first + count; i++) {
			FlexItem &it = p_items.write[i];
			if (it.frozen) {
				continue;
			}
			float clamped = MAX(it.target_main, it.main_min);
			if (it.main_max >= 0.0) {
				clamped = MIN(clamped, it.main_max);
			}
			total_violation += clamped - it.target_main;
			it.target_main = clamped;
		}

		for (int i = first; i < first + count; i++) {
			FlexItem &it = p_items.write[i];
			if (it.frozen) {
				continue;
			}
			float unclamped_min = it.main_min;
			float unclamped_max = it.main_max;
			if (Math::is_zero_approx(total_violation)) {
				it.frozen = true; // No violations: freeze everything.
			} else if (total_violation > 0.0) {
				// Min violations: freeze items clamped up to their min.
				if (it.target_main <= unclamped_min) {
					it.frozen = true;
				}
			} else {
				// Max violations: freeze items clamped down to their max.
				if (unclamped_max >= 0.0 && it.target_main >= unclamped_max) {
					it.frozen = true;
				}
			}
		}
	}
}

WebFlexContainer::AlignSelf WebFlexContainer::_resolve_align(AlignSelf p_self) const {
	if (p_self != ALIGN_SELF_AUTO) {
		return p_self;
	}
	switch (align_items) {
		case ALIGN_ITEMS_FLEX_START:
			return ALIGN_SELF_FLEX_START;
		case ALIGN_ITEMS_FLEX_END:
			return ALIGN_SELF_FLEX_END;
		case ALIGN_ITEMS_CENTER:
			return ALIGN_SELF_CENTER;
		case ALIGN_ITEMS_BASELINE:
			return ALIGN_SELF_BASELINE;
		default:
			return ALIGN_SELF_STRETCH;
	}
}

Ref<StyleBox> WebFlexContainer::_get_current_stylebox() const {
	if (hovered && theme_cache.hover.is_valid()) {
		return theme_cache.hover;
	}
	return theme_cache.normal;
}

Size2 WebFlexContainer::_get_stylebox_minimum_size() const {
	return Size2(_get_box_margin(SIDE_LEFT) + _get_box_margin(SIDE_RIGHT), _get_box_margin(SIDE_TOP) + _get_box_margin(SIDE_BOTTOM));
}

float WebFlexContainer::_get_current_padding(Side p_side) const {
	int normal_padding = 0;
	int hover_padding = -1;
	switch (p_side) {
		case SIDE_TOP:
			normal_padding = theme_cache.padding_top;
			hover_padding = theme_cache.hover_padding_top;
			break;
		case SIDE_RIGHT:
			normal_padding = theme_cache.padding_right;
			hover_padding = theme_cache.hover_padding_right;
			break;
		case SIDE_BOTTOM:
			normal_padding = theme_cache.padding_bottom;
			hover_padding = theme_cache.hover_padding_bottom;
			break;
		case SIDE_LEFT:
			normal_padding = theme_cache.padding_left;
			hover_padding = theme_cache.hover_padding_left;
			break;
	}
	if (hovered && hover_padding >= 0) {
		return hover_padding;
	}
	return MAX(0, normal_padding);
}

float WebFlexContainer::_get_box_margin(Side p_side) const {
	Ref<StyleBox> stylebox = _get_current_stylebox();
	const float style_margin = stylebox.is_valid() ? stylebox->get_margin(p_side) : 0.0;
	return style_margin + _get_current_padding(p_side);
}

Point2 WebFlexContainer::_get_box_offset() const {
	return Point2(_get_box_margin(SIDE_LEFT), _get_box_margin(SIDE_TOP));
}

Rect2 WebFlexContainer::_get_stylebox_draw_rect() const {
	Ref<StyleBox> stylebox = _get_current_stylebox();
	const Size2 control_size = get_size();
	if (stylebox.is_null()) {
		return Rect2(Point2(), control_size);
	}

	if ((BoxSizing)theme_cache.box_sizing == BOX_SIZING_BORDER_BOX) {
		return Rect2(Point2(), control_size);
	}

	return Rect2(-_get_box_offset(), control_size + _get_stylebox_minimum_size());
}

Rect2 WebFlexContainer::_get_content_rect() const {
	const Size2 control_size = get_size();
	if ((BoxSizing)theme_cache.box_sizing == BOX_SIZING_CONTENT_BOX) {
		return Rect2(Point2(), control_size);
	}

	const Size2 style_minimum = _get_stylebox_minimum_size();
	Size2 content_size = control_size - style_minimum;
	content_size.x = MAX(0.0, content_size.x);
	content_size.y = MAX(0.0, content_size.y);
	return Rect2(_get_box_offset(), content_size);
}

void WebFlexContainer::_update_theme_opacity() {
	const int raw_opacity = hovered ? theme_cache.hover_opacity : theme_cache.opacity;
	const float opacity = CLAMP((float)raw_opacity / 1000.0f, 0.0f, 1.0f);
	Color color = get_modulate();
	color.a = opacity;
	set_modulate(color);
}

void WebFlexContainer::_resort() {
	if (!is_visible_in_tree()) {
		return;
	}

	const Rect2 content_rect = _get_content_rect();
	const Size2 size = content_rect.size;
	const bool row = is_row_direction();
	const float container_main = main_of(size);
	const float container_cross = cross_of(size);
	const float m_gap = main_gap();
	const float c_gap = cross_gap();

	// Collect flex items.
	Vector<FlexItem> items;
	int child_index = 0;
	for (int i = 0; i < get_child_count(); i++) {
		Control *child = as_sortable_control(get_child(i));
		if (!child) {
			continue;
		}

		FlexItem it;
		it.control = child;
		it.child_index = child_index++;
		it.order = get_item_order(child);
		it.flex_grow = MAX(0.0f, (float)get_item_flex_grow(child));
		it.flex_shrink = MAX(0.0f, (float)get_item_flex_shrink(child));
		it.flex_basis = get_item_flex_basis(child);
		it.align_self = get_item_align_self(child);

		const Size2 minsize = child->get_combined_minimum_size();
		const Size2 maxsize = child->get_combined_maximum_size();
		// The child's combined minimum size is its CSS min-width/min-height: a hard
		// floor that Godot also enforces when the rect is committed. Items shrink
		// only down to this (use `custom_minimum_size = 0` for `min-width: 0`).
		it.main_min = main_of(minsize);
		it.cross_min = cross_of(minsize);
		it.main_max = main_of(maxsize);
		it.cross_max = cross_of(maxsize);
		it.outer_cross = cross_of(minsize); // Specified (content) cross size; 0 means auto.

		// Flex base size: explicit basis, or the content main size for `auto`.
		it.base_size = (it.flex_basis >= 0.0) ? it.flex_basis : main_of(minsize);

		float hyp = MAX(it.base_size, it.main_min);
		if (it.main_max >= 0.0) {
			hyp = MIN(hyp, it.main_max);
		}
		it.hypothetical_main = hyp;

		items.push_back(it);
	}

	if (items.is_empty()) {
		return;
	}

	// Apply CSS `order`.
	items.sort_custom<_FlexItemOrderComparator>();

	// Line breaking.
	Vector<FlexLine> lines;
	if (flex_wrap == FLEX_WRAP_NOWRAP) {
		FlexLine line;
		line.first = 0;
		line.count = items.size();
		lines.push_back(line);
	} else {
		int idx = 0;
		while (idx < items.size()) {
			FlexLine line;
			line.first = idx;
			line.count = 0;
			float used = 0.0;
			while (idx < items.size()) {
				const float add = items[idx].hypothetical_main;
				const float gap = (line.count > 0) ? m_gap : 0.0;
				if (line.count > 0 && used + gap + add > container_main) {
					break;
				}
				used += gap + add;
				line.count++;
				idx++;
			}
			lines.push_back(line);
		}
	}

	// Resolve main sizes per line and compute line main extents.
	for (int l = 0; l < lines.size(); l++) {
		_resolve_flexible_lengths(items, lines[l], container_main);
		const FlexLine &line = lines[l];
		float total = 0.0;
		for (int i = line.first; i < line.first + line.count; i++) {
			total += items[i].target_main;
		}
		if (line.count > 1) {
			total += (line.count - 1) * m_gap;
		}
		lines.write[l].main_size = total;
	}

	const bool single_line = (flex_wrap == FLEX_WRAP_NOWRAP);

	// Determine cross size of each line.
	for (int l = 0; l < lines.size(); l++) {
		const FlexLine &line = lines[l];
		float cross = 0.0;
		for (int i = line.first; i < line.first + line.count; i++) {
			cross = MAX(cross, items[i].outer_cross);
		}
		lines.write[l].cross_size = cross;
	}
	if (single_line) {
		lines.write[0].cross_size = container_cross;
	}

	// Cross-axis line packing (align-content) for multi-line containers.
	if (single_line) {
		lines.write[0].cross_pos = 0.0;
	} else {
		float total_lines_cross = 0.0;
		for (int l = 0; l < lines.size(); l++) {
			total_lines_cross += lines[l].cross_size;
		}
		if (lines.size() > 1) {
			total_lines_cross += (lines.size() - 1) * c_gap;
		}
		float free_cross = container_cross - total_lines_cross;

		float leading = 0.0;
		float between = c_gap;

		if (align_content == ALIGN_CONTENT_STRETCH && free_cross > 0.0) {
			const float add = free_cross / lines.size();
			for (int l = 0; l < lines.size(); l++) {
				lines.write[l].cross_size += add;
			}
			leading = 0.0;
			between = c_gap;
		} else {
			const int n = lines.size();
			switch (align_content) {
				case ALIGN_CONTENT_FLEX_END:
					leading = free_cross;
					break;
				case ALIGN_CONTENT_CENTER:
					leading = free_cross * 0.5;
					break;
				case ALIGN_CONTENT_SPACE_BETWEEN:
					if (free_cross > 0.0 && n > 1) {
						between = c_gap + free_cross / (n - 1);
					}
					break;
				case ALIGN_CONTENT_SPACE_AROUND:
					if (free_cross > 0.0) {
						const float u = free_cross / n;
						leading = u * 0.5;
						between = c_gap + u;
					} else {
						leading = free_cross * 0.5; // Acts like center when overflowing.
					}
					break;
				case ALIGN_CONTENT_SPACE_EVENLY:
					if (free_cross > 0.0) {
						const float u = free_cross / (n + 1);
						leading = u;
						between = c_gap + u;
					} else {
						leading = free_cross * 0.5;
					}
					break;
				default: // FLEX_START / STRETCH (no free space).
					leading = 0.0;
					break;
			}
		}

		float cur = leading;
		for (int l = 0; l < lines.size(); l++) {
			lines.write[l].cross_pos = cur;
			cur += lines[l].cross_size + between;
		}
	}

	const bool rtl = is_layout_rtl();
	bool flip_main = is_reverse_direction();
	if (is_row_direction() && rtl) {
		flip_main = !flip_main;
	}

	// Position items within each line.
	for (int l = 0; l < lines.size(); l++) {
		const FlexLine &line = lines[l];

		// Main-axis distribution (justify-content).
		const float free_main = container_main - line.main_size;
		float leading = 0.0;
		float between = m_gap;
		const int n = line.count;

		if (free_main >= 0.0) {
			switch (justify_content) {
				case JUSTIFY_FLEX_END:
					leading = free_main;
					break;
				case JUSTIFY_CENTER:
					leading = free_main * 0.5;
					break;
				case JUSTIFY_SPACE_BETWEEN:
					if (n > 1) {
						between = m_gap + free_main / (n - 1);
					}
					break;
				case JUSTIFY_SPACE_AROUND: {
					const float u = free_main / n;
					leading = u * 0.5;
					between = m_gap + u;
				} break;
				case JUSTIFY_SPACE_EVENLY: {
					const float u = free_main / (n + 1);
					leading = u;
					between = m_gap + u;
				} break;
				default: // FLEX_START.
					leading = 0.0;
					break;
			}
		} else {
			// Overflow: browsers fall back to start/center/end packing.
			switch (justify_content) {
				case JUSTIFY_FLEX_END:
					leading = free_main;
					break;
				case JUSTIFY_CENTER:
				case JUSTIFY_SPACE_AROUND:
				case JUSTIFY_SPACE_EVENLY:
					leading = free_main * 0.5;
					break;
				default: // FLEX_START / SPACE_BETWEEN.
					leading = 0.0;
					break;
			}
		}

		float cur = leading;
		for (int i = line.first; i < line.first + line.count; i++) {
			FlexItem &it = items.write[i];
			it.main_pos = cur;
			cur += it.target_main + between;
		}

		if (flip_main) {
			for (int i = line.first; i < line.first + line.count; i++) {
				FlexItem &it = items.write[i];
				it.main_pos = container_main - it.main_pos - it.target_main;
			}
		}

		// Baseline reference for the line (row direction only). A contentless box
		// has its baseline at its bottom edge, so its ascent equals its cross size.
		float line_baseline = 0.0;
		if (is_row_direction()) {
			for (int i = line.first; i < line.first + line.count; i++) {
				if (_resolve_align(items[i].align_self) == ALIGN_SELF_BASELINE) {
					line_baseline = MAX(line_baseline, items[i].outer_cross);
				}
			}
		}

		// Cross-axis alignment (align-self, falling back to align-items).
		for (int i = line.first; i < line.first + line.count; i++) {
			FlexItem &it = items.write[i];

			const AlignSelf resolved = _resolve_align(it.align_self);
			const float line_cross = line.cross_size;

			if (resolved == ALIGN_SELF_STRETCH) {
				// `stretch` only grows items whose cross size is auto (content 0);
				// an explicit cross size is preserved and aligned to the line start.
				float cs = (it.outer_cross > 0.0) ? it.outer_cross : line_cross;
				if (it.cross_max >= 0.0) {
					cs = MIN(cs, it.cross_max);
				}
				cs = MAX(cs, it.cross_min);
				it.cross_size = cs;
				it.cross_pos = line.cross_pos;
			} else if (resolved == ALIGN_SELF_BASELINE && is_row_direction()) {
				it.cross_size = it.outer_cross;
				it.cross_pos = line.cross_pos + (line_baseline - it.outer_cross);
			} else {
				const float cs = it.outer_cross;
				float offset = 0.0;
				switch (resolved) {
					case ALIGN_SELF_FLEX_END:
						offset = line_cross - cs;
						break;
					case ALIGN_SELF_CENTER:
						offset = (line_cross - cs) * 0.5;
						break;
					default: // FLEX_START / BASELINE in a column (acts as flex-start).
						offset = 0.0;
						break;
				}
				it.cross_size = cs;
				it.cross_pos = line.cross_pos + offset;
			}
		}
	}

	// flex-wrap: wrap-reverse mirrors the entire cross axis, reversing both the
	// line stacking order and the alignment direction within each line.
	if (flex_wrap == FLEX_WRAP_WRAP_REVERSE) {
		for (int i = 0; i < items.size(); i++) {
			FlexItem &it = items.write[i];
			it.cross_pos = container_cross - it.cross_pos - it.cross_size;
		}
	}

	// Commit rects to children.
	for (int i = 0; i < items.size(); i++) {
		const FlexItem &it = items[i];
		Rect2 rect;
		if (row) {
			rect = Rect2(content_rect.position + Point2(it.main_pos, it.cross_pos), Size2(it.target_main, it.cross_size));
		} else {
			rect = Rect2(content_rect.position + Point2(it.cross_pos, it.main_pos), Size2(it.cross_size, it.target_main));
		}
		it.control->set_rect(rect);
		it.control->set_rotation(0);
		it.control->set_scale(Vector2(1, 1));
	}
}

Size2 WebFlexContainer::get_minimum_size() const {
	// An embedded pb_flow_solver is a transient layout calculator. Preserve
	// the caller's containing-block size even when children overflow so the
	// normal flex shrink/alignment algorithm receives the real free space.
	if (has_meta("pb_flow_solver")) {
		return Size2();
	}
	Size2 minimum;
	float main_total = 0.0;
	float cross_max = 0.0;
	int n = 0;
	for (int i = 0; i < get_child_count(); i++) {
		Control *c = as_sortable_control(get_child(i), SortableVisibilityMode::VISIBLE);
		if (!c) {
			continue;
		}
		const Size2 ms = c->get_combined_minimum_size();
		const float main_min = main_of(ms);
		// A shrinkable item only needs its min main size; a non-shrinkable one
		// needs its full preferred (flex base) size.
		const float basis = get_item_flex_basis(c);
		const float base = (basis >= 0.0) ? basis : main_min;
		const float cm = (get_item_flex_shrink(c) > 0.0) ? main_min : MAX(base, main_min);
		const float cc = cross_of(ms);
		if (flex_wrap == FLEX_WRAP_NOWRAP) {
			main_total += cm + (n > 0 ? main_gap() : 0.0);
		} else {
			main_total = MAX(main_total, cm);
		}
		cross_max = MAX(cross_max, cc);
		n++;
	}
	if (is_row_direction()) {
		minimum = Size2(main_total, cross_max);
	} else {
		minimum = Size2(cross_max, main_total);
	}
	if ((BoxSizing)theme_cache.box_sizing == BOX_SIZING_BORDER_BOX) {
		minimum += _get_stylebox_minimum_size();
	}
	return minimum;
}

/* ----------------------------- Child lookup ----------------------------- */

Control *WebFlexContainer::_find_child_by_name(const String &p_name) const {
	for (int i = 0; i < get_child_count(); i++) {
		Control *c = Object::cast_to<Control>(get_child(i));
		if (c && c->get_name() == p_name) {
			return c;
		}
	}
	return nullptr;
}

/* --------------------------- Per-item accessors ------------------------- */

void WebFlexContainer::set_item_align_self(Control *p_child, AlignSelf p_align_self) {
	ERR_FAIL_NULL(p_child);
	if (p_align_self == ALIGN_SELF_AUTO) {
		p_child->remove_meta(META_ALIGN_SELF);
	} else {
		p_child->set_meta(META_ALIGN_SELF, (int)p_align_self);
	}
	queue_sort();
}

WebFlexContainer::AlignSelf WebFlexContainer::get_item_align_self(Control *p_child) const {
	if (p_child && p_child->has_meta(META_ALIGN_SELF)) {
		return (AlignSelf)(int)p_child->get_meta(META_ALIGN_SELF);
	}
	return ALIGN_SELF_AUTO;
}

void WebFlexContainer::set_item_flex_grow(Control *p_child, float p_grow) {
	ERR_FAIL_NULL(p_child);
	if (Math::is_zero_approx(p_grow)) {
		p_child->remove_meta(META_FLEX_GROW);
	} else {
		p_child->set_meta(META_FLEX_GROW, p_grow);
	}
	update_minimum_size();
	queue_sort();
}

float WebFlexContainer::get_item_flex_grow(Control *p_child) const {
	if (p_child && p_child->has_meta(META_FLEX_GROW)) {
		return p_child->get_meta(META_FLEX_GROW);
	}
	return 0.0; // CSS default flex-grow.
}

void WebFlexContainer::set_item_flex_shrink(Control *p_child, float p_shrink) {
	ERR_FAIL_NULL(p_child);
	if (Math::is_equal_approx(p_shrink, 1.0f)) {
		p_child->remove_meta(META_FLEX_SHRINK);
	} else {
		p_child->set_meta(META_FLEX_SHRINK, p_shrink);
	}
	update_minimum_size();
	queue_sort();
}

float WebFlexContainer::get_item_flex_shrink(Control *p_child) const {
	if (p_child && p_child->has_meta(META_FLEX_SHRINK)) {
		return p_child->get_meta(META_FLEX_SHRINK);
	}
	return 1.0; // CSS default flex-shrink.
}

void WebFlexContainer::set_item_flex_basis(Control *p_child, float p_basis) {
	ERR_FAIL_NULL(p_child);
	if (p_basis < 0.0) {
		p_child->remove_meta(META_FLEX_BASIS);
	} else {
		p_child->set_meta(META_FLEX_BASIS, p_basis);
	}
	update_minimum_size();
	queue_sort();
}

float WebFlexContainer::get_item_flex_basis(Control *p_child) const {
	if (p_child && p_child->has_meta(META_FLEX_BASIS)) {
		return p_child->get_meta(META_FLEX_BASIS);
	}
	return -1.0; // CSS default flex-basis: auto.
}

void WebFlexContainer::set_item_order(Control *p_child, int p_order) {
	ERR_FAIL_NULL(p_child);
	if (p_order == 0) {
		p_child->remove_meta(META_ORDER);
	} else {
		p_child->set_meta(META_ORDER, p_order);
	}
	queue_sort();
}

int WebFlexContainer::get_item_order(Control *p_child) const {
	if (p_child && p_child->has_meta(META_ORDER)) {
		return p_child->get_meta(META_ORDER);
	}
	return 0; // CSS default order.
}

/* ----------------------- Container property setters --------------------- */

void WebFlexContainer::set_flex_direction(FlexDirection p_direction) {
	if (flex_direction == p_direction) {
		return;
	}
	flex_direction = p_direction;
	update_minimum_size();
	queue_sort();
}

WebFlexContainer::FlexDirection WebFlexContainer::get_flex_direction() const {
	return flex_direction;
}

void WebFlexContainer::set_flex_wrap(FlexWrap p_wrap) {
	if (flex_wrap == p_wrap) {
		return;
	}
	flex_wrap = p_wrap;
	update_minimum_size();
	queue_sort();
}

WebFlexContainer::FlexWrap WebFlexContainer::get_flex_wrap() const {
	return flex_wrap;
}

void WebFlexContainer::set_justify_content(JustifyContent p_justify) {
	if (justify_content == p_justify) {
		return;
	}
	justify_content = p_justify;
	queue_sort();
}

WebFlexContainer::JustifyContent WebFlexContainer::get_justify_content() const {
	return justify_content;
}

void WebFlexContainer::set_align_items(AlignItems p_align) {
	if (align_items == p_align) {
		return;
	}
	align_items = p_align;
	queue_sort();
}

WebFlexContainer::AlignItems WebFlexContainer::get_align_items() const {
	return align_items;
}

void WebFlexContainer::set_align_content(AlignContent p_align) {
	if (align_content == p_align) {
		return;
	}
	align_content = p_align;
	queue_sort();
}

WebFlexContainer::AlignContent WebFlexContainer::get_align_content() const {
	return align_content;
}

void WebFlexContainer::set_row_gap(float p_gap) {
	if (row_gap == p_gap) {
		return;
	}
	row_gap = p_gap;
	update_minimum_size();
	queue_sort();
}

float WebFlexContainer::get_row_gap() const {
	return row_gap;
}

void WebFlexContainer::set_column_gap(float p_gap) {
	if (column_gap == p_gap) {
		return;
	}
	column_gap = p_gap;
	update_minimum_size();
	queue_sort();
}

float WebFlexContainer::get_column_gap() const {
	return column_gap;
}

void WebFlexContainer::set_box_sizing(BoxSizing p_box_sizing) {
	if ((BoxSizing)theme_cache.box_sizing == p_box_sizing) {
		return;
	}
	theme_cache.box_sizing = p_box_sizing;
	update_minimum_size();
	queue_sort();
	queue_redraw();
}

WebFlexContainer::BoxSizing WebFlexContainer::get_box_sizing() const {
	return (BoxSizing)theme_cache.box_sizing;
}

/* ---------------------------- Child notifies ---------------------------- */

void WebFlexContainer::add_child_notify(Node *p_child) {
	Container::add_child_notify(p_child);
	if (Object::cast_to<Control>(p_child)) {
		notify_property_list_changed();
	}
}

void WebFlexContainer::move_child_notify(Node *p_child) {
	Container::move_child_notify(p_child);
	if (Object::cast_to<Control>(p_child)) {
		notify_property_list_changed();
	}
}

void WebFlexContainer::remove_child_notify(Node *p_child) {
	Container::remove_child_notify(p_child);
	if (Object::cast_to<Control>(p_child)) {
		notify_property_list_changed();
	}
}

void WebFlexContainer::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_DRAW: {
			Ref<StyleBox> stylebox = _get_current_stylebox();
			if (stylebox.is_valid()) {
				stylebox->draw(get_canvas_item(), _get_stylebox_draw_rect());
			}
		} break;

		case NOTIFICATION_SORT_CHILDREN: {
			_resort();
			update_minimum_size();
		} break;

		case NOTIFICATION_THEME_CHANGED: {
			_update_theme_opacity();
			update_minimum_size();
			queue_sort();
			queue_redraw();
		} break;

		case NOTIFICATION_MOUSE_ENTER: {
			hovered = true;
			_update_theme_opacity();
			update_minimum_size();
			queue_sort();
			queue_redraw();
		} break;

		case NOTIFICATION_MOUSE_EXIT: {
			hovered = false;
			_update_theme_opacity();
			update_minimum_size();
			queue_sort();
			queue_redraw();
		} break;

		case NOTIFICATION_TRANSLATION_CHANGED:
		case NOTIFICATION_LAYOUT_DIRECTION_CHANGED: {
			queue_sort();
		} break;
	}
}

/* ------------------- Dynamic per-child inspector list ------------------- */

bool WebFlexContainer::_set(const StringName &p_name, const Variant &p_value) {
	const String name = p_name;
	if (!name.begins_with("flex_items/")) {
		return false;
	}
	const Vector<String> parts = name.split("/");
	if (parts.size() != 3) {
		return false;
	}
	Control *child = _find_child_by_name(parts[1]);
	if (!child) {
		return false;
	}
	const String &prop = parts[2];
	if (prop == "align_self") {
		set_item_align_self(child, (AlignSelf)(int)p_value);
	} else if (prop == "flex_grow") {
		set_item_flex_grow(child, p_value);
	} else if (prop == "flex_shrink") {
		set_item_flex_shrink(child, p_value);
	} else if (prop == "flex_basis") {
		set_item_flex_basis(child, p_value);
	} else if (prop == "order") {
		set_item_order(child, p_value);
	} else {
		return false;
	}
	return true;
}

bool WebFlexContainer::_get(const StringName &p_name, Variant &r_ret) const {
	const String name = p_name;
	if (!name.begins_with("flex_items/")) {
		return false;
	}
	const Vector<String> parts = name.split("/");
	if (parts.size() != 3) {
		return false;
	}
	Control *child = _find_child_by_name(parts[1]);
	if (!child) {
		return false;
	}
	const String &prop = parts[2];
	if (prop == "align_self") {
		r_ret = (int)get_item_align_self(child);
	} else if (prop == "flex_grow") {
		r_ret = get_item_flex_grow(child);
	} else if (prop == "flex_shrink") {
		r_ret = get_item_flex_shrink(child);
	} else if (prop == "flex_basis") {
		r_ret = get_item_flex_basis(child);
	} else if (prop == "order") {
		r_ret = get_item_order(child);
	} else {
		return false;
	}
	return true;
}

void WebFlexContainer::_get_property_list(List<PropertyInfo> *p_list) const {
	for (PropertyInfo &property_info : *p_list) {
		if (property_info.name == StringName("theme_override_constants/box_sizing")) {
			property_info.hint = PROPERTY_HINT_ENUM;
			property_info.hint_string = "Content Box,Border Box";
		} else if (property_info.name == StringName("theme_override_constants/padding_top") ||
				property_info.name == StringName("theme_override_constants/padding_right") ||
				property_info.name == StringName("theme_override_constants/padding_bottom") ||
				property_info.name == StringName("theme_override_constants/padding_left")) {
			property_info.hint = PROPERTY_HINT_RANGE;
			property_info.hint_string = "0,4096,1,or_greater,suffix:px";
		} else if (property_info.name == StringName("theme_override_constants/hover_padding_top") ||
				property_info.name == StringName("theme_override_constants/hover_padding_right") ||
				property_info.name == StringName("theme_override_constants/hover_padding_bottom") ||
				property_info.name == StringName("theme_override_constants/hover_padding_left")) {
			property_info.hint = PROPERTY_HINT_RANGE;
			property_info.hint_string = "-1,4096,1,or_greater,suffix:px";
		}
	}

	for (int i = 0; i < get_child_count(); i++) {
		Control *c = Object::cast_to<Control>(get_child(i));
		if (!c || c->is_set_as_top_level()) {
			continue;
		}
		const String cname = c->get_name();
		const String prefix = "flex_items/" + cname + "/";

		p_list->push_back(PropertyInfo(Variant::NIL, cname, PROPERTY_HINT_NONE, prefix, PROPERTY_USAGE_GROUP));
		p_list->push_back(PropertyInfo(Variant::INT, prefix + "align_self", PROPERTY_HINT_ENUM, "Auto,Stretch,Flex Start,Flex End,Center,Baseline", PROPERTY_USAGE_EDITOR));
		p_list->push_back(PropertyInfo(Variant::FLOAT, prefix + "flex_grow", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater", PROPERTY_USAGE_EDITOR));
		p_list->push_back(PropertyInfo(Variant::FLOAT, prefix + "flex_shrink", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater", PROPERTY_USAGE_EDITOR));
		p_list->push_back(PropertyInfo(Variant::FLOAT, prefix + "flex_basis", PROPERTY_HINT_RANGE, "-1,4000,0.5,or_greater,suffix:px", PROPERTY_USAGE_EDITOR));
		p_list->push_back(PropertyInfo(Variant::INT, prefix + "order", PROPERTY_HINT_RANGE, "-100,100,1,or_greater,or_less", PROPERTY_USAGE_EDITOR));
	}
}

bool WebFlexContainer::_property_can_revert(const StringName &p_name) const {
	const String name = p_name;
	return name.begins_with("flex_items/");
}

bool WebFlexContainer::_property_get_revert(const StringName &p_name, Variant &r_property) const {
	const String name = p_name;
	if (!name.begins_with("flex_items/")) {
		return false;
	}
	if (name.ends_with("/align_self")) {
		r_property = (int)ALIGN_SELF_AUTO;
	} else if (name.ends_with("/flex_grow")) {
		r_property = 0.0;
	} else if (name.ends_with("/flex_shrink")) {
		r_property = 1.0;
	} else if (name.ends_with("/flex_basis")) {
		r_property = -1.0;
	} else if (name.ends_with("/order")) {
		r_property = 0;
	} else {
		return false;
	}
	return true;
}

/* -------------------------------- Binding ------------------------------- */

void WebFlexContainer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_flex_direction", "direction"), &WebFlexContainer::set_flex_direction);
	ClassDB::bind_method(D_METHOD("get_flex_direction"), &WebFlexContainer::get_flex_direction);
	ClassDB::bind_method(D_METHOD("set_flex_wrap", "wrap"), &WebFlexContainer::set_flex_wrap);
	ClassDB::bind_method(D_METHOD("get_flex_wrap"), &WebFlexContainer::get_flex_wrap);
	ClassDB::bind_method(D_METHOD("set_justify_content", "justify"), &WebFlexContainer::set_justify_content);
	ClassDB::bind_method(D_METHOD("get_justify_content"), &WebFlexContainer::get_justify_content);
	ClassDB::bind_method(D_METHOD("set_align_items", "align"), &WebFlexContainer::set_align_items);
	ClassDB::bind_method(D_METHOD("get_align_items"), &WebFlexContainer::get_align_items);
	ClassDB::bind_method(D_METHOD("set_align_content", "align"), &WebFlexContainer::set_align_content);
	ClassDB::bind_method(D_METHOD("get_align_content"), &WebFlexContainer::get_align_content);
	ClassDB::bind_method(D_METHOD("set_row_gap", "gap"), &WebFlexContainer::set_row_gap);
	ClassDB::bind_method(D_METHOD("get_row_gap"), &WebFlexContainer::get_row_gap);
	ClassDB::bind_method(D_METHOD("set_column_gap", "gap"), &WebFlexContainer::set_column_gap);
	ClassDB::bind_method(D_METHOD("get_column_gap"), &WebFlexContainer::get_column_gap);
	ClassDB::bind_method(D_METHOD("set_box_sizing", "box_sizing"), &WebFlexContainer::set_box_sizing);
	ClassDB::bind_method(D_METHOD("get_box_sizing"), &WebFlexContainer::get_box_sizing);

	ClassDB::bind_method(D_METHOD("set_item_align_self", "child", "align_self"), &WebFlexContainer::set_item_align_self);
	ClassDB::bind_method(D_METHOD("get_item_align_self", "child"), &WebFlexContainer::get_item_align_self);
	ClassDB::bind_method(D_METHOD("set_item_flex_grow", "child", "grow"), &WebFlexContainer::set_item_flex_grow);
	ClassDB::bind_method(D_METHOD("get_item_flex_grow", "child"), &WebFlexContainer::get_item_flex_grow);
	ClassDB::bind_method(D_METHOD("set_item_flex_shrink", "child", "shrink"), &WebFlexContainer::set_item_flex_shrink);
	ClassDB::bind_method(D_METHOD("get_item_flex_shrink", "child"), &WebFlexContainer::get_item_flex_shrink);
	ClassDB::bind_method(D_METHOD("set_item_flex_basis", "child", "basis"), &WebFlexContainer::set_item_flex_basis);
	ClassDB::bind_method(D_METHOD("get_item_flex_basis", "child"), &WebFlexContainer::get_item_flex_basis);
	ClassDB::bind_method(D_METHOD("set_item_order", "child", "order"), &WebFlexContainer::set_item_order);
	ClassDB::bind_method(D_METHOD("get_item_order", "child"), &WebFlexContainer::get_item_order);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "flex_direction", PROPERTY_HINT_ENUM, "Row,Row Reverse,Column,Column Reverse"), "set_flex_direction", "get_flex_direction");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "flex_wrap", PROPERTY_HINT_ENUM, "No Wrap,Wrap,Wrap Reverse"), "set_flex_wrap", "get_flex_wrap");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "justify_content", PROPERTY_HINT_ENUM, "Flex Start,Flex End,Center,Space Between,Space Around,Space Evenly"), "set_justify_content", "get_justify_content");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "align_items", PROPERTY_HINT_ENUM, "Stretch,Flex Start,Flex End,Center,Baseline"), "set_align_items", "get_align_items");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "align_content", PROPERTY_HINT_ENUM, "Stretch,Flex Start,Flex End,Center,Space Between,Space Around,Space Evenly"), "set_align_content", "get_align_content");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "row_gap", PROPERTY_HINT_RANGE, "0,1000,0.5,or_greater,suffix:px"), "set_row_gap", "get_row_gap");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "column_gap", PROPERTY_HINT_RANGE, "0,1000,0.5,or_greater,suffix:px"), "set_column_gap", "get_column_gap");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "box_sizing", PROPERTY_HINT_ENUM, "Content Box,Border Box"), "set_box_sizing", "get_box_sizing");

	BIND_THEME_ITEM(Theme::DATA_TYPE_STYLEBOX, WebFlexContainer, normal);
	BIND_THEME_ITEM(Theme::DATA_TYPE_STYLEBOX, WebFlexContainer, hover);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebFlexContainer, box_sizing);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebFlexContainer, opacity);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebFlexContainer, hover_opacity);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebFlexContainer, padding_top);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebFlexContainer, padding_right);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebFlexContainer, padding_bottom);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebFlexContainer, padding_left);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebFlexContainer, hover_padding_top);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebFlexContainer, hover_padding_right);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebFlexContainer, hover_padding_bottom);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebFlexContainer, hover_padding_left);

	BIND_ENUM_CONSTANT(FLEX_DIRECTION_ROW);
	BIND_ENUM_CONSTANT(FLEX_DIRECTION_ROW_REVERSE);
	BIND_ENUM_CONSTANT(FLEX_DIRECTION_COLUMN);
	BIND_ENUM_CONSTANT(FLEX_DIRECTION_COLUMN_REVERSE);

	BIND_ENUM_CONSTANT(FLEX_WRAP_NOWRAP);
	BIND_ENUM_CONSTANT(FLEX_WRAP_WRAP);
	BIND_ENUM_CONSTANT(FLEX_WRAP_WRAP_REVERSE);

	BIND_ENUM_CONSTANT(JUSTIFY_FLEX_START);
	BIND_ENUM_CONSTANT(JUSTIFY_FLEX_END);
	BIND_ENUM_CONSTANT(JUSTIFY_CENTER);
	BIND_ENUM_CONSTANT(JUSTIFY_SPACE_BETWEEN);
	BIND_ENUM_CONSTANT(JUSTIFY_SPACE_AROUND);
	BIND_ENUM_CONSTANT(JUSTIFY_SPACE_EVENLY);

	BIND_ENUM_CONSTANT(ALIGN_ITEMS_STRETCH);
	BIND_ENUM_CONSTANT(ALIGN_ITEMS_FLEX_START);
	BIND_ENUM_CONSTANT(ALIGN_ITEMS_FLEX_END);
	BIND_ENUM_CONSTANT(ALIGN_ITEMS_CENTER);
	BIND_ENUM_CONSTANT(ALIGN_ITEMS_BASELINE);

	BIND_ENUM_CONSTANT(ALIGN_CONTENT_STRETCH);
	BIND_ENUM_CONSTANT(ALIGN_CONTENT_FLEX_START);
	BIND_ENUM_CONSTANT(ALIGN_CONTENT_FLEX_END);
	BIND_ENUM_CONSTANT(ALIGN_CONTENT_CENTER);
	BIND_ENUM_CONSTANT(ALIGN_CONTENT_SPACE_BETWEEN);
	BIND_ENUM_CONSTANT(ALIGN_CONTENT_SPACE_AROUND);
	BIND_ENUM_CONSTANT(ALIGN_CONTENT_SPACE_EVENLY);

	BIND_ENUM_CONSTANT(ALIGN_SELF_AUTO);
	BIND_ENUM_CONSTANT(ALIGN_SELF_STRETCH);
	BIND_ENUM_CONSTANT(ALIGN_SELF_FLEX_START);
	BIND_ENUM_CONSTANT(ALIGN_SELF_FLEX_END);
	BIND_ENUM_CONSTANT(ALIGN_SELF_CENTER);
	BIND_ENUM_CONSTANT(ALIGN_SELF_BASELINE);

	BIND_ENUM_CONSTANT(BOX_SIZING_CONTENT_BOX);
	BIND_ENUM_CONSTANT(BOX_SIZING_BORDER_BOX);
}

WebFlexContainer::WebFlexContainer() {
	set_mouse_filter(MOUSE_FILTER_PASS);
}
