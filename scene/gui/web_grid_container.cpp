/**************************************************************************/
/*  web_grid_container.cpp                                                */
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

#include "web_grid_container.h"

#include "core/config/engine.h"
#include "scene/theme/theme_db.h"
#include "core/input/input_event.h"
#include "core/math/math_funcs.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/gui/button.h"
#include "scene/gui/web_grid_overlay.h"

static bool _web_grid_valid_merge_rect(const Rect2i &p_rect) {
	return p_rect.position.x >= 0 && p_rect.position.y >= 0 &&
			p_rect.size.x > 0 && p_rect.size.y > 0 &&
			(p_rect.size.x > 1 || p_rect.size.y > 1);
}

// Add a merge while maintaining a non-overlapping canonical list. Intersecting
// regions are folded into their bounding rectangle, then checked again because the
// growth can reach another region. This also makes malformed externally supplied
// arrays deterministic and safe for the dense spatial cache.
static void _web_grid_append_merged_rect(Vector<Rect2i> &r_rects, const Rect2i &p_rect) {
	if (!_web_grid_valid_merge_rect(p_rect)) {
		return;
	}
	Rect2i merged = p_rect;
	bool changed = true;
	while (changed) {
		changed = false;
		for (int i = r_rects.size() - 1; i >= 0; i--) {
			if (merged.intersects(r_rects[i])) {
				merged = merged.merge(r_rects[i]);
				r_rects.remove_at(i);
				changed = true;
			}
		}
	}
	r_rects.push_back(merged);
}

void WebGridContainer::_resize_tracks(Vector<GridTrack> &p_tracks, int p_count) {
	int old_size = p_tracks.size();
	p_tracks.resize(p_count);
	for (int i = old_size; i < p_count; i++) {
		p_tracks.write[i] = GridTrack();
	}
}

int WebGridContainer::_get_sortable_child_count() const {
	int count = 0;
	for (int i = 0; i < get_child_count(); i++) {
		if (as_sortable_control(get_child(i), SortableVisibilityMode::IGNORE)) {
			count++;
		}
	}
	return count;
}

void WebGridContainer::_sync_child_aligns() {
	int count = _get_sortable_child_count();
	// Only grow, never shrink. Shrinking would discard per-child alignment that
	// was restored from a saved scene before the child nodes were re-added (a
	// PackedScene sets the parent's properties before adding its children).
	if (count <= child_aligns.size()) {
		return;
	}
	int old_size = child_aligns.size();
	child_aligns.resize(count);
	for (int i = old_size; i < count; i++) {
		child_aligns.write[i] = ChildAlign();
	}
	notify_property_list_changed();
}

// Row-major auto-placement over logical cells. A merged region is one slot: its
// anchor accepts one child and its covered physical cells are skipped.
void WebGridContainer::_place_items(Vector<GridArea> &r_areas, int &r_cols, int &r_rows) const {
	int item_count = 0;
	for (int i = 0; i < get_child_count(); i++) {
		if (as_sortable_control(get_child(i), SortableVisibilityMode::IGNORE)) {
			item_count++;
		}
	}
	r_areas.resize(item_count);

	// Stored merges may extend the explicit grid and therefore create implicit tracks.
	int col_lines = MAX(column_count, 1);
	for (const Rect2i &rect : merged_cell_rects) {
		if (_web_grid_valid_merge_rect(rect)) {
			col_lines = MAX(col_lines, rect.position.x + rect.size.x);
		}
	}
	int max_row = MAX(row_count, 1);
	for (const Rect2i &rect : merged_cell_rects) {
		if (_web_grid_valid_merge_rect(rect)) {
			max_row = MAX(max_row, rect.position.y + rect.size.y);
		}
	}

	// Build the logical-cell owner map once. Rows created later by auto-flow contain no
	// authored merges, so they are ordinary cells and need no topology rebuild.
	_rebuild_merge_cache(col_lines, max_row);

	// Grow an arbitrary candidate rectangle until it fully contains every merged
	// logical cell it touches. The dense owner map avoids scanning all merges per cell.
	// A child landing on a merge anchor with a 1x1 authored span therefore resolves to
	// the complete merged slot, while a span crossing a merge can never split it.
	auto resolve_footprint = [&](int p_column, int p_row, int p_column_span, int p_row_span) -> Rect2i {
		if (p_column < 0 || p_row < 0 || p_column_span < 1 || p_row_span < 1 || p_column + p_column_span > col_lines) {
			return Rect2i();
		}
		Rect2i footprint(p_column, p_row, p_column_span, p_row_span);
		bool changed = true;
		while (changed) {
			changed = false;
			const Rect2i before = footprint;
			const int x0 = CLAMP(footprint.position.x, 0, merge_cache_columns);
			const int y0 = CLAMP(footprint.position.y, 0, merge_cache_rows);
			const int x1 = CLAMP(footprint.position.x + footprint.size.x, 0, merge_cache_columns);
			const int y1 = CLAMP(footprint.position.y + footprint.size.y, 0, merge_cache_rows);
			for (int row = y0; row < y1; row++) {
				for (int column = x0; column < x1; column++) {
					const int owner = merged_cell_owner_cache[row * merge_cache_columns + column];
					if (owner >= 0 && owner < merged_cell_rects.size() && !footprint.encloses(merged_cell_rects[owner])) {
						footprint = footprint.merge(merged_cell_rects[owner]);
					}
				}
			}
			changed = footprint != before;
		}
		if (footprint.position.x < 0 || footprint.position.y < 0 ||
				footprint.position.x + footprint.size.x > col_lines) {
			return Rect2i();
		}
		return footprint;
	};

	// Physical occupancy is stored as row-major 64-bit chunks. This keeps candidate
	// checks O(row_span * touched_words), supports programmatic grids wider than the
	// inspector's 64-column hint, and avoids one byte/object per cell.
	const int words_per_row = (col_lines + 63) / 64;
	Vector<uint64_t> occupied;
	int occupied_rows = 0;
	auto ensure_occupied_rows = [&](int p_rows) {
		if (p_rows <= occupied_rows) {
			return;
		}
		const int old_size = occupied.size();
		occupied.resize(p_rows * words_per_row);
		for (int i = old_size; i < occupied.size(); i++) {
			occupied.write[i] = 0;
		}
		occupied_rows = p_rows;
	};
	auto word_mask = [](int p_begin, int p_end) -> uint64_t {
		const uint64_t high = p_end == 64 ? ~uint64_t(0) : (uint64_t(1) << p_end) - 1;
		const uint64_t low = p_begin == 0 ? 0 : (uint64_t(1) << p_begin) - 1;
		return high & ~low;
	};
	auto is_free = [&](const Rect2i &p_rect) -> bool {
		if (p_rect.size.x < 1 || p_rect.size.y < 1 || p_rect.position.x < 0 || p_rect.position.y < 0 ||
				p_rect.position.x + p_rect.size.x > col_lines) {
			return false;
		}
		ensure_occupied_rows(p_rect.position.y + p_rect.size.y);
		const int first_word = p_rect.position.x / 64;
		const int last_word = (p_rect.position.x + p_rect.size.x - 1) / 64;
		for (int row = p_rect.position.y; row < p_rect.position.y + p_rect.size.y; row++) {
			for (int word = first_word; word <= last_word; word++) {
				const int word_start = word * 64;
				const int begin = MAX(p_rect.position.x - word_start, 0);
				const int end = MIN(p_rect.position.x + p_rect.size.x - word_start, 64);
				if (occupied[row * words_per_row + word] & word_mask(begin, end)) {
					return false;
				}
			}
		}
		return true;
	};
	auto mark = [&](const Rect2i &p_rect) {
		ensure_occupied_rows(p_rect.position.y + p_rect.size.y);
		const int first_word = p_rect.position.x / 64;
		const int last_word = (p_rect.position.x + p_rect.size.x - 1) / 64;
		for (int row = p_rect.position.y; row < p_rect.position.y + p_rect.size.y; row++) {
			for (int word = first_word; word <= last_word; word++) {
				const int word_start = word * 64;
				const int begin = MAX(p_rect.position.x - word_start, 0);
				const int end = MIN(p_rect.position.x + p_rect.size.x - word_start, 64);
				occupied.write[row * words_per_row + word] |= word_mask(begin, end);
			}
		}
	};
	auto store_area = [&](int p_index, const Rect2i &p_rect) {
		mark(p_rect);
		r_areas.write[p_index] = GridArea{ p_rect.position.x, p_rect.position.y, p_rect.size.x, p_rect.size.y };
		max_row = MAX(max_row, p_rect.position.y + p_rect.size.y);
	};

	// Every child is auto-placed. Covered cells reject the anchor check; the merge
	// anchor expands to the full logical region and marks its complete footprint.
	int cur_r = 0;
	int cur_c = 0;
	for (int i = 0; i < item_count; i++) {
		int r = cur_r;
		int c = cur_c;
		while (true) {
			Rect2i candidate = resolve_footprint(c, r, 1, 1);
			if (candidate.position == Vector2i(c, r) && is_free(candidate)) {
				store_area(i, candidate);
				break;
			}
			c++;
			if (c >= col_lines) {
				c = 0;
				r++;
			}
		}
		const GridArea &area = r_areas[i];
		cur_r = area.row;
		cur_c = area.col + area.col_span;
		if (cur_c >= col_lines) {
			cur_c = 0;
			cur_r++;
		}
	}

	r_cols = col_lines;
	r_rows = max_row;
}

// Gathers, for each track on the requested axis, the maximum content-based
// minimum of the children in that track. Items that span several tracks have
// their leftover minimum (after subtracting definite sizes and gaps) distributed
// across the spanned auto/fr tracks, matching the CSS spanning-item contribution.
void WebGridContainer::_compute_track_content_min(bool p_is_columns, const Vector<GridArea> &p_areas, int p_count, float p_available, Vector<float> &r_content_min) const {
	const Vector<GridTrack> &tracks = p_is_columns ? column_tracks : row_tracks;
	float gap = p_is_columns ? column_gap : row_gap;

	r_content_min.resize(p_count);
	for (int i = 0; i < p_count; i++) {
		r_content_min.write[i] = 0.0f;
	}

	struct SpanItem {
		int start;
		int span;
		float min;
	};
	Vector<SpanItem> spanning;

	int valid_index = 0;
	for (int i = 0; i < get_child_count(); i++) {
		Control *c = as_sortable_control(get_child(i), SortableVisibilityMode::IGNORE);
		if (!c) {
			continue;
		}
		int idx = valid_index++;
		if (idx >= p_areas.size()) {
			continue;
		}
		const GridArea &a = p_areas[idx];
		int start = p_is_columns ? a.col : a.row;
		int span = p_is_columns ? a.col_span : a.row_span;
		if (start < 0 || start >= p_count) {
			continue;
		}
		Size2 ms = c->get_combined_minimum_size();
		float v = p_is_columns ? ms.width : ms.height;
		if (span <= 1) {
			if (v > r_content_min[start]) {
				r_content_min.write[start] = v;
			}
		} else {
			spanning.push_back(SpanItem{ start, span, v });
		}
	}

	for (int s = 0; s < spanning.size(); s++) {
		int start = spanning[s].start;
		int end = MIN(start + spanning[s].span, p_count);
		int spanned = end - start;
		if (spanned <= 0) {
			continue;
		}
		float fixed = gap * MAX(spanned - 1, 0);
		Vector<int> auto_tracks;
		Vector<int> fr_tracks;
		for (int t = start; t < end; t++) {
			const GridTrack &gt = (t < tracks.size()) ? tracks[t] : GridTrack();
			if (gt.unit == UNIT_PX) {
				fixed += MAX(gt.value, 0.0f);
			} else if (gt.unit == UNIT_PERCENT) {
				// A percentage track is definite once the available size is known, so
				// it already covers part of a spanning item (it must not receive any
				// of the item's leftover, and it may fully absorb it).
				fixed += MAX(gt.value, 0.0f) / 100.0f * p_available;
			} else if (gt.unit == UNIT_AUTO) {
				fixed += r_content_min[t];
				auto_tracks.push_back(t);
			} else if (gt.unit == UNIT_FR) {
				fr_tracks.push_back(t);
			}
		}
		float deficit = spanning[s].min - fixed;
		if (deficit <= 0.0f) {
			continue;
		}
		// A spanning item that crosses a flexible (fr) track is satisfied by that
		// track growing during fr sizing, NOT by inflating any track's base size
		// (this matches the CSS track sizing algorithm: the spanning-item step only
		// touches intrinsic tracks, and is skipped entirely when an fr track is in
		// the span). Only when no fr track is spanned does the deficit fall to the
		// spanned auto tracks.
		if (fr_tracks.size() > 0 || auto_tracks.size() == 0) {
			continue;
		}
		float add = deficit / auto_tracks.size();
		for (int t = 0; t < auto_tracks.size(); t++) {
			r_content_min.write[auto_tracks[t]] += add;
		}
	}
}

// Resolves track sizes and positions for one axis using the CSS grid track
// sizing algorithm (px / % / auto base sizes, then fr distribution, then content
// distribution for any leftover space).
WebGridContainer::AxisLayout WebGridContainer::_resolve_axis(bool p_is_columns, float p_available, const Vector<GridArea> &p_areas, int p_count, float p_origin) const {
	const Vector<GridTrack> &tracks = p_is_columns ? column_tracks : row_tracks;
	int count = p_count;
	float gap = p_is_columns ? column_gap : row_gap;
	ContentAlign content_align = p_is_columns ? justify_content : align_content;

	AxisLayout layout;
	layout.positions.resize(count);
	layout.sizes.resize(count);
	if (count <= 0) {
		return layout;
	}

	Vector<float> content_min;
	_compute_track_content_min(p_is_columns, p_areas, count, p_available, content_min);

	float total_gap = gap * MAX(count - 1, 0);
	float space_for_tracks = p_available - total_gap;

	// Step 1: base sizes for non-fr tracks (px / % / auto). fr tracks are sized
	// from the leftover space, so they are NOT part of the base sum.
	Vector<float> sizes;
	sizes.resize(count);
	Vector<bool> is_fr;
	is_fr.resize(count);
	int auto_count = 0;
	float total_fr = 0.0f;
	float sum_non_fr_base = 0.0f;
	for (int i = 0; i < count; i++) {
		const GridTrack &t = (i < tracks.size()) ? tracks[i] : GridTrack();
		is_fr.write[i] = false;
		switch (t.unit) {
			case UNIT_PX: {
				sizes.write[i] = MAX(t.value, 0.0f);
				sum_non_fr_base += sizes[i];
			} break;
			case UNIT_PERCENT: {
				sizes.write[i] = MAX(t.value, 0.0f) / 100.0f * p_available;
				sum_non_fr_base += sizes[i];
			} break;
			case UNIT_FR: {
				is_fr.write[i] = true;
				sizes.write[i] = 0.0f;
				total_fr += MAX(t.value, 0.0f);
				// The render server always emits `minmax(0, fr)` for flexible
				// tracks, so an fr track has a zero content floor: content wider
				// than its share overflows the track instead of growing it. Clear
				// the intrinsic minimum here so the fr distribution below (which
				// caps at content_min) matches the browser exactly.
				content_min.write[i] = 0.0f;
			} break;
			case UNIT_AUTO:
			default: {
				sizes.write[i] = content_min[i];
				sum_non_fr_base += sizes[i];
				auto_count++;
			} break;
		}
	}

	float free = space_for_tracks - sum_non_fr_base;

	// Step 2: distribute the leftover among fr tracks by fraction. An fr track may
	// not be smaller than its content floor, so cap those iteratively (matching
	// the CSS grid track sizing algorithm) and re-share the rest.
	if (total_fr > 0.0f) {
		Vector<bool> fr_capped;
		fr_capped.resize(count);
		for (int i = 0; i < count; i++) {
			fr_capped.write[i] = false;
		}
		float remaining_free = MAX(free, 0.0f);
		float remaining_fr = total_fr;
		bool changed = true;
		while (changed) {
			changed = false;
			if (remaining_fr <= 0.0f) {
				break;
			}
			float per_fr = remaining_free / remaining_fr;
			for (int i = 0; i < count; i++) {
				if (!is_fr[i] || fr_capped[i]) {
					continue;
				}
				float frv = MAX((i < tracks.size() ? tracks[i].value : 0.0f), 0.0f);
				if (per_fr * frv < content_min[i]) {
					fr_capped.write[i] = true;
					sizes.write[i] = content_min[i];
					remaining_free -= content_min[i];
					remaining_fr -= frv;
					changed = true;
					break;
				}
			}
		}
		float per_fr = (remaining_fr > 0.0f) ? (MAX(remaining_free, 0.0f) / remaining_fr) : 0.0f;
		for (int i = 0; i < count; i++) {
			if (is_fr[i] && !fr_capped[i]) {
				sizes.write[i] = per_fr * MAX((i < tracks.size() ? tracks[i].value : 0.0f), 0.0f);
			}
		}
	}

	// Recompute the leftover from the final track sizes.
	free = space_for_tracks;
	for (int i = 0; i < count; i++) {
		free -= sizes[i];
	}

	// Step 3: content distribution. Positional values (start/end/center) apply
	// even when the tracks overflow (free < 0). Distributed values (space-*) fall
	// back to `start` when there is no free space.
	float leading = 0.0f;
	float between_extra = 0.0f;
	switch (content_align) {
		case CONTENT_END: {
			leading = free;
		} break;
		case CONTENT_CENTER: {
			leading = free / 2.0f;
		} break;
		case CONTENT_STRETCH: {
			if (free > 0.0f && auto_count > 0) {
				float add = free / auto_count;
				for (int i = 0; i < count; i++) {
					const GridTrack &t = (i < tracks.size()) ? tracks[i] : GridTrack();
					if (t.unit == UNIT_AUTO) {
						sizes.write[i] += add;
					}
				}
			}
		} break;
		case CONTENT_SPACE_BETWEEN: {
			if (free > 0.0f && count > 1) {
				between_extra = free / (count - 1);
			}
		} break;
		case CONTENT_SPACE_AROUND: {
			if (free > 0.0f) {
				float unit = free / count;
				leading = unit / 2.0f;
				between_extra = unit;
			}
		} break;
		case CONTENT_SPACE_EVENLY: {
			if (free > 0.0f) {
				float unit = free / (count + 1);
				leading = unit;
				between_extra = unit;
			}
		} break;
		case CONTENT_START:
		default: {
			leading = 0.0f;
		} break;
	}

	// Step 4: final positions, offset by the leading inset.
	float pos = p_origin + leading;
	for (int i = 0; i < count; i++) {
		layout.positions.write[i] = pos;
		layout.sizes.write[i] = sizes[i];
		pos += sizes[i] + gap + between_extra;
	}

	return layout;
}

// Layout inset = padding from the "padding_*" Theme constants (default 0).
// The panel StyleBox is purely visual and does NOT contribute to the inset, to
// avoid interference from Godot's fallback StyleBox (which has 4px margins and
// is always valid even when no explicit style is set).
float WebGridContainer::_inset_left() const { return MAX(theme_cache.padding_left, 0); }
float WebGridContainer::_inset_top() const { return MAX(theme_cache.padding_top, 0); }
float WebGridContainer::_inset_right() const { return MAX(theme_cache.padding_right, 0); }
float WebGridContainer::_inset_bottom() const { return MAX(theme_cache.padding_bottom, 0); }

WebGridContainer::AxisLayout WebGridContainer::_resolve_axis_box(bool p_is_columns, const Vector<GridArea> &p_areas, int p_count) const {
	Size2 size = get_size();
	float lead = p_is_columns ? _inset_left() : _inset_top();
	float trail = p_is_columns ? _inset_right() : _inset_bottom();
	float full = p_is_columns ? size.width : size.height;
	return _resolve_axis(p_is_columns, MAX(full - lead - trail, 0.0f), p_areas, p_count, lead);
}

void WebGridContainer::_resort() {
	_sync_child_aligns();

	Vector<GridArea> areas;
	int eff_cols = 1;
	int eff_rows = 1;
	_place_items(areas, eff_cols, eff_rows);
	effective_column_count = eff_cols;
	effective_row_count = eff_rows;

	AxisLayout cols = _resolve_axis_box(true, areas, eff_cols);
	AxisLayout rows = _resolve_axis_box(false, areas, eff_rows);

	int valid_index = 0;
	for (int i = 0; i < get_child_count(); i++) {
		Control *c = as_sortable_control(get_child(i), SortableVisibilityMode::IGNORE);
		if (!c) {
			continue;
		}
		int child_idx = valid_index++;
		if (child_idx >= areas.size()) {
			fit_child_in_rect(c, Rect2(Point2(), Size2()));
			continue;
		}
		const GridArea &a = areas[child_idx];
		if (a.col < 0 || a.row < 0 || a.col >= eff_cols || a.row >= eff_rows) {
			fit_child_in_rect(c, Rect2(Point2(), Size2()));
			continue;
		}

		int c1 = CLAMP(a.col + a.col_span - 1, 0, eff_cols - 1);
		int r1 = CLAMP(a.row + a.row_span - 1, 0, eff_rows - 1);
		float cx = cols.positions[a.col];
		float cw = cols.positions[c1] + cols.sizes[c1] - cx;
		float cy = rows.positions[a.row];
		float ch = rows.positions[r1] + rows.sizes[r1] - cy;
		Rect2 cell(Point2(cx, cy), Size2(cw, ch));

		SelfAlign js = SELF_AUTO;
		SelfAlign as = SELF_AUTO;
		if (child_idx < child_aligns.size()) {
			js = child_aligns[child_idx].justify_self;
			as = child_aligns[child_idx].align_self;
		}

		auto resolve_self = [](SelfAlign p_self, ItemsAlign p_items) -> SelfAlign {
			if (p_self != SELF_AUTO) {
				return p_self;
			}
			switch (p_items) {
				case ITEMS_START:
					return SELF_START;
				case ITEMS_END:
					return SELF_END;
				case ITEMS_CENTER:
					return SELF_CENTER;
				case ITEMS_STRETCH:
				default:
					return SELF_STRETCH;
			}
		};

		SelfAlign rj = resolve_self(js, justify_items);
		SelfAlign ra = resolve_self(as, align_items);

		Size2 child_min = c->get_combined_minimum_size();
		Rect2 final_rect;

		if (rj == SELF_STRETCH) {
			final_rect.position.x = cell.position.x;
			final_rect.size.width = cell.size.width;
		} else {
			float w = child_min.width;
			final_rect.size.width = w;
			switch (rj) {
				case SELF_END:
					final_rect.position.x = cell.position.x + cell.size.width - w;
					break;
				case SELF_CENTER:
					final_rect.position.x = cell.position.x + (cell.size.width - w) / 2.0f;
					break;
				case SELF_START:
				default:
					final_rect.position.x = cell.position.x;
					break;
			}
		}

		if (ra == SELF_STRETCH) {
			final_rect.position.y = cell.position.y;
			final_rect.size.height = cell.size.height;
		} else {
			float h = child_min.height;
			final_rect.size.height = h;
			switch (ra) {
				case SELF_END:
					final_rect.position.y = cell.position.y + cell.size.height - h;
					break;
				case SELF_CENTER:
					final_rect.position.y = cell.position.y + (cell.size.height - h) / 2.0f;
					break;
				case SELF_START:
				default:
					final_rect.position.y = cell.position.y;
					break;
			}
		}

		fit_child_in_rect(c, final_rect);
	}

	_update_overlay();
	_update_merge_availability();
}

Size2 WebGridContainer::_get_minimum_size() const {
	Vector<GridArea> areas;
	int eff_cols = 1;
	int eff_rows = 1;
	_place_items(areas, eff_cols, eff_rows);

	Vector<float> col_min;
	Vector<float> row_min;
	// Intrinsic minimum: percentage tracks contribute 0 (available is undefined).
	_compute_track_content_min(true, areas, eff_cols, 0.0f, col_min);
	_compute_track_content_min(false, areas, eff_rows, 0.0f, row_min);

	Size2 ms;
	for (int i = 0; i < eff_cols; i++) {
		const GridTrack &t = (i < column_tracks.size()) ? column_tracks[i] : GridTrack();
		ms.width += (t.unit == UNIT_PX) ? MAX(t.value, 0.0f) : col_min[i];
	}
	for (int i = 0; i < eff_rows; i++) {
		const GridTrack &t = (i < row_tracks.size()) ? row_tracks[i] : GridTrack();
		ms.height += (t.unit == UNIT_PX) ? MAX(t.value, 0.0f) : row_min[i];
	}
	ms.width += column_gap * MAX(eff_cols - 1, 0);
	ms.height += row_gap * MAX(eff_rows - 1, 0);
	// The content box is inset by the StyleBox margins, so the container's minimum outer
	// size must include those insets on top of the tracks' intrinsic minimum.
	ms.width += _inset_left() + _inset_right();
	ms.height += _inset_top() + _inset_bottom();
	return ms;
}

Size2 WebGridContainer::get_minimum_size() const {
	return _get_minimum_size();
}

Rect2 WebGridContainer::_cell_range_rect(int p_col, int p_row, int p_col_span, int p_row_span) const {
	Vector<GridArea> areas;
	int eff_cols = 1;
	int eff_rows = 1;
	_place_items(areas, eff_cols, eff_rows);
	AxisLayout cols = _resolve_axis_box(true, areas, eff_cols);
	AxisLayout rows = _resolve_axis_box(false, areas, eff_rows);
	if (p_col < 0 || p_row < 0 || p_col >= eff_cols || p_row >= eff_rows) {
		return Rect2();
	}
	int c1 = CLAMP(p_col + p_col_span - 1, 0, eff_cols - 1);
	int r1 = CLAMP(p_row + p_row_span - 1, 0, eff_rows - 1);
	float x = cols.positions[p_col];
	float w = cols.positions[c1] + cols.sizes[c1] - x;
	float y = rows.positions[p_row];
	float h = rows.positions[r1] + rows.sizes[r1] - y;
	return Rect2(Point2(x, y), Size2(w, h));
}

PackedFloat32Array WebGridContainer::get_resolved_column_offsets() const {
	Vector<GridArea> areas;
	int ec = 1, er = 1;
	_place_items(areas, ec, er);
	AxisLayout l = _resolve_axis_box(true, areas, ec);
	PackedFloat32Array out;
	out.resize(l.positions.size());
	for (int i = 0; i < l.positions.size(); i++) {
		out.write[i] = l.positions[i];
	}
	return out;
}

PackedFloat32Array WebGridContainer::get_resolved_column_sizes() const {
	Vector<GridArea> areas;
	int ec = 1, er = 1;
	_place_items(areas, ec, er);
	AxisLayout l = _resolve_axis_box(true, areas, ec);
	PackedFloat32Array out;
	out.resize(l.sizes.size());
	for (int i = 0; i < l.sizes.size(); i++) {
		out.write[i] = l.sizes[i];
	}
	return out;
}

PackedFloat32Array WebGridContainer::get_resolved_row_offsets() const {
	Vector<GridArea> areas;
	int ec = 1, er = 1;
	_place_items(areas, ec, er);
	AxisLayout l = _resolve_axis_box(false, areas, er);
	PackedFloat32Array out;
	out.resize(l.positions.size());
	for (int i = 0; i < l.positions.size(); i++) {
		out.write[i] = l.positions[i];
	}
	return out;
}

PackedFloat32Array WebGridContainer::get_resolved_row_sizes() const {
	Vector<GridArea> areas;
	int ec = 1, er = 1;
	_place_items(areas, ec, er);
	AxisLayout l = _resolve_axis_box(false, areas, er);
	PackedFloat32Array out;
	out.resize(l.sizes.size());
	for (int i = 0; i < l.sizes.size(); i++) {
		out.write[i] = l.sizes[i];
	}
	return out;
}

float WebGridContainer::get_axis_available(bool p_is_columns) const {
	// The available space for tracks is the content box: the container size minus the
	// border+padding insets on that axis.
	if (p_is_columns) {
		return MAX(get_size().width - _inset_left() - _inset_right(), 0.0f);
	}
	return MAX(get_size().height - _inset_top() - _inset_bottom(), 0.0f);
}

int WebGridContainer::get_effective_column_count() const {
	Vector<GridArea> areas;
	int ec = 1, er = 1;
	_place_items(areas, ec, er);
	return ec;
}

int WebGridContainer::get_effective_row_count() const {
	Vector<GridArea> areas;
	int ec = 1, er = 1;
	_place_items(areas, ec, er);
	return er;
}

Rect2i WebGridContainer::get_child_resolved_grid_rect(int p_index) const {
	Vector<GridArea> areas;
	int ec = 1, er = 1;
	_place_items(areas, ec, er);
	ERR_FAIL_INDEX_V(p_index, areas.size(), Rect2i());
	const GridArea &area = areas[p_index];
	return Rect2i(area.col, area.row, area.col_span, area.row_span);
}

//
// Interaction bridge: grid-line dragging.
//

Dictionary WebGridContainer::find_line_at(const Point2 &p_local, float p_radius) const {
	Dictionary out;
	out["axis"] = String("");
	out["index"] = -1;

	Vector<GridArea> areas;
	int ec = 1, er = 1;
	_place_items(areas, ec, er);
	AxisLayout cols = _resolve_axis_box(true, areas, ec);
	AxisLayout rows = _resolve_axis_box(false, areas, er);
	Size2 size = get_size();
	Vector<Rect2i> merged = _merged_rects();

	float best = p_radius + 1.0f;
	String best_axis;
	int best_idx = -1;

	for (int b = 1; b < ec; b++) {
		float line_x = (cols.positions[b - 1] + cols.sizes[b - 1] + cols.positions[b]) * 0.5f;
		// Shrink the hit radius so two close lines never overlap their zones.
		float eff = MIN(p_radius, MIN(cols.sizes[b - 1], cols.sizes[b]) * 0.5f);
		if (eff <= 0.0f) {
			continue;
		}
		if (p_local.y < -p_radius || p_local.y > size.height + p_radius) {
			continue;
		}
		// A boundary that runs through a merged cell's interior has no drawn line there
		// and must not be draggable (item: merged interior lines are inert).
		if (_boundary_interior_at(true, b, p_local, cols, rows, merged)) {
			continue;
		}
		float d = Math::abs(p_local.x - line_x);
		if (d <= eff && d < best) {
			best = d;
			best_axis = "column";
			best_idx = b - 1;
		}
	}
	for (int b = 1; b < er; b++) {
		float line_y = (rows.positions[b - 1] + rows.sizes[b - 1] + rows.positions[b]) * 0.5f;
		float eff = MIN(p_radius, MIN(rows.sizes[b - 1], rows.sizes[b]) * 0.5f);
		if (eff <= 0.0f) {
			continue;
		}
		if (p_local.x < -p_radius || p_local.x > size.width + p_radius) {
			continue;
		}
		if (_boundary_interior_at(false, b, p_local, cols, rows, merged)) {
			continue;
		}
		float d = Math::abs(p_local.y - line_y);
		if (d <= eff && d < best) {
			best = d;
			best_axis = "row";
			best_idx = b - 1;
		}
	}

	if (best_idx >= 0) {
		out["axis"] = best_axis;
		out["index"] = best_idx;
	}
	return out;
}

void WebGridContainer::_set_track_silent(bool p_is_columns, int p_index, int p_unit, float p_value) {
	if (p_index < 0) {
		return;
	}
	Vector<GridTrack> &tr = p_is_columns ? column_tracks : row_tracks;
	if (p_index >= tr.size()) {
		int old = tr.size();
		tr.resize(p_index + 1);
		for (int k = old; k <= p_index; k++) {
			tr.write[k] = GridTrack();
		}
	}
	tr.write[p_index].unit = (TrackUnit)p_unit;
	tr.write[p_index].value = p_value;
}

void WebGridContainer::_apply_line_target(int p_track, float p_target_size, bool p_ctrl) {
	bool is_cols = line_drag_is_columns;
	const Vector<GridTrack> &tr = is_cols ? column_tracks : row_tracks;
	int count = line_drag_sizes.size();
	int i = p_track;
	if (i < 0 || i >= count) {
		return;
	}

	if (p_ctrl) {
		float ratio = (line_drag_sizes[i] > 0.01f) ? (p_target_size / line_drag_sizes[i]) : 1.0f;
		for (int j = 0; j < count; j++) {
			int u = line_drag_units[j];
			float scaled = line_drag_sizes[j] * ratio;
			if (u == UNIT_AUTO || u == UNIT_PX) {
				_set_track_silent(is_cols, j, UNIT_PX, scaled);
			} else if (u == UNIT_PERCENT) {
				_set_track_silent(is_cols, j, UNIT_PERCENT, line_drag_values[j] * ratio);
			} else { // FR
				_set_track_silent(is_cols, j, UNIT_FR, line_drag_values[j] * ratio);
			}
		}
		return;
	}

	int u = (i < tr.size()) ? (int)tr[i].unit : (int)UNIT_AUTO;
	switch (u) {
		case UNIT_AUTO:
		case UNIT_PX: {
			_set_track_silent(is_cols, i, UNIT_PX, p_target_size);
		} break;
		case UNIT_PERCENT: {
			float avail = get_axis_available(is_cols);
			if (avail > 0.01f) {
				_set_track_silent(is_cols, i, UNIT_PERCENT, p_target_size / avail * 100.0f);
			}
		} break;
		case UNIT_FR: {
			PackedFloat32Array sz = is_cols ? get_resolved_column_sizes() : get_resolved_row_sizes();
			float fr_px = 0.0f;
			float fr_tot = 0.0f;
			for (int j = 0; j < count; j++) {
				if (j < tr.size() && tr[j].unit == UNIT_FR) {
					fr_px += (j < sz.size()) ? sz[j] : 0.0f;
					fr_tot += MAX(tr[j].value, 0.0f);
				}
			}
			float per_fr = (fr_tot > 0.0f && fr_px > 0.0f) ? (fr_px / fr_tot) : 0.0f;
			if (per_fr > 0.01f) {
				_set_track_silent(is_cols, i, UNIT_FR, p_target_size / per_fr);
			} else {
				_set_track_silent(is_cols, i, UNIT_PX, p_target_size);
			}
		} break;
	}
}

// Writes one side of a dragged boundary to p_target_px in that track's own unit.
// fr tracks are handled by the caller (_apply_boundary_target); here a fixed track
// keeps its unit (px stays px, % stays %) and an auto track is converted to px
// because `auto` cannot carry a forced size.
void WebGridContainer::_set_boundary_side(int p_index, float p_target_px) {
	bool is_cols = line_drag_is_columns;
	int unit = (p_index < line_drag_units.size()) ? line_drag_units[p_index] : (int)UNIT_AUTO;
	p_target_px = MAX(p_target_px, 0.0f);
	if (unit == UNIT_PERCENT) {
		float avail = line_drag_available;
		if (avail > 0.01f) {
			_set_track_silent(is_cols, p_index, UNIT_PERCENT, p_target_px / avail * 100.0f);
		}
	} else {
		// AUTO or PX -> px.
		_set_track_silent(is_cols, p_index, UNIT_PX, p_target_px);
	}
}

// Boundary drag. The two tracks adjacent to the boundary share a fixed combined
// size (their snapshot total); moving the boundary trades pixels between them and
// leaves every other track untouched. The unit combination decides how:
//   fr | fr  -> trade in fr-value space, preserving the pair's fr-value sum, so the
//               per-fr ratio (hence all other fr tracks) is unchanged.
//   fixed|fr -> write only the fixed side; the fr track absorbs the remainder.
//   else     -> write both sides exactly (auto becomes px).
void WebGridContainer::_apply_boundary_target(int p_boundary, float p_target_size_i) {
	int i = p_boundary;
	int j = i + 1;
	int count = line_drag_sizes.size();
	if (i < 0 || j >= count) {
		return;
	}
	float pair_total = line_drag_sizes[i] + line_drag_sizes[j];
	float new_i = CLAMP(p_target_size_i, 0.0f, pair_total);
	float new_j = pair_total - new_i;

	int ui = (i < line_drag_units.size()) ? line_drag_units[i] : (int)UNIT_AUTO;
	int uj = (j < line_drag_units.size()) ? line_drag_units[j] : (int)UNIT_AUTO;

	if (ui == UNIT_FR && uj == UNIT_FR) {
		float sum_val = MAX(line_drag_values[i], 0.0f) + MAX(line_drag_values[j], 0.0f);
		float fi = (pair_total > 0.01f) ? sum_val * (new_i / pair_total) : sum_val * 0.5f;
		float fj = sum_val - fi;
		_set_track_silent(line_drag_is_columns, i, UNIT_FR, fi);
		_set_track_silent(line_drag_is_columns, j, UNIT_FR, fj);
		return;
	}

	// At most one side is fr: write the non-fr side(s) exactly; an fr side is left
	// as-is and absorbs the leftover space when the layout re-resolves.
	if (ui != UNIT_FR) {
		_set_boundary_side(i, new_i);
	}
	if (uj != UNIT_FR) {
		_set_boundary_side(j, new_j);
	}
}

void WebGridContainer::begin_line_drag(bool p_is_columns, int p_boundary, bool p_ctrl) {
	int count = p_is_columns ? get_effective_column_count() : get_effective_row_count();
	if (p_boundary < 0 || p_boundary >= count - 1) {
		return;
	}
	// Bake implicit tracks into the explicit count so a dragged implicit boundary
	// becomes a real, editable track.
	if (p_is_columns && column_count < count) {
		set_column_count(count);
	} else if (!p_is_columns && row_count < count) {
		set_row_count(count);
	}

	line_dragging = true;
	line_drag_is_columns = p_is_columns;
	line_drag_boundary = p_boundary;
	line_drag_ctrl = p_ctrl;

	line_drag_units.resize(count);
	line_drag_values.resize(count);
	line_drag_sizes.resize(count);
	PackedFloat32Array sizes = p_is_columns ? get_resolved_column_sizes() : get_resolved_row_sizes();
	const Vector<GridTrack> &tr = p_is_columns ? column_tracks : row_tracks;
	for (int j = 0; j < count; j++) {
		GridTrack gt = (j < tr.size()) ? tr[j] : GridTrack();
		line_drag_units.write[j] = (int)gt.unit;
		line_drag_values.write[j] = gt.value;
		line_drag_sizes.write[j] = (j < sizes.size()) ? sizes[j] : 0.0f;
	}
	line_drag_available = get_axis_available(p_is_columns);
}

void WebGridContainer::update_line_drag(const Point2 &p_local) {
	if (!line_dragging) {
		return;
	}
	float axis_pos = line_drag_is_columns ? p_local.x : p_local.y;
	int i = line_drag_boundary;

	if (line_drag_ctrl) {
		// Track positions are offset by the leading content-box inset (border+padding),
		// so the accumulated edge must start there to match the cursor's local space.
		float start_edge = line_drag_is_columns ? _inset_left() : _inset_top();
		for (int j = 0; j <= i; j++) {
			start_edge += line_drag_sizes[j];
		}
		float gap = line_drag_is_columns ? column_gap : row_gap;
		start_edge += gap * i;
		float new_size = MAX(axis_pos - (start_edge - line_drag_sizes[i]), 0.0f);
		_apply_line_target(i, new_size, true);
	} else {
		// Iterate so the dragged boundary converges onto the cursor even when other
		// tracks (auto/fr/stretch) redistribute in response to the change. The
		// resolved layout is recomputed each iteration; this is what keeps the line
		// glued to the mouse instead of lagging a frame behind. _apply_boundary_target
		// trades between the two adjacent tracks only, so the rest of the grid (and,
		// for an fr|fr boundary, every other fr track) stays put.
		for (int iter = 0; iter < 8; iter++) {
			PackedFloat32Array off = line_drag_is_columns ? get_resolved_column_offsets() : get_resolved_row_offsets();
			if (i >= off.size()) {
				break;
			}
			float start_i = off[i];
			float target = MAX(axis_pos - start_i, 0.0f);
			_apply_boundary_target(i, target);
		}
	}

	queue_sort();
	update_minimum_size();
	_update_overlay();
	emit_signal(SNAME("grid_changed"));
}

void WebGridContainer::end_line_drag() {
	line_dragging = false;
	line_drag_boundary = -1;
}

bool WebGridContainer::is_line_dragging() const {
	return line_dragging;
}

//
// Interaction bridge: cell selection + merge.
//

Vector2i WebGridContainer::cell_at(const Point2 &p_local) const {
	Vector<GridArea> areas;
	int ec = 1, er = 1;
	_place_items(areas, ec, er);
	AxisLayout cols = _resolve_axis_box(true, areas, ec);
	AxisLayout rows = _resolve_axis_box(false, areas, er);

	int col = -1;
	int row = -1;
	for (int c = 0; c < ec; c++) {
		if (p_local.x >= cols.positions[c] && p_local.x <= cols.positions[c] + cols.sizes[c]) {
			col = c;
			break;
		}
	}
	for (int r = 0; r < er; r++) {
		if (p_local.y >= rows.positions[r] && p_local.y <= rows.positions[r] + rows.sizes[r]) {
			row = r;
			break;
		}
	}
	if (col < 0 || row < 0) {
		return Vector2i(-1, -1);
	}
	return Vector2i(col, row);
}

void WebGridContainer::_invalidate_merge_cache() {
	merge_cache_dirty = true;
}

void WebGridContainer::_rebuild_merge_cache(int p_columns, int p_rows) const {
	p_columns = MAX(p_columns, 1);
	p_rows = MAX(p_rows, 1);
	if (!merge_cache_dirty && merge_cache_columns == p_columns && merge_cache_rows == p_rows) {
		return;
	}

	merge_cache_columns = p_columns;
	merge_cache_rows = p_rows;
	const int mask_size = merge_cache_columns * merge_cache_rows;
	merged_cell_owner_cache.resize(mask_size);
	merged_vertical_interior_cache.resize(mask_size);
	merged_horizontal_interior_cache.resize(mask_size);
	for (int i = 0; i < mask_size; i++) {
		merged_cell_owner_cache.write[i] = -1;
		merged_vertical_interior_cache.write[i] = false;
		merged_horizontal_interior_cache.write[i] = false;
	}

	// A vertical boundary uses index row * columns + boundary, where boundary is
	// 1..columns-1. Horizontal boundaries use boundary * columns + column. Keeping the
	// unused outer-edge slots makes all lookups branch-free and costs only O(rows*cols).
	for (int merge_index = 0; merge_index < merged_cell_rects.size(); merge_index++) {
		const Rect2i &rect = merged_cell_rects[merge_index];
		const int x0 = CLAMP(rect.position.x, 0, merge_cache_columns);
		const int y0 = CLAMP(rect.position.y, 0, merge_cache_rows);
		const int x1 = CLAMP(rect.position.x + rect.size.x, 0, merge_cache_columns);
		const int y1 = CLAMP(rect.position.y + rect.size.y, 0, merge_cache_rows);
		for (int row = y0; row < y1; row++) {
			for (int column = x0; column < x1; column++) {
				merged_cell_owner_cache.write[row * merge_cache_columns + column] = merge_index;
			}
			for (int boundary = x0 + 1; boundary < x1; boundary++) {
				merged_vertical_interior_cache.write[row * merge_cache_columns + boundary] = true;
			}
		}
		for (int boundary = y0 + 1; boundary < y1; boundary++) {
			for (int column = x0; column < x1; column++) {
				merged_horizontal_interior_cache.write[boundary * merge_cache_columns + column] = true;
			}
		}
	}

	merge_cache_dirty = false;
}

Vector<Rect2i> WebGridContainer::_merged_rects() const {
	return merged_cell_rects;
}

Rect2i WebGridContainer::_snap_rect_to_merges(const Rect2i &p_rect) const {
	Rect2i rect = p_rect;
	Vector<Rect2i> merged = _merged_rects();
	// Repeat until no merged area is only partially covered: each pass may grow the
	// rect enough to start overlapping another merged area.
	bool changed = true;
	while (changed) {
		changed = false;
		for (int i = 0; i < merged.size(); i++) {
			const Rect2i &m = merged[i];
			if (rect.intersects(m) && !rect.encloses(m)) {
				rect = rect.merge(m);
				changed = true;
			}
		}
	}
	return rect;
}

Array WebGridContainer::get_merged_rects() const {
	Vector<Rect2i> merged = _merged_rects();
	Array out;
	for (int i = 0; i < merged.size(); i++) {
		out.push_back(merged[i]);
	}
	return out;
}

bool WebGridContainer::_boundary_interior_at(bool p_is_columns, int p_boundary, const Point2 &p_local, const AxisLayout &p_cols, const AxisLayout &p_rows, const Vector<Rect2i> &p_merged) const {
	// p_boundary is the index of the line between track p_boundary-1 and p_boundary.
	for (int m = 0; m < p_merged.size(); m++) {
		const Rect2i &mr = p_merged[m];
		if (p_is_columns) {
			// The column boundary must run strictly inside the merged area's columns,
			// and the cursor's y must fall within the merged area's vertical pixel span.
			if (mr.position.x < p_boundary && p_boundary < mr.position.x + mr.size.x) {
				int r0 = mr.position.y;
				int r1 = mr.position.y + mr.size.y - 1;
				if (r0 >= 0 && r1 < p_rows.positions.size()) {
					float top = p_rows.positions[r0];
					float bottom = p_rows.positions[r1] + p_rows.sizes[r1];
					if (p_local.y >= top && p_local.y <= bottom) {
						return true;
					}
				}
			}
		} else {
			if (mr.position.y < p_boundary && p_boundary < mr.position.y + mr.size.y) {
				int c0 = mr.position.x;
				int c1 = mr.position.x + mr.size.x - 1;
				if (c0 >= 0 && c1 < p_cols.positions.size()) {
					float left = p_cols.positions[c0];
					float right = p_cols.positions[c1] + p_cols.sizes[c1];
					if (p_local.x >= left && p_local.x <= right) {
						return true;
					}
				}
			}
		}
	}
	return false;
}

Array WebGridContainer::get_grid_line_segments() const {
	Array out;
	Vector<GridArea> areas;
	int ec = 1, er = 1;
	_place_items(areas, ec, er);
	AxisLayout cols = _resolve_axis_box(true, areas, ec);
	AxisLayout rows = _resolve_axis_box(false, areas, er);
	_rebuild_merge_cache(ec, er);

	// A boundary segment that runs through the interior of a merged area is omitted
	// (the merged cell reads as one block, like an Excel merged cell).
	auto col_interior = [&](int b, int r) -> bool {
		return b > 0 && b < merge_cache_columns && r >= 0 && r < merge_cache_rows &&
				merged_vertical_interior_cache[r * merge_cache_columns + b];
	};
	auto row_interior = [&](int b, int c) -> bool {
		return b > 0 && b < merge_cache_rows && c >= 0 && c < merge_cache_columns &&
				merged_horizontal_interior_cache[b * merge_cache_columns + c];
	};

	// Column boundaries: vertical lines at the centre of each column gap. Consecutive
	// non-interior rows are joined into a single continuous segment so the line is not
	// broken by the row gaps (and never extends beyond the grid).
	for (int b = 1; b < ec; b++) {
		float x = (cols.positions[b - 1] + cols.sizes[b - 1] + cols.positions[b]) * 0.5f;
		int run_start = -1;
		for (int r = 0; r <= er; r++) {
			bool active = (r < er) && !col_interior(b, r);
			if (active && run_start < 0) {
				run_start = r;
			} else if (!active && run_start >= 0) {
				int last = r - 1;
				PackedVector2Array seg;
				seg.push_back(Vector2(x, rows.positions[run_start]));
				seg.push_back(Vector2(x, rows.positions[last] + rows.sizes[last]));
				out.push_back(seg);
				run_start = -1;
			}
		}
	}
	// Row boundaries: horizontal lines, same run logic across columns.
	for (int b = 1; b < er; b++) {
		float y = (rows.positions[b - 1] + rows.sizes[b - 1] + rows.positions[b]) * 0.5f;
		int run_start = -1;
		for (int c = 0; c <= ec; c++) {
			bool active = (c < ec) && !row_interior(b, c);
			if (active && run_start < 0) {
				run_start = c;
			} else if (!active && run_start >= 0) {
				int last = c - 1;
				PackedVector2Array seg;
				seg.push_back(Vector2(cols.positions[run_start], y));
				seg.push_back(Vector2(cols.positions[last] + cols.sizes[last], y));
				out.push_back(seg);
				run_start = -1;
			}
		}
	}
	return out;
}

Array WebGridContainer::get_gap_rects() const {
	Array out;
	if (column_gap <= 0.0f && row_gap <= 0.0f) {
		return out;
	}
	Vector<GridArea> areas;
	int ec = 1, er = 1;
	_place_items(areas, ec, er);
	AxisLayout cols = _resolve_axis_box(true, areas, ec);
	AxisLayout rows = _resolve_axis_box(false, areas, er);
	_rebuild_merge_cache(ec, er);

	// A gap strip that runs through a merged area's interior is omitted (the merged cell
	// covers the gap it spans, exactly as the grid lines there are omitted).
	auto col_interior = [&](int b, int r) -> bool {
		return b > 0 && b < merge_cache_columns && r >= 0 && r < merge_cache_rows &&
				merged_vertical_interior_cache[r * merge_cache_columns + b];
	};
	auto row_interior = [&](int b, int c) -> bool {
		return b > 0 && b < merge_cache_rows && c >= 0 && c < merge_cache_columns &&
				merged_horizontal_interior_cache[b * merge_cache_columns + c];
	};

	// Column gaps: vertical strips between adjacent columns, spanning runs of rows.
	if (column_gap > 0.0f) {
		for (int b = 1; b < ec; b++) {
			float x0 = cols.positions[b - 1] + cols.sizes[b - 1];
			float x1 = cols.positions[b];
			if (x1 - x0 <= 0.0f) {
				continue;
			}
			int run_start = -1;
			for (int r = 0; r <= er; r++) {
				bool active = (r < er) && !col_interior(b, r);
				if (active && run_start < 0) {
					run_start = r;
				} else if (!active && run_start >= 0) {
					int last = r - 1;
					float y0 = rows.positions[run_start];
					float y1 = rows.positions[last] + rows.sizes[last];
					out.push_back(Rect2(x0, y0, x1 - x0, y1 - y0));
					run_start = -1;
				}
			}
		}
	}
	// Row gaps: horizontal strips between adjacent rows, spanning runs of columns.
	if (row_gap > 0.0f) {
		for (int b = 1; b < er; b++) {
			float y0 = rows.positions[b - 1] + rows.sizes[b - 1];
			float y1 = rows.positions[b];
			if (y1 - y0 <= 0.0f) {
				continue;
			}
			int run_start = -1;
			for (int c = 0; c <= ec; c++) {
				bool active = (c < ec) && !row_interior(b, c);
				if (active && run_start < 0) {
					run_start = c;
				} else if (!active && run_start >= 0) {
					int last = c - 1;
					float x0 = cols.positions[run_start];
					float x1 = cols.positions[last] + cols.sizes[last];
					out.push_back(Rect2(x0, y0, x1 - x0, y1 - y0));
					run_start = -1;
				}
			}
		}
	}
	return out;
}

void WebGridContainer::_draw_box() {
	if (theme_cache.panel_style.is_valid()) {
		draw_style_box(theme_cache.panel_style, Rect2(Point2(), get_size()));
	}
}

void WebGridContainer::select_cell(const Vector2i &p_cell, bool p_shift) {
	if (p_cell.x < 0 || p_cell.y < 0) {
		return;
	}
	if (p_shift && has_selection) {
		int x0 = MIN(selection_anchor.x, p_cell.x);
		int y0 = MIN(selection_anchor.y, p_cell.y);
		int x1 = MAX(selection_anchor.x, p_cell.x);
		int y1 = MAX(selection_anchor.y, p_cell.y);
		selection_rect = Rect2i(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
	} else {
		selection_anchor = p_cell;
		selection_rect = Rect2i(p_cell.x, p_cell.y, 1, 1);
	}
	selection_rect = _snap_rect_to_merges(selection_rect);
	has_selection = true;
	_update_overlay();
	_update_merge_availability();
	emit_signal(SNAME("cells_selected"), selection_rect);
}

void WebGridContainer::begin_cell_drag(const Vector2i &p_cell) {
	if (p_cell.x < 0 || p_cell.y < 0) {
		return;
	}
	selection_anchor = p_cell;
	selection_rect = _snap_rect_to_merges(Rect2i(p_cell.x, p_cell.y, 1, 1));
	has_selection = true;
	cell_dragging = true;
	_update_overlay();
	_update_merge_availability();
	emit_signal(SNAME("cells_selected"), selection_rect);
}

void WebGridContainer::update_cell_drag(const Vector2i &p_cell) {
	if (!cell_dragging || p_cell.x < 0 || p_cell.y < 0) {
		return;
	}
	int x0 = MIN(selection_anchor.x, p_cell.x);
	int y0 = MIN(selection_anchor.y, p_cell.y);
	int x1 = MAX(selection_anchor.x, p_cell.x);
	int y1 = MAX(selection_anchor.y, p_cell.y);
	selection_rect = _snap_rect_to_merges(Rect2i(x0, y0, x1 - x0 + 1, y1 - y0 + 1));
	_update_overlay();
	_update_merge_availability();
	emit_signal(SNAME("cells_selected"), selection_rect);
}

void WebGridContainer::end_cell_drag() {
	cell_dragging = false;
}

void WebGridContainer::clear_cell_selection() {
	if (!has_selection) {
		return;
	}
	has_selection = false;
	cell_dragging = false;
	selection_rect = Rect2i();
	_update_overlay();
	_update_merge_availability();
	emit_signal(SNAME("cells_selected"), selection_rect);
}

bool WebGridContainer::has_cell_selection() const {
	return has_selection;
}

Rect2i WebGridContainer::get_selection_rect() const {
	return has_selection ? selection_rect : Rect2i();
}

bool WebGridContainer::_selection_is_inside_grid() const {
	if (!has_selection || selection_rect.position.x < 0 || selection_rect.position.y < 0 ||
			selection_rect.size.x < 1 || selection_rect.size.y < 1) {
		return false;
	}
	Vector<GridArea> areas;
	int columns = 1;
	int rows = 1;
	_place_items(areas, columns, rows);
	return selection_rect.position.x + selection_rect.size.x <= columns &&
			selection_rect.position.y + selection_rect.size.y <= rows;
}

bool WebGridContainer::can_merge_selected_cells() const {
	return is_grid_editable() && _selection_is_inside_grid() &&
			(selection_rect.size.x > 1 || selection_rect.size.y > 1);
}

bool WebGridContainer::can_unmerge_selected_cells() const {
	if (!is_grid_editable() || !_selection_is_inside_grid()) {
		return false;
	}
	Vector<Rect2i> merged = _merged_rects();
	for (const Rect2i &rect : merged) {
		if (selection_rect.intersects(rect)) {
			return true;
		}
	}
	return false;
}

void WebGridContainer::_update_merge_availability() {
	if (merge_operation_in_progress) {
		return;
	}
	const bool merge_available = can_merge_selected_cells();
	const bool unmerge_available = can_unmerge_selected_cells();
	if (merge_available != last_merge_available) {
		last_merge_available = merge_available;
		emit_signal(SNAME("merge_available_changed"), merge_available);
	}
	if (unmerge_available != last_unmerge_available) {
		last_unmerge_available = unmerge_available;
		emit_signal(SNAME("unmerge_available_changed"), unmerge_available);
	}
}

int WebGridContainer::merge_selected_cells() {
	// A merge must change the grid topology, so a single cell is never a valid merge.
	if (!can_merge_selected_cells()) {
		return -1;
	}
	Rect2i sel = selection_rect;

	Vector<GridArea> areas;
	int ec = 1, er = 1;
	_place_items(areas, ec, er);

	int target = -1;
	// Prefer the child that currently occupies the selection's top-left cell.
	for (int idx = 0; idx < areas.size(); idx++) {
		const GridArea &a = areas[idx];
		if (sel.position.x >= a.col && sel.position.x < a.col + a.col_span &&
				sel.position.y >= a.row && sel.position.y < a.row + a.row_span) {
			target = idx;
			break;
		}
	}
	if (target < 0) {
		// Otherwise the first child whose origin lies inside the selection.
		for (int idx = 0; idx < areas.size(); idx++) {
			const GridArea &a = areas[idx];
			if (a.col >= sel.position.x && a.col < sel.position.x + sel.size.x &&
					a.row >= sel.position.y && a.row < sel.position.y + sel.size.y) {
				target = idx;
				break;
			}
		}
	}
	merge_operation_in_progress = true;

	// Merge topology belongs exclusively to the grid. Child alignment is deliberately
	// untouched, so adding, removing, or moving children cannot change the topology.
	Vector<Rect2i> next_merges;
	for (const Rect2i &rect : merged_cell_rects) {
		if (!sel.intersects(rect)) {
			next_merges.push_back(rect);
		}
	}
	_web_grid_append_merged_rect(next_merges, sel);
	merged_cell_rects = next_merges;

	_invalidate_merge_cache();
	queue_sort();
	update_minimum_size();
	_update_overlay();
	merge_operation_in_progress = false;
	emit_signal(SNAME("merged_cells_changed"));
	emit_signal(SNAME("grid_changed"));
	emit_signal(SNAME("cells_merged"), target, sel);
	clear_cell_selection();
	// -2 identifies a successful empty-cell merge; -1 is reserved for failure.
	return target >= 0 ? target : -2;
}

int WebGridContainer::unmerge_selected_cells() {
	if (!can_unmerge_selected_cells()) {
		return 0;
	}
	Rect2i sel = selection_rect;

	Vector<GridArea> areas;
	int ec = 1, er = 1;
	_place_items(areas, ec, er);

	Vector<Rect2i> effective_before = _merged_rects();
	Vector<Rect2i> affected;
	for (const Rect2i &rect : effective_before) {
		if (sel.intersects(rect)) {
			affected.push_back(rect);
		}
	}
	Vector<int> affected_owners;
	for (const Rect2i &rect : affected) {
		int owner = -1;
		for (int idx = 0; idx < areas.size(); idx++) {
			const GridArea &area = areas[idx];
			if (Rect2i(area.col, area.row, area.col_span, area.row_span).has_point(rect.position)) {
				owner = idx;
				break;
			}
		}
		affected_owners.push_back(owner);
	}

	merge_operation_in_progress = true;
	Vector<Rect2i> next_merges;
	for (const Rect2i &rect : merged_cell_rects) {
		if (!sel.intersects(rect)) {
			next_merges.push_back(rect);
		}
	}
	merged_cell_rects = next_merges;

	_invalidate_merge_cache();
	queue_sort();
	update_minimum_size();
	selection_rect = _snap_rect_to_merges(selection_rect);
	_update_overlay();
	merge_operation_in_progress = false;
	emit_signal(SNAME("merged_cells_changed"));
	emit_signal(SNAME("grid_changed"));
	for (int owner : affected_owners) {
		emit_signal(SNAME("cells_unmerged"), owner);
	}
	_update_merge_availability();
	emit_signal(SNAME("cells_selected"), selection_rect);
	return affected.size();
}

void WebGridContainer::set_merged_cell_rects(const Array &p_rects) {
	Vector<Rect2i> normalized;
	for (int i = 0; i < p_rects.size(); i++) {
		if (p_rects[i].get_type() == Variant::RECT2I) {
			_web_grid_append_merged_rect(normalized, p_rects[i]);
		}
	}
	bool equal = normalized.size() == merged_cell_rects.size();
	for (int i = 0; equal && i < normalized.size(); i++) {
		equal = normalized[i] == merged_cell_rects[i];
	}
	if (equal) {
		return;
	}
	merged_cell_rects = normalized;
	_invalidate_merge_cache();
	queue_sort();
	update_minimum_size();
	_update_overlay();
	emit_signal(SNAME("merged_cells_changed"));
	emit_signal(SNAME("grid_changed"));
	_update_merge_availability();
}

Array WebGridContainer::get_merged_cell_rects() const {
	Array out;
	for (const Rect2i &rect : merged_cell_rects) {
		out.push_back(rect);
	}
	return out;
}

//
// CSS export / import.
//

// Trim trailing zeros so 200.0 prints as "200" and 1.5 as "1.5".
static String _css_fmt_num(float p_v) {
	String s = String::num(p_v, 4);
	if (s.find(".") != -1) {
		while (s.ends_with("0")) {
			s = s.substr(0, s.length() - 1);
		}
		if (s.ends_with(".")) {
			s = s.substr(0, s.length() - 1);
		}
	}
	return s;
}

static const char *SELF_CSS_NAMES[] = { "auto", "stretch", "start", "end", "center" };

static WebGridContainer::SelfAlign _css_to_self(const String &p_name) {
	String n = p_name.strip_edges().to_lower();
	for (int i = 0; i < 5; i++) {
		if (n == SELF_CSS_NAMES[i]) {
			return (WebGridContainer::SelfAlign)i;
		}
	}
	return WebGridContainer::SELF_AUTO;
}

String WebGridContainer::_track_to_css(TrackUnit p_unit, float p_value) {
	switch (p_unit) {
		case UNIT_PX:
			return _css_fmt_num(MAX(p_value, 0.0f)) + "px";
		case UNIT_PERCENT:
			return _css_fmt_num(MAX(p_value, 0.0f)) + "%";
		case UNIT_FR:
			return _css_fmt_num(MAX(p_value, 0.0f)) + "fr";
		case UNIT_AUTO:
		default:
			return "auto";
	}
}

bool WebGridContainer::_css_to_track(const String &p_token, TrackUnit &r_unit, float &r_value) {
	String t = p_token.strip_edges().to_lower();
	if (t.is_empty()) {
		return false;
	}
	if (t == "auto") {
		r_unit = UNIT_AUTO;
		r_value = 0.0f;
		return true;
	}
	if (t.ends_with("fr")) {
		r_unit = UNIT_FR;
		r_value = t.substr(0, t.length() - 2).to_float();
		return true;
	}
	if (t.ends_with("px")) {
		r_unit = UNIT_PX;
		r_value = t.substr(0, t.length() - 2).to_float();
		return true;
	}
	if (t.ends_with("%")) {
		r_unit = UNIT_PERCENT;
		r_value = t.substr(0, t.length() - 1).to_float();
		return true;
	}
	if (t.is_valid_float()) {
		// A bare number is treated as pixels.
		r_unit = UNIT_PX;
		r_value = t.to_float();
		return true;
	}
	return false;
}

String WebGridContainer::export_css() const {
	String out;
	out += "grid {\n";
	out += "\tdisplay: grid;\n";

	String rows_css;
	for (int i = 0; i < row_count; i++) {
		const GridTrack &t = (i < row_tracks.size()) ? row_tracks[i] : GridTrack();
		if (i > 0) {
			rows_css += " ";
		}
		rows_css += _track_to_css(t.unit, t.value);
	}
	out += "\tgrid-template-rows: " + rows_css + ";\n";

	String cols_css;
	for (int i = 0; i < column_count; i++) {
		const GridTrack &t = (i < column_tracks.size()) ? column_tracks[i] : GridTrack();
		if (i > 0) {
			cols_css += " ";
		}
		cols_css += _track_to_css(t.unit, t.value);
	}
	out += "\tgrid-template-columns: " + cols_css + ";\n";

	// CSS `gap` shorthand is `<row-gap> <column-gap>` (collapsed to one value if equal).
	if (row_gap != 0.0f || column_gap != 0.0f) {
		if (row_gap == column_gap) {
			out += "\tgap: " + _css_fmt_num(row_gap) + "px;\n";
		} else {
			out += "\tgap: " + _css_fmt_num(row_gap) + "px " + _css_fmt_num(column_gap) + "px;\n";
		}
	}

	out += "}\n";

	// Per-child alignment rules, named after the child node. Default children are
	// skipped entirely.
	Vector<String> names;
	for (int i = 0; i < get_child_count(); i++) {
		Control *c = as_sortable_control(get_child(i), SortableVisibilityMode::IGNORE);
		if (c) {
			names.push_back(c->get_name());
		}
	}
	for (int idx = 0; idx < names.size(); idx++) {
		ChildAlign ca = (idx < child_aligns.size()) ? child_aligns[idx] : ChildAlign();
		bool is_default = ca.justify_self == SELF_AUTO && ca.align_self == SELF_AUTO;
		if (is_default) {
			continue;
		}
		String body;
		if (ca.justify_self != SELF_AUTO) {
			body += String("\tjustify-self: ") + SELF_CSS_NAMES[(int)ca.justify_self] + ";\n";
		}
		if (ca.align_self != SELF_AUTO) {
			body += String("\talign-self: ") + SELF_CSS_NAMES[(int)ca.align_self] + ";\n";
		}
		out += String(names[idx]) + " {\n" + body + "}\n";
	}
	return out;
}

void WebGridContainer::import_css(const String &p_css) {
	int pos = 0;
	while (true) {
		int brace = p_css.find("{", pos);
		if (brace < 0) {
			break;
		}
		String selector = p_css.substr(pos, brace - pos).strip_edges();
		int close = p_css.find("}", brace + 1);
		if (close < 0) {
			break;
		}
		String body = p_css.substr(brace + 1, close - brace - 1);
		pos = close + 1;

		// Split the rule body into "prop: value" declarations.
		Vector<String> decls = body.split(";", false);
		Vector<String> props;
		Vector<String> values;
		for (int d = 0; d < decls.size(); d++) {
			int colon = decls[d].find(":");
			if (colon < 0) {
				continue;
			}
			props.push_back(decls[d].substr(0, colon).strip_edges().to_lower());
			values.push_back(decls[d].substr(colon + 1).strip_edges());
		}

		if (selector.to_lower() == "grid") {
			for (int d = 0; d < props.size(); d++) {
				const String &p = props[d];
				const String &v = values[d];
				if (p == "grid-template-columns" || p == "grid-template-rows") {
					bool is_cols = p == "grid-template-columns";
					Vector<String> toks = v.split_spaces();
					if (toks.is_empty()) {
						continue;
					}
					if (is_cols) {
						set_column_count(toks.size());
					} else {
						set_row_count(toks.size());
					}
					for (int t = 0; t < toks.size(); t++) {
						TrackUnit u;
						float val;
						if (!_css_to_track(toks[t], u, val)) {
							continue;
						}
						if (is_cols) {
							set_column_track_unit(t, u);
							set_column_track_value(t, val);
						} else {
							set_row_track_unit(t, u);
							set_row_track_value(t, val);
						}
					}
				} else if (p == "gap") {
					Vector<String> toks = v.split_spaces();
					if (toks.size() == 1) {
						float g = toks[0].trim_suffix("px").to_float();
						set_row_gap(g);
						set_column_gap(g);
					} else if (toks.size() >= 2) {
						set_row_gap(toks[0].trim_suffix("px").to_float());
						set_column_gap(toks[1].trim_suffix("px").to_float());
					}
				} else if (p == "row-gap") {
					set_row_gap(v.trim_suffix("px").to_float());
				} else if (p == "column-gap") {
					set_column_gap(v.trim_suffix("px").to_float());
				}
			}
			continue;
		}

		// Otherwise it is a per-child rule keyed by the child node's name.
		int child_idx = -1;
		int running = 0;
		for (int i = 0; i < get_child_count(); i++) {
			Control *c = as_sortable_control(get_child(i), SortableVisibilityMode::IGNORE);
			if (!c) {
				continue;
			}
			if (String(c->get_name()) == selector) {
				child_idx = running;
				break;
			}
			running++;
		}
		if (child_idx < 0 || child_idx >= child_aligns.size()) {
			continue;
		}
		for (int d = 0; d < props.size(); d++) {
			const String &p = props[d];
			const String &v = values[d];
			if (p == "justify-self") {
				set_child_justify_self(child_idx, _css_to_self(v));
			} else if (p == "align-self") {
				set_child_align_self(child_idx, _css_to_self(v));
			}
		}
	}
	queue_sort();
	update_minimum_size();
	emit_signal(SNAME("grid_changed"));
}

//
// Runtime interaction overlay.
//

void WebGridContainer::_ensure_overlay() {
	if (interaction_overlay) {
		return;
	}
	// The interaction overlay is a runtime-only affordance. In the editor the
	// WebGridContainerEditorPlugin already draws the grid lines / selection and
	// handles input on the canvas, so creating the overlay here would paint a second
	// (mis-registered) set of lines over the node. Never create it at edit time.
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	if (draw_grid == DRAW_GRID_NEVER) {
		// The overlay is both the painter and the input capture, so DRAW_GRID_NEVER
		// simply means it never exists: no lines, no cell selection, no merge button.
		return;
	}
	// By default the top-level root wins GUI hit-testing against canvas siblings.
	// A host application with a flat logical canvas can instead provide an
	// external parent and place this root immediately after the grid's logical
	// subtree. That preserves both grid editing and the host's foreground order.
	interaction_root = memnew(Control);
	interaction_root->set_name("WebGridRuntimeOverlay");
	interaction_root->set_meta(SNAME("pb_grid_overlay"), true);
	interaction_root->set_mouse_filter(MOUSE_FILTER_IGNORE);
	interaction_root->set_clip_contents(true);
	interaction_root->set_anchors_preset(PRESET_TOP_LEFT);
	if (runtime_overlay_parent) {
		runtime_overlay_parent->add_child(interaction_root);
	} else {
		add_child(interaction_root, false, INTERNAL_MODE_FRONT);
		interaction_root->set_as_top_level(true);
		interaction_root->set_z_index(RSE::CANVAS_ITEM_Z_MAX);
	}

	interaction_overlay = memnew(Control);
	interaction_overlay->set_mouse_filter(MOUSE_FILTER_STOP);
	interaction_overlay->set_focus_mode(Control::FOCUS_CLICK);
	// Pin to the top-left and drive the size explicitly from _update_overlay(). The
	// node draws in its own local space, so the overlay must sit exactly at (0,0) with
	// the container's size for the lines / selection to register on the grid.
	interaction_overlay->set_anchors_preset(PRESET_TOP_LEFT);
	interaction_overlay->set_position(Point2());
	interaction_overlay->connect(SNAME("draw"), callable_mp(this, &WebGridContainer::_overlay_draw));
	interaction_overlay->connect(SNAME("gui_input"), callable_mp(this, &WebGridContainer::_overlay_gui_input));
	// Clear the cell selection when the grid loses focus at runtime.
	interaction_overlay->connect(SNAME("focus_exited"), callable_mp(this, &WebGridContainer::clear_cell_selection));
	interaction_root->add_child(interaction_overlay);
	// A web editor may keep logical grid children as flat canvas siblings instead
	// of parenting their Controls below this container. Godot GUI hit-testing walks
	// scene-tree order, not CanvasItem z-index. The top-level root above lets the
	// native WebGridContainer remain the sole input surface without asking the host
	// app to disable mouse input on every grid child.

	merge_button = memnew(Button);
	merge_button->set_text("Merge");
	merge_button->set_visible(false);
	// Do NOT take focus: the overlay clears the cell selection on focus_exited, so if the
	// button grabbed focus on press the selection would be gone before `pressed` fires and
	// the merge would be a no-op.
	merge_button->set_focus_mode(Control::FOCUS_NONE);
	merge_button->connect(SNAME("pressed"), callable_mp(this, &WebGridContainer::_merge_button_pressed));
	interaction_overlay->add_child(merge_button);

	_update_overlay();
}

void WebGridContainer::_destroy_overlay() {
	if (!interaction_overlay) {
		return;
	}
	interaction_root->queue_free();
	interaction_root = nullptr;
	interaction_overlay = nullptr;
	merge_button = nullptr;
}

void WebGridContainer::_refresh_overlay_presence() {
	if (runtime_interactive && draw_grid != DRAW_GRID_NEVER) {
		_ensure_overlay(); // A no-op at edit time (see its guard).
	} else {
		_destroy_overlay();
	}
}

void WebGridContainer::_update_overlay() {
	if (!interaction_root || !interaction_overlay) {
		return;
	}
	const Transform2D grid_xform = get_global_transform();
	Rect2 visible_rect = get_global_rect();
	for (Node *ancestor = get_parent(); ancestor; ancestor = ancestor->get_parent()) {
		Control *clip = Object::cast_to<Control>(ancestor);
		if (clip && clip->is_clipping_contents()) {
			visible_rect = visible_rect.intersection(clip->get_global_rect());
		}
	}
	Rect2 local_clip = grid_xform.affine_inverse().xform(visible_rect);
	local_clip = local_clip.intersection(Rect2(Point2(), get_size()));
	const bool overlay_visible = is_visible_in_tree() && local_clip.size.x > 0.0f && local_clip.size.y > 0.0f;
	interaction_root->set_visible(overlay_visible);
	if (!overlay_visible) {
		return;
	}
	Transform2D overlay_xform = grid_xform;
	if (runtime_overlay_parent && interaction_root->get_parent() == runtime_overlay_parent) {
		overlay_xform = runtime_overlay_parent->get_global_transform().affine_inverse() * grid_xform;
	}
	interaction_root->set_position(overlay_xform.xform(local_clip.position));
	interaction_root->set_rotation(overlay_xform.get_rotation());
	interaction_root->set_scale(overlay_xform.get_scale());
	interaction_root->set_size(local_clip.size);
	interaction_overlay->set_position(-local_clip.position);
	interaction_overlay->set_size(get_size());
	interaction_overlay->queue_redraw();

	if (merge_button) {
		bool show = runtime_interactive && show_merge_button && can_merge_selected_cells();
		merge_button->set_visible(show);
		if (show) {
			Rect2 r = _cell_range_rect(selection_rect.position.x, selection_rect.position.y, selection_rect.size.x, selection_rect.size.y);
			Size2 bs = merge_button->get_combined_minimum_size();
			merge_button->set_size(bs);
			// Centre the button inside the selection and clamp it to the container, so it
			// is always within the overlay's rect and therefore always clickable (a button
			// placed above a top-row selection would fall outside the overlay and receive
			// no input).
			Size2 cs = get_size();
			float bx = r.position.x + (r.size.x - bs.x) / 2.0f;
			float by = r.position.y + (r.size.y - bs.y) / 2.0f;
			bx = CLAMP(bx, 0.0f, MAX(cs.width - bs.x, 0.0f));
			by = CLAMP(by, 0.0f, MAX(cs.height - bs.y, 0.0f));
			merge_button->set_position(Vector2(bx, by));
		}
	}
}

void WebGridContainer::_overlay_draw() {
	if (!interaction_overlay) {
		return;
	}
	// The DevTools-style overlay (hatched cells and gaps, merged blocks, selection,
	// frame and numbered line rulers) is rendered by the same code the canvas editor
	// gizmo uses, so edit time and runtime look identical. The overlay Control sits
	// exactly on the container, so no extra transform is needed.
	WebGridOverlay::draw(interaction_overlay, this);
}

void WebGridContainer::_overlay_gui_input(const Ref<InputEvent> &p_event) {
	if (!interaction_overlay) {
		return;
	}
	Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_valid() && mb->get_button_index() == MouseButton::LEFT) {
		if (mb->is_pressed()) {
			// Keyboard focus is not mouse capture. Explicitly retain the click so motion
			// and release events keep reaching this overlay when the pointer leaves the
			// narrow line hit area during a drag.
			interaction_overlay->grab_click_focus();
			interaction_overlay->grab_focus();
			Point2 pos = mb->get_position();
			Dictionary line = find_line_at(pos);
			if ((int)line["index"] >= 0) {
				begin_line_drag(String(line["axis"]) == "column", (int)line["index"], mb->is_ctrl_pressed());
			} else {
				Vector2i cell = cell_at(pos);
				if (mb->is_shift_pressed()) {
					select_cell(cell, true);
				} else {
					begin_cell_drag(cell);
				}
			}
			interaction_overlay->accept_event();
		} else {
			if (line_dragging) {
				end_line_drag();
				emit_signal(SNAME("grid_changed"));
			}
			if (cell_dragging) {
				end_cell_drag();
			}
			interaction_overlay->accept_event();
		}
		return;
	}

	Ref<InputEventMouseMotion> mm = p_event;
	if (mm.is_valid()) {
		Point2 pos = mm->get_position();
		if (line_dragging) {
			update_line_drag(pos);
			interaction_overlay->accept_event();
		} else if (cell_dragging) {
			update_cell_drag(cell_at(pos));
			interaction_overlay->accept_event();
		} else {
			Dictionary line = find_line_at(pos);
			if ((int)line["index"] >= 0) {
				interaction_overlay->set_default_cursor_shape(String(line["axis"]) == "column" ? CURSOR_HSIZE : CURSOR_VSIZE);
			} else {
				interaction_overlay->set_default_cursor_shape(CURSOR_ARROW);
			}
		}
	}
}

void WebGridContainer::_merge_button_pressed() {
	merge_selected_cells();
}

void WebGridContainer::set_draw_grid(DrawGrid p_mode) {
	if (draw_grid == p_mode) {
		return;
	}
	draw_grid = p_mode;
	if (draw_grid == DRAW_GRID_NEVER) {
		// Nothing may linger from a mode that allowed editing.
		end_line_drag();
		clear_cell_selection();
	}
	_refresh_overlay_presence();
	_update_merge_availability();
	// The editor canvas overlay repaints on this signal.
	emit_signal(SNAME("grid_changed"));
	queue_redraw();
}

WebGridContainer::DrawGrid WebGridContainer::get_draw_grid() const {
	return draw_grid;
}

bool WebGridContainer::is_grid_editable() const {
	return draw_grid != DRAW_GRID_NEVER;
}

void WebGridContainer::set_runtime_interactive(bool p_enabled) {
	if (runtime_interactive == p_enabled) {
		return;
	}
	runtime_interactive = p_enabled;
	_refresh_overlay_presence();
	emit_signal(SNAME("grid_changed"));
}

bool WebGridContainer::is_runtime_interactive() const {
	return runtime_interactive;
}

void WebGridContainer::set_runtime_overlay_parent(Control *p_parent) {
	ERR_FAIL_COND_MSG(p_parent && is_ancestor_of(p_parent), "The runtime overlay parent cannot be a descendant of the WebGridContainer.");
	if (runtime_overlay_parent == p_parent) {
		return;
	}
	if (interaction_root) {
		interaction_root->set_visible(false);
		_destroy_overlay();
	}
	runtime_overlay_parent = p_parent;
	_refresh_overlay_presence();
}

Control *WebGridContainer::get_runtime_overlay_parent() const {
	return runtime_overlay_parent;
}

Control *WebGridContainer::get_runtime_overlay_control() const {
	return interaction_root;
}

void WebGridContainer::set_show_merge_button(bool p_enabled) {
	if (show_merge_button == p_enabled) {
		return;
	}
	show_merge_button = p_enabled;
	_update_overlay();
	emit_signal(SNAME("grid_changed"));
}

bool WebGridContainer::is_show_merge_button() const {
	return show_merge_button;
}

void WebGridContainer::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_SORT_CHILDREN: {
			_resort();
			update_minimum_size();
		} break;

		case NOTIFICATION_DRAW: {
			_draw_box();
		} break;

		case NOTIFICATION_RESIZED: {
			_update_overlay();
		} break;

		case NOTIFICATION_TRANSFORM_CHANGED:
		case NOTIFICATION_VISIBILITY_CHANGED: {
			_update_overlay();
		} break;

		case NOTIFICATION_READY: {
			// A no-op in the editor (see the _ensure_overlay() guard); the overlay only
			// exists when the scene is actually running.
			_refresh_overlay_presence();
		} break;

		case NOTIFICATION_THEME_CHANGED: {
			queue_sort();
			update_minimum_size();
			queue_redraw();
		} break;

		case NOTIFICATION_TRANSLATION_CHANGED:
		case NOTIFICATION_LAYOUT_DIRECTION_CHANGED: {
			queue_sort();
		} break;
	}
}

void WebGridContainer::add_child_notify(Node *p_child) {
	Container::add_child_notify(p_child);
	_sync_child_aligns();
	queue_sort();
	_update_merge_availability();
}

void WebGridContainer::move_child_notify(Node *p_child) {
	Container::move_child_notify(p_child);
	queue_sort();
	_update_merge_availability();
}

void WebGridContainer::remove_child_notify(Node *p_child) {
	Container::remove_child_notify(p_child);
	_sync_child_aligns();
	queue_sort();
	_update_merge_availability();
}

//
// Property setters / getters.
//

void WebGridContainer::set_column_count(int p_count) {
	p_count = MAX(p_count, 1);
	if (column_count == p_count) {
		return;
	}
	column_count = p_count;
	_resize_tracks(column_tracks, column_count);
	_invalidate_merge_cache();
	notify_property_list_changed();
	queue_sort();
	update_minimum_size();
	emit_signal(SNAME("grid_changed"));
	_update_merge_availability();
}

int WebGridContainer::get_column_count() const {
	return column_count;
}

void WebGridContainer::set_row_count(int p_count) {
	p_count = MAX(p_count, 1);
	if (row_count == p_count) {
		return;
	}
	row_count = p_count;
	_resize_tracks(row_tracks, row_count);
	_invalidate_merge_cache();
	notify_property_list_changed();
	queue_sort();
	update_minimum_size();
	emit_signal(SNAME("grid_changed"));
	_update_merge_availability();
}

int WebGridContainer::get_row_count() const {
	return row_count;
}

void WebGridContainer::set_column_gap(float p_gap) {
	if (column_gap == p_gap) {
		return;
	}
	column_gap = p_gap;
	queue_sort();
	update_minimum_size();
	emit_signal(SNAME("grid_changed"));
}

float WebGridContainer::get_column_gap() const {
	return column_gap;
}

void WebGridContainer::set_row_gap(float p_gap) {
	if (row_gap == p_gap) {
		return;
	}
	row_gap = p_gap;
	queue_sort();
	update_minimum_size();
	emit_signal(SNAME("grid_changed"));
}

float WebGridContainer::get_row_gap() const {
	return row_gap;
}

void WebGridContainer::set_justify_items(ItemsAlign p_align) {
	if (justify_items == p_align) {
		return;
	}
	justify_items = p_align;
	queue_sort();
	emit_signal(SNAME("grid_changed"));
}

WebGridContainer::ItemsAlign WebGridContainer::get_justify_items() const {
	return justify_items;
}

void WebGridContainer::set_align_items(ItemsAlign p_align) {
	if (align_items == p_align) {
		return;
	}
	align_items = p_align;
	queue_sort();
	emit_signal(SNAME("grid_changed"));
}

WebGridContainer::ItemsAlign WebGridContainer::get_align_items() const {
	return align_items;
}

void WebGridContainer::set_justify_content(ContentAlign p_align) {
	if (justify_content == p_align) {
		return;
	}
	justify_content = p_align;
	queue_sort();
	emit_signal(SNAME("grid_changed"));
}

WebGridContainer::ContentAlign WebGridContainer::get_justify_content() const {
	return justify_content;
}

void WebGridContainer::set_align_content(ContentAlign p_align) {
	if (align_content == p_align) {
		return;
	}
	align_content = p_align;
	queue_sort();
	emit_signal(SNAME("grid_changed"));
}

WebGridContainer::ContentAlign WebGridContainer::get_align_content() const {
	return align_content;
}

float WebGridContainer::_unit_preserving_value(bool p_is_columns, int p_index, TrackUnit p_new_unit) const {
	PackedFloat32Array sizes = p_is_columns ? get_resolved_column_sizes() : get_resolved_row_sizes();
	float cur_px = (p_index >= 0 && p_index < sizes.size()) ? sizes[p_index] : 0.0f;
	const Vector<GridTrack> &tr = p_is_columns ? column_tracks : row_tracks;
	switch (p_new_unit) {
		case UNIT_PX:
			return cur_px;
		case UNIT_PERCENT: {
			float avail = get_axis_available(p_is_columns);
			return (avail > 0.01f) ? (cur_px / avail * 100.0f) : 0.0f;
		}
		case UNIT_FR: {
			// Reuse the current per-fr pixel ratio (from the other fr tracks) so this
			// track keeps its pixel size and the others stay put. With no other fr track
			// to anchor the ratio, fall back to 1fr (it absorbs the free space).
			float fr_px = 0.0f, fr_tot = 0.0f;
			for (int j = 0; j < sizes.size(); j++) {
				if (j != p_index && j < tr.size() && tr[j].unit == UNIT_FR) {
					fr_px += sizes[j];
					fr_tot += MAX(tr[j].value, 0.0f);
				}
			}
			float per_fr = (fr_tot > 0.0f && fr_px > 0.0f) ? (fr_px / fr_tot) : 0.0f;
			return (per_fr > 0.01f) ? (cur_px / per_fr) : 1.0f;
		}
		case UNIT_AUTO:
		default:
			return 0.0f; // `auto` is content-sized; it carries no stored value.
	}
}

void WebGridContainer::set_column_track_unit(int p_index, TrackUnit p_unit) {
	ERR_FAIL_INDEX(p_index, column_tracks.size());
	TrackUnit old_unit = column_tracks[p_index].unit;
	if (old_unit == p_unit) {
		return;
	}
	// Switching units keeps the track's resolved pixel size so the grid line does not
	// jump (item: recompute value on unit change).
	float keep = _unit_preserving_value(true, p_index, p_unit);
	column_tracks.write[p_index].unit = p_unit;
	column_tracks.write[p_index].value = keep;
	// Only the auto <-> non-auto transition changes the value field's read-only state,
	// which is the one thing that forces the inspector to rebuild its entire property
	// list (the source of the unit-switch lag). For px/%/fr switches skip it: the grid
	// re-lays-out immediately via queue_sort(), and the value field catches up through
	// the inspector's auto-refresh, so the change feels instant.
	if (old_unit == UNIT_AUTO || p_unit == UNIT_AUTO) {
		notify_property_list_changed();
	}
	queue_sort();
	update_minimum_size();
	emit_signal(SNAME("grid_changed"));
}

WebGridContainer::TrackUnit WebGridContainer::get_column_track_unit(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, column_tracks.size(), UNIT_AUTO);
	return column_tracks[p_index].unit;
}

void WebGridContainer::set_column_track_value(int p_index, float p_value) {
	ERR_FAIL_INDEX(p_index, column_tracks.size());
	if (column_tracks[p_index].value == p_value) {
		return;
	}
	column_tracks.write[p_index].value = p_value;
	queue_sort();
	update_minimum_size();
	emit_signal(SNAME("grid_changed"));
}

float WebGridContainer::get_column_track_value(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, column_tracks.size(), 0.0f);
	return column_tracks[p_index].value;
}

void WebGridContainer::set_row_track_unit(int p_index, TrackUnit p_unit) {
	ERR_FAIL_INDEX(p_index, row_tracks.size());
	TrackUnit old_unit = row_tracks[p_index].unit;
	if (old_unit == p_unit) {
		return;
	}
	float keep = _unit_preserving_value(false, p_index, p_unit);
	row_tracks.write[p_index].unit = p_unit;
	row_tracks.write[p_index].value = keep;
	// See set_column_track_unit: only rebuild the property list on the auto transition.
	if (old_unit == UNIT_AUTO || p_unit == UNIT_AUTO) {
		notify_property_list_changed();
	}
	queue_sort();
	update_minimum_size();
	emit_signal(SNAME("grid_changed"));
}

WebGridContainer::TrackUnit WebGridContainer::get_row_track_unit(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, row_tracks.size(), UNIT_AUTO);
	return row_tracks[p_index].unit;
}

void WebGridContainer::set_row_track_value(int p_index, float p_value) {
	ERR_FAIL_INDEX(p_index, row_tracks.size());
	if (row_tracks[p_index].value == p_value) {
		return;
	}
	row_tracks.write[p_index].value = p_value;
	queue_sort();
	update_minimum_size();
	emit_signal(SNAME("grid_changed"));
}

float WebGridContainer::get_row_track_value(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, row_tracks.size(), 0.0f);
	return row_tracks[p_index].value;
}

void WebGridContainer::set_child_justify_self(int p_index, SelfAlign p_align) {
	ERR_FAIL_INDEX(p_index, child_aligns.size());
	if (child_aligns[p_index].justify_self == p_align) {
		return;
	}
	child_aligns.write[p_index].justify_self = p_align;
	queue_sort();
	emit_signal(SNAME("grid_changed"));
}

WebGridContainer::SelfAlign WebGridContainer::get_child_justify_self(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, child_aligns.size(), SELF_AUTO);
	return child_aligns[p_index].justify_self;
}

void WebGridContainer::set_child_align_self(int p_index, SelfAlign p_align) {
	ERR_FAIL_INDEX(p_index, child_aligns.size());
	if (child_aligns[p_index].align_self == p_align) {
		return;
	}
	child_aligns.write[p_index].align_self = p_align;
	queue_sort();
	emit_signal(SNAME("grid_changed"));
}

WebGridContainer::SelfAlign WebGridContainer::get_child_align_self(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, child_aligns.size(), SELF_AUTO);
	return child_aligns[p_index].align_self;
}

//
// Dynamic properties for the variable-length track and child lists.
//

bool WebGridContainer::_set(const StringName &p_name, const Variant &p_value) {
	const String name = p_name;
	if (name.begins_with("column_tracks/") || name.begins_with("row_tracks/")) {
		bool is_col = name.begins_with("column_tracks/");
		Vector<String> parts = name.split("/");
		if (parts.size() != 3) {
			return false;
		}
		int index = parts[1].to_int();
		const String &field = parts[2];
		Vector<GridTrack> &tracks = is_col ? column_tracks : row_tracks;
		if (index < 0 || index >= tracks.size()) {
			return false;
		}
		if (field == "unit") {
			if (is_col) {
				set_column_track_unit(index, (TrackUnit)(int)p_value);
			} else {
				set_row_track_unit(index, (TrackUnit)(int)p_value);
			}
			return true;
		} else if (field == "value") {
			if (is_col) {
				set_column_track_value(index, p_value);
			} else {
				set_row_track_value(index, p_value);
			}
			return true;
		}
		return false;
	}

	if (name.begins_with("children/")) {
		Vector<String> parts = name.split("/");
		if (parts.size() != 3) {
			return false;
		}
		int index = parts[1].to_int();
		const String &field = parts[2];
		if (index < 0 || (field != "justify_self" && field != "align_self")) {
			return false;
		}
		// Auto-grow so values restored from a saved scene are kept until the child
		// nodes are (re-)added. See _sync_child_aligns().
		if (index >= child_aligns.size()) {
			int old_size = child_aligns.size();
			child_aligns.resize(index + 1);
			for (int i = old_size; i < child_aligns.size(); i++) {
				child_aligns.write[i] = ChildAlign();
			}
		}
		if (field == "justify_self") {
			set_child_justify_self(index, (SelfAlign)(int)p_value);
			return true;
		} else if (field == "align_self") {
			set_child_align_self(index, (SelfAlign)(int)p_value);
			return true;
		}
		return false;
	}

	return false;
}

bool WebGridContainer::_get(const StringName &p_name, Variant &r_ret) const {
	const String name = p_name;
	if (name.begins_with("column_tracks/") || name.begins_with("row_tracks/")) {
		bool is_col = name.begins_with("column_tracks/");
		Vector<String> parts = name.split("/");
		if (parts.size() != 3) {
			return false;
		}
		int index = parts[1].to_int();
		const String &field = parts[2];
		const Vector<GridTrack> &tracks = is_col ? column_tracks : row_tracks;
		if (index < 0 || index >= tracks.size()) {
			return false;
		}
		if (field == "unit") {
			r_ret = (int)tracks[index].unit;
			return true;
		} else if (field == "value") {
			r_ret = tracks[index].value;
			return true;
		}
		return false;
	}

	if (name.begins_with("children/")) {
		Vector<String> parts = name.split("/");
		if (parts.size() != 3) {
			return false;
		}
		int index = parts[1].to_int();
		const String &field = parts[2];
		if (index < 0 || index >= child_aligns.size()) {
			return false;
		}
		if (field == "justify_self") {
			r_ret = (int)child_aligns[index].justify_self;
			return true;
		} else if (field == "align_self") {
			r_ret = (int)child_aligns[index].align_self;
			return true;
		}
		return false;
	}

	return false;
}

void WebGridContainer::_get_property_list(List<PropertyInfo> *p_list) const {
	const String unit_hint = "Auto,Px,Percent,Fr";
	const String self_hint = "Auto,Stretch,Start,End,Center";

	p_list->push_back(PropertyInfo(Variant::NIL, "Columns", PROPERTY_HINT_NONE, "column_tracks/", PROPERTY_USAGE_GROUP));
	for (int i = 0; i < column_count; i++) {
		p_list->push_back(PropertyInfo(Variant::INT, vformat("column_tracks/%d/unit", i), PROPERTY_HINT_ENUM, unit_hint));
		PropertyInfo pi(Variant::FLOAT, vformat("column_tracks/%d/value", i), PROPERTY_HINT_RANGE, "0,10000,0.01,or_greater");
		if (i < column_tracks.size() && column_tracks[i].unit == UNIT_AUTO) {
			pi.usage |= PROPERTY_USAGE_READ_ONLY;
		}
		p_list->push_back(pi);
	}

	p_list->push_back(PropertyInfo(Variant::NIL, "Rows", PROPERTY_HINT_NONE, "row_tracks/", PROPERTY_USAGE_GROUP));
	for (int i = 0; i < row_count; i++) {
		p_list->push_back(PropertyInfo(Variant::INT, vformat("row_tracks/%d/unit", i), PROPERTY_HINT_ENUM, unit_hint));
		PropertyInfo pi(Variant::FLOAT, vformat("row_tracks/%d/value", i), PROPERTY_HINT_RANGE, "0,10000,0.01,or_greater");
		if (i < row_tracks.size() && row_tracks[i].unit == UNIT_AUTO) {
			pi.usage |= PROPERTY_USAGE_READ_ONLY;
		}
		p_list->push_back(pi);
	}

	if (child_aligns.size() > 0) {
		p_list->push_back(PropertyInfo(Variant::NIL, "Children", PROPERTY_HINT_NONE, "children/", PROPERTY_USAGE_GROUP));
		for (int i = 0; i < child_aligns.size(); i++) {
			p_list->push_back(PropertyInfo(Variant::INT, vformat("children/%d/justify_self", i), PROPERTY_HINT_ENUM, self_hint));
			p_list->push_back(PropertyInfo(Variant::INT, vformat("children/%d/align_self", i), PROPERTY_HINT_ENUM, self_hint));
		}
	}
}

bool WebGridContainer::_property_can_revert(const StringName &p_name) const {
	const String name = p_name;
	if (name.begins_with("column_tracks/") || name.begins_with("row_tracks/")) {
		return name.ends_with("/unit") || name.ends_with("/value");
	}
	if (name.begins_with("children/")) {
		return name.ends_with("/justify_self") || name.ends_with("/align_self");
	}
	return false;
}

bool WebGridContainer::_property_get_revert(const StringName &p_name, Variant &r_property) const {
	const String name = p_name;
	if (name.ends_with("/unit")) {
		r_property = (int)UNIT_AUTO;
		return true;
	}
	if (name.ends_with("/value")) {
		r_property = 0.0f;
		return true;
	}
	if (name.ends_with("/justify_self") || name.ends_with("/align_self")) {
		r_property = (int)SELF_AUTO;
		return true;
	}
	return false;
}

PackedStringArray WebGridContainer::_get_linked_undo_properties(const String &p_property, const Variant &p_new_value) const {
	PackedStringArray ret;
	// Changing a track's unit also rewrites its stored value (to preserve the resolved
	// pixel size). Declare the sibling value so undo restores it too, not just the unit.
	if ((p_property.begins_with("column_tracks/") || p_property.begins_with("row_tracks/")) && p_property.ends_with("/unit")) {
		int slash = p_property.rfind("/");
		if (slash > 0) {
			ret.push_back(p_property.substr(0, slash) + "/value");
		}
	}
	return ret;
}

void WebGridContainer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_column_count", "count"), &WebGridContainer::set_column_count);
	ClassDB::bind_method(D_METHOD("get_column_count"), &WebGridContainer::get_column_count);
	ClassDB::bind_method(D_METHOD("set_row_count", "count"), &WebGridContainer::set_row_count);
	ClassDB::bind_method(D_METHOD("get_row_count"), &WebGridContainer::get_row_count);

	ClassDB::bind_method(D_METHOD("set_column_gap", "gap"), &WebGridContainer::set_column_gap);
	ClassDB::bind_method(D_METHOD("get_column_gap"), &WebGridContainer::get_column_gap);
	ClassDB::bind_method(D_METHOD("set_row_gap", "gap"), &WebGridContainer::set_row_gap);
	ClassDB::bind_method(D_METHOD("get_row_gap"), &WebGridContainer::get_row_gap);

	ClassDB::bind_method(D_METHOD("set_justify_items", "align"), &WebGridContainer::set_justify_items);
	ClassDB::bind_method(D_METHOD("get_justify_items"), &WebGridContainer::get_justify_items);
	ClassDB::bind_method(D_METHOD("set_align_items", "align"), &WebGridContainer::set_align_items);
	ClassDB::bind_method(D_METHOD("get_align_items"), &WebGridContainer::get_align_items);
	ClassDB::bind_method(D_METHOD("set_justify_content", "align"), &WebGridContainer::set_justify_content);
	ClassDB::bind_method(D_METHOD("get_justify_content"), &WebGridContainer::get_justify_content);
	ClassDB::bind_method(D_METHOD("set_align_content", "align"), &WebGridContainer::set_align_content);
	ClassDB::bind_method(D_METHOD("get_align_content"), &WebGridContainer::get_align_content);

	ClassDB::bind_method(D_METHOD("set_column_track_unit", "index", "unit"), &WebGridContainer::set_column_track_unit);
	ClassDB::bind_method(D_METHOD("get_column_track_unit", "index"), &WebGridContainer::get_column_track_unit);
	ClassDB::bind_method(D_METHOD("set_column_track_value", "index", "value"), &WebGridContainer::set_column_track_value);
	ClassDB::bind_method(D_METHOD("get_column_track_value", "index"), &WebGridContainer::get_column_track_value);
	ClassDB::bind_method(D_METHOD("set_row_track_unit", "index", "unit"), &WebGridContainer::set_row_track_unit);
	ClassDB::bind_method(D_METHOD("get_row_track_unit", "index"), &WebGridContainer::get_row_track_unit);
	ClassDB::bind_method(D_METHOD("set_row_track_value", "index", "value"), &WebGridContainer::set_row_track_value);
	ClassDB::bind_method(D_METHOD("get_row_track_value", "index"), &WebGridContainer::get_row_track_value);

	ClassDB::bind_method(D_METHOD("set_child_justify_self", "index", "align"), &WebGridContainer::set_child_justify_self);
	ClassDB::bind_method(D_METHOD("get_child_justify_self", "index"), &WebGridContainer::get_child_justify_self);
	ClassDB::bind_method(D_METHOD("set_child_align_self", "index", "align"), &WebGridContainer::set_child_align_self);
	ClassDB::bind_method(D_METHOD("get_child_align_self", "index"), &WebGridContainer::get_child_align_self);
	ClassDB::bind_method(D_METHOD("set_draw_grid", "mode"), &WebGridContainer::set_draw_grid);
	ClassDB::bind_method(D_METHOD("get_draw_grid"), &WebGridContainer::get_draw_grid);
	ClassDB::bind_method(D_METHOD("is_grid_editable"), &WebGridContainer::is_grid_editable);
	ClassDB::bind_method(D_METHOD("set_runtime_interactive", "enabled"), &WebGridContainer::set_runtime_interactive);
	ClassDB::bind_method(D_METHOD("is_runtime_interactive"), &WebGridContainer::is_runtime_interactive);
	ClassDB::bind_method(D_METHOD("set_runtime_overlay_parent", "parent"), &WebGridContainer::set_runtime_overlay_parent);
	ClassDB::bind_method(D_METHOD("get_runtime_overlay_parent"), &WebGridContainer::get_runtime_overlay_parent);
	ClassDB::bind_method(D_METHOD("get_runtime_overlay_control"), &WebGridContainer::get_runtime_overlay_control);
	ClassDB::bind_method(D_METHOD("set_show_merge_button", "enabled"), &WebGridContainer::set_show_merge_button);
	ClassDB::bind_method(D_METHOD("is_show_merge_button"), &WebGridContainer::is_show_merge_button);

	ClassDB::bind_method(D_METHOD("get_resolved_column_offsets"), &WebGridContainer::get_resolved_column_offsets);
	ClassDB::bind_method(D_METHOD("get_resolved_column_sizes"), &WebGridContainer::get_resolved_column_sizes);
	ClassDB::bind_method(D_METHOD("get_resolved_row_offsets"), &WebGridContainer::get_resolved_row_offsets);
	ClassDB::bind_method(D_METHOD("get_resolved_row_sizes"), &WebGridContainer::get_resolved_row_sizes);
	ClassDB::bind_method(D_METHOD("get_axis_available", "is_columns"), &WebGridContainer::get_axis_available);
	ClassDB::bind_method(D_METHOD("get_effective_column_count"), &WebGridContainer::get_effective_column_count);
	ClassDB::bind_method(D_METHOD("get_effective_row_count"), &WebGridContainer::get_effective_row_count);
	ClassDB::bind_method(D_METHOD("get_child_resolved_grid_rect", "index"), &WebGridContainer::get_child_resolved_grid_rect);

	ClassDB::bind_method(D_METHOD("find_line_at", "local_position", "radius"), &WebGridContainer::find_line_at, DEFVAL(8.0));
	ClassDB::bind_method(D_METHOD("begin_line_drag", "is_columns", "boundary", "ctrl"), &WebGridContainer::begin_line_drag, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("update_line_drag", "local_position"), &WebGridContainer::update_line_drag);
	ClassDB::bind_method(D_METHOD("end_line_drag"), &WebGridContainer::end_line_drag);
	ClassDB::bind_method(D_METHOD("is_line_dragging"), &WebGridContainer::is_line_dragging);

	ClassDB::bind_method(D_METHOD("cell_at", "local_position"), &WebGridContainer::cell_at);
	ClassDB::bind_method(D_METHOD("select_cell", "cell", "shift"), &WebGridContainer::select_cell, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("begin_cell_drag", "cell"), &WebGridContainer::begin_cell_drag);
	ClassDB::bind_method(D_METHOD("update_cell_drag", "cell"), &WebGridContainer::update_cell_drag);
	ClassDB::bind_method(D_METHOD("end_cell_drag"), &WebGridContainer::end_cell_drag);
	ClassDB::bind_method(D_METHOD("clear_cell_selection"), &WebGridContainer::clear_cell_selection);
	ClassDB::bind_method(D_METHOD("has_cell_selection"), &WebGridContainer::has_cell_selection);
	ClassDB::bind_method(D_METHOD("get_selection_rect"), &WebGridContainer::get_selection_rect);
	ClassDB::bind_method(D_METHOD("can_merge_selected_cells"), &WebGridContainer::can_merge_selected_cells);
	ClassDB::bind_method(D_METHOD("can_unmerge_selected_cells"), &WebGridContainer::can_unmerge_selected_cells);
	ClassDB::bind_method(D_METHOD("merge_selected_cells"), &WebGridContainer::merge_selected_cells);
	ClassDB::bind_method(D_METHOD("unmerge_selected_cells"), &WebGridContainer::unmerge_selected_cells);
	ClassDB::bind_method(D_METHOD("set_merged_cell_rects", "rects"), &WebGridContainer::set_merged_cell_rects);
	ClassDB::bind_method(D_METHOD("get_merged_cell_rects"), &WebGridContainer::get_merged_cell_rects);
	ClassDB::bind_method(D_METHOD("get_merged_rects"), &WebGridContainer::get_merged_rects);
	ClassDB::bind_method(D_METHOD("get_grid_line_segments"), &WebGridContainer::get_grid_line_segments);
	ClassDB::bind_method(D_METHOD("get_gap_rects"), &WebGridContainer::get_gap_rects);

	ClassDB::bind_method(D_METHOD("export_css"), &WebGridContainer::export_css);
	ClassDB::bind_method(D_METHOD("import_css", "css"), &WebGridContainer::import_css);

	ClassDB::bind_method(D_METHOD("_get_linked_undo_properties", "for_property", "for_value"), &WebGridContainer::_get_linked_undo_properties);

	// Interaction toggles first, at the very top of the node's own section (no group).
	ADD_PROPERTY(PropertyInfo(Variant::INT, "draw_grid", PROPERTY_HINT_ENUM, "Selected,Always,Never"), "set_draw_grid", "get_draw_grid");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "runtime_interactive"), "set_runtime_interactive", "is_runtime_interactive");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "runtime_show_merge_button"), "set_show_merge_button", "is_show_merge_button");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "column_count", PROPERTY_HINT_RANGE, "1,64,1"), "set_column_count", "get_column_count");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "row_count", PROPERTY_HINT_RANGE, "1,64,1"), "set_row_count", "get_row_count");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "merged_cell_rects", PROPERTY_HINT_ARRAY_TYPE, "Rect2i"), "set_merged_cell_rects", "get_merged_cell_rects");
	ADD_GROUP("Gap", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "column_gap", PROPERTY_HINT_RANGE, "0,1000,0.01,or_greater"), "set_column_gap", "get_column_gap");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "row_gap", PROPERTY_HINT_RANGE, "0,1000,0.01,or_greater"), "set_row_gap", "get_row_gap");

	ADD_GROUP("Alignment", "");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "justify_items", PROPERTY_HINT_ENUM, "Stretch,Start,End,Center"), "set_justify_items", "get_justify_items");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "align_items", PROPERTY_HINT_ENUM, "Stretch,Start,End,Center"), "set_align_items", "get_align_items");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "justify_content", PROPERTY_HINT_ENUM, "Start,End,Center,Stretch,Space Between,Space Around,Space Evenly"), "set_justify_content", "get_justify_content");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "align_content", PROPERTY_HINT_ENUM, "Start,End,Center,Stretch,Space Between,Space Around,Space Evenly"), "set_align_content", "get_align_content");

	ADD_SIGNAL(MethodInfo("grid_changed"));
	ADD_SIGNAL(MethodInfo("cells_selected", PropertyInfo(Variant::RECT2I, "rect")));
	ADD_SIGNAL(MethodInfo("cells_merged", PropertyInfo(Variant::INT, "child_index"), PropertyInfo(Variant::RECT2I, "rect")));
	ADD_SIGNAL(MethodInfo("cells_unmerged", PropertyInfo(Variant::INT, "child_index")));
	ADD_SIGNAL(MethodInfo("merged_cells_changed"));
	ADD_SIGNAL(MethodInfo("merge_available_changed", PropertyInfo(Variant::BOOL, "available")));
	ADD_SIGNAL(MethodInfo("unmerge_available_changed", PropertyInfo(Variant::BOOL, "available")));

	BIND_ENUM_CONSTANT(DRAW_GRID_SELECTED);
	BIND_ENUM_CONSTANT(DRAW_GRID_ALWAYS);
	BIND_ENUM_CONSTANT(DRAW_GRID_NEVER);

	BIND_ENUM_CONSTANT(UNIT_AUTO);
	BIND_ENUM_CONSTANT(UNIT_PX);
	BIND_ENUM_CONSTANT(UNIT_PERCENT);
	BIND_ENUM_CONSTANT(UNIT_FR);

	BIND_ENUM_CONSTANT(ITEMS_STRETCH);
	BIND_ENUM_CONSTANT(ITEMS_START);
	BIND_ENUM_CONSTANT(ITEMS_END);
	BIND_ENUM_CONSTANT(ITEMS_CENTER);

	BIND_ENUM_CONSTANT(SELF_AUTO);
	BIND_ENUM_CONSTANT(SELF_STRETCH);
	BIND_ENUM_CONSTANT(SELF_START);
	BIND_ENUM_CONSTANT(SELF_END);
	BIND_ENUM_CONSTANT(SELF_CENTER);

	BIND_ENUM_CONSTANT(CONTENT_START);
	BIND_ENUM_CONSTANT(CONTENT_END);
	BIND_ENUM_CONSTANT(CONTENT_CENTER);
	BIND_ENUM_CONSTANT(CONTENT_STRETCH);
	BIND_ENUM_CONSTANT(CONTENT_SPACE_BETWEEN);
	BIND_ENUM_CONSTANT(CONTENT_SPACE_AROUND);
	BIND_ENUM_CONSTANT(CONTENT_SPACE_EVENLY);

	// Theme item: "panel" StyleBox (same name as PanelContainer) — drawn as the
	// entire visual background/border when set. Exposed in the Inspector under
	// "Theme Overrides > Styles" so users can theme it per-node or project-wide.
	BIND_THEME_ITEM_CUSTOM(Theme::DATA_TYPE_STYLEBOX, WebGridContainer, panel_style, "panel");

	// Theme constants: padding insets the content box (the area where grid tracks
	// are laid out). Equivalent to CSS padding. Exposed under "Theme Overrides >
	// Constants". The panel StyleBox is drawn over the full outer rect (including
	// the padding area), so background and border are unaffected by padding.
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebGridContainer, padding_left);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebGridContainer, padding_top);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebGridContainer, padding_right);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebGridContainer, padding_bottom);
}

WebGridContainer::WebGridContainer() {
	set_notify_transform(true);
	_resize_tracks(column_tracks, column_count);
	_resize_tracks(row_tracks, row_count);
}
