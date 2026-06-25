/**************************************************************************/
/*  web_grid_overlay.cpp                                                  */
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

#include "web_grid_overlay.h"

#include "scene/gui/control.h"
#include "scene/gui/web_grid_container.h"

namespace {

//
// Palette and metrics, transcribed from the "Grid Draw Interaction" reference design.
// The alphas are a little stronger than the reference: there the overlay sits on one
// fixed dark panel, here it is composited over an arbitrary scene.
//

const Color OVERLAY_ACCENT = Color(0.718f, 0.616f, 0.941f); // #b79df0 - frame, gaps, merged cells.
const Color OVERLAY_SELECT = Color(0.310f, 0.765f, 0.851f); // #4fc3d9 - cell selection.
const Color OVERLAY_NEUTRAL = Color(0.863f, 0.882f, 0.894f); // #dce1e4 - cell hatch and borders.

const float CELL_HATCH_ALPHA = 0.05f;
const float CELL_BORDER_ALPHA = 0.22f;
const float GAP_HATCH_ALPHA = 0.34f;
const float MERGE_FILL_ALPHA = 0.14f;
const float MERGE_BORDER_ALPHA = 0.55f;
const float SELECT_FILL_ALPHA = 0.16f;
const float FRAME_ALPHA = 0.9f;

// All of these are screen pixels at 100% canvas zoom and 1x editor scale; every use
// site divides by the transform scale so the overlay never grows or shrinks with zoom.
const float CELL_HATCH_PERIOD = 16.0f; // CSS repeating-linear-gradient(135deg, x 0 8px, transparent 8px 16px).
const float CELL_HATCH_BAND = 8.0f;
const float GAP_HATCH_PERIOD = 6.0f; // CSS repeating-linear-gradient(45deg, x 0 3px, transparent 3px 6px).
const float GAP_HATCH_BAND = 3.0f;
const float CELL_DASH = 4.0f;
const float CELL_BORDER_WIDTH = 1.0f;
const float FRAME_WIDTH = 1.5f;
const float SELECT_BORDER_WIDTH = 2.0f;

// Stripe normals (unit vectors perpendicular to the stripes) for the two CSS gradient
// angles the design uses. Y grows downwards, as in CSS.
const Vector2 CELL_HATCH_NORMAL = Vector2(0.70710678f, 0.70710678f); // 135deg.
const Vector2 GAP_HATCH_NORMAL = Vector2(0.70710678f, -0.70710678f); // 45deg.

// A pathological zoom / track combination could ask for an unbounded number of stripes;
// past this many the hatch reads as a flat fill anyway, so it is simply skipped.
const int MAX_HATCH_STRIPES = 512;

// Sutherland-Hodgman clip of a convex polygon against the half-plane dot(n, p) <= d.
Vector<Point2> _clip_half_plane(const Vector<Point2> &p_poly, const Vector2 &p_normal, float p_d) {
	Vector<Point2> out;
	const int n = p_poly.size();
	for (int i = 0; i < n; i++) {
		const Point2 &a = p_poly[i];
		const Point2 &b = p_poly[(i + 1) % n];
		const float da = p_normal.dot(a) - p_d;
		const float db = p_normal.dot(b) - p_d;
		if (da <= 0.0f) {
			out.push_back(a);
		}
		if ((da < 0.0f) != (db < 0.0f)) {
			out.push_back(a.lerp(b, da / (da - db)));
		}
	}
	return out;
}

// Fills p_rect with a CSS repeating-linear-gradient style diagonal hatch: bands of
// p_band width every p_period along p_normal, each one clipped to the rect so nothing
// bleeds outside the cell (a thick diagonal line would).
void _draw_hatch(Control *p_canvas, const Rect2 &p_rect, const Vector2 &p_normal, float p_band, float p_period, const Color &p_color) {
	if (p_rect.size.x <= 0.0f || p_rect.size.y <= 0.0f || p_period <= 0.0f || p_band <= 0.0f) {
		return;
	}
	Vector<Point2> rect_poly;
	rect_poly.push_back(p_rect.position);
	rect_poly.push_back(p_rect.position + Vector2(p_rect.size.x, 0));
	rect_poly.push_back(p_rect.position + p_rect.size);
	rect_poly.push_back(p_rect.position + Vector2(0, p_rect.size.y));

	float d_min = 1e30f;
	float d_max = -1e30f;
	for (int i = 0; i < rect_poly.size(); i++) {
		const float d = p_normal.dot(rect_poly[i]);
		d_min = MIN(d_min, d);
		d_max = MAX(d_max, d);
	}
	const int first = (int)Math::floor(d_min / p_period);
	const int last = (int)Math::ceil(d_max / p_period);
	if (last - first > MAX_HATCH_STRIPES) {
		return;
	}
	for (int k = first; k <= last; k++) {
		const float d0 = k * p_period;
		Vector<Point2> stripe = _clip_half_plane(rect_poly, -p_normal, -d0);
		if (stripe.size() < 3) {
			continue;
		}
		stripe = _clip_half_plane(stripe, p_normal, d0 + p_band);
		if (stripe.size() >= 3) {
			p_canvas->draw_colored_polygon(stripe, p_color);
		}
	}
}

void _draw_dashed_rect(Control *p_canvas, const Rect2 &p_rect, const Color &p_color, float p_width, float p_dash) {
	const Point2 tl = p_rect.position;
	const Point2 tr = p_rect.position + Vector2(p_rect.size.x, 0);
	const Point2 br = p_rect.position + p_rect.size;
	const Point2 bl = p_rect.position + Vector2(0, p_rect.size.y);
	// Every edge runs left-to-right / top-to-bottom so the dashes of two adjacent cells
	// sharing an edge (gap 0) land on top of each other instead of interleaving.
	p_canvas->draw_dashed_line(tl, tr, p_color, p_width, p_dash, true, true);
	p_canvas->draw_dashed_line(bl, br, p_color, p_width, p_dash, true, true);
	p_canvas->draw_dashed_line(tl, bl, p_color, p_width, p_dash, true, true);
	p_canvas->draw_dashed_line(tr, br, p_color, p_width, p_dash, true, true);
}

} // namespace

void WebGridOverlay::draw(Control *p_canvas, const WebGridContainer *p_grid, const Transform2D &p_xform) {
	ERR_FAIL_NULL(p_canvas);
	ERR_FAIL_NULL(p_grid);

	const PackedFloat32Array col_off = p_grid->get_resolved_column_offsets();
	const PackedFloat32Array col_size = p_grid->get_resolved_column_sizes();
	const PackedFloat32Array row_off = p_grid->get_resolved_row_offsets();
	const PackedFloat32Array row_size = p_grid->get_resolved_row_sizes();
	const int cols = (int)col_off.size();
	const int rows = (int)row_off.size();
	if (cols == 0 || rows == 0 || (int)col_size.size() != cols || (int)row_size.size() != rows) {
		return;
	}

	// Everything below is authored in the grid's local space; screen_px() converts a
	// screen-pixel measurement into that space, so strokes and hatching keep a constant
	// on-screen size at any canvas zoom.
	const float scale = MAX((float)p_xform.get_scale().x, 0.0001f);
	auto screen_px = [scale](float p_pixels) { return p_pixels / scale; };

	Vector<Rect2i> merged;
	{
		const Array arr = p_grid->get_merged_rects();
		for (int i = 0; i < arr.size(); i++) {
			merged.push_back(arr[i]);
		}
	}
	// Index of the merged area covering a cell, or -1.
	auto merged_at = [&merged](int p_col, int p_row) {
		for (int i = 0; i < merged.size(); i++) {
			const Rect2i &m = merged[i];
			if (p_col >= m.position.x && p_col < m.position.x + m.size.x &&
					p_row >= m.position.y && p_row < m.position.y + m.size.y) {
				return i;
			}
		}
		return -1;
	};
	auto cell_rect = [&](int p_col, int p_row, int p_col_span, int p_row_span) {
		const int c0 = CLAMP(p_col, 0, cols - 1);
		const int r0 = CLAMP(p_row, 0, rows - 1);
		const int c1 = CLAMP(p_col + p_col_span - 1, c0, cols - 1);
		const int r1 = CLAMP(p_row + p_row_span - 1, r0, rows - 1);
		const float x = col_off[c0];
		const float y = row_off[r0];
		return Rect2(x, y, col_off[c1] + col_size[c1] - x, row_off[r1] + row_size[r1] - y);
	};

	p_canvas->draw_set_transform_matrix(p_xform);

	// 1. Cells. A plain cell gets the faint diagonal hatch and a dashed outline; a merged
	//    area is drawn once, as a single accented block, by its top-left cell.
	for (int r = 0; r < rows; r++) {
		for (int c = 0; c < cols; c++) {
			const int mi = merged_at(c, r);
			Rect2 rect;
			if (mi >= 0) {
				const Rect2i &m = merged[mi];
				if (m.position.x != c || m.position.y != r) {
					continue; // Covered by a merged area that another cell owns.
				}
				rect = cell_rect(m.position.x, m.position.y, m.size.x, m.size.y);
			} else {
				rect = cell_rect(c, r, 1, 1);
			}
			if (rect.size.x <= 0.0f || rect.size.y <= 0.0f) {
				continue;
			}
			if (mi >= 0) {
				p_canvas->draw_rect(rect, Color(OVERLAY_ACCENT, MERGE_FILL_ALPHA), true);
				p_canvas->draw_rect(rect.grow(-screen_px(CELL_BORDER_WIDTH * 0.5f)), Color(OVERLAY_ACCENT, MERGE_BORDER_ALPHA), false, screen_px(CELL_BORDER_WIDTH), true);
			} else {
				_draw_hatch(p_canvas, rect, CELL_HATCH_NORMAL, screen_px(CELL_HATCH_BAND), screen_px(CELL_HATCH_PERIOD), Color(OVERLAY_NEUTRAL, CELL_HATCH_ALPHA));
				_draw_dashed_rect(p_canvas, rect.grow(-screen_px(CELL_BORDER_WIDTH * 0.5f)), Color(OVERLAY_NEUTRAL, CELL_BORDER_ALPHA), screen_px(CELL_BORDER_WIDTH), screen_px(CELL_DASH));
			}
		}
	}

	// 2. Gap strips, hatched on the opposite diagonal so a row / column gap reads as its
	//    own kind of region. This is the part that visualises `gap`.
	const Array gaps = p_grid->get_gap_rects();
	for (int i = 0; i < gaps.size(); i++) {
		const Rect2 g = gaps[i];
		_draw_hatch(p_canvas, g, GAP_HATCH_NORMAL, screen_px(GAP_HATCH_BAND), screen_px(GAP_HATCH_PERIOD), Color(OVERLAY_ACCENT, GAP_HATCH_ALPHA));
	}

	// 3. Cell selection.
	if (p_grid->has_cell_selection()) {
		const Rect2i sel = p_grid->get_selection_rect();
		const Rect2 r = cell_rect(sel.position.x, sel.position.y, sel.size.x, sel.size.y);
		if (r.size.x > 0.0f && r.size.y > 0.0f) {
			p_canvas->draw_rect(r, Color(OVERLAY_SELECT, SELECT_FILL_ALPHA), true);
			p_canvas->draw_rect(r.grow(-screen_px(SELECT_BORDER_WIDTH * 0.5f)), OVERLAY_SELECT, false, screen_px(SELECT_BORDER_WIDTH), true);
		}
	}

	// 4. The grid's own frame, inset like the design's `box-shadow: inset 0 0 0 1.5px`.
	const Rect2 frame = Rect2(Point2(), p_grid->get_size()).grow(-screen_px(FRAME_WIDTH * 0.5f));
	if (frame.size.x > 0.0f && frame.size.y > 0.0f) {
		p_canvas->draw_rect(frame, Color(OVERLAY_ACCENT, FRAME_ALPHA), false, screen_px(FRAME_WIDTH), true);
	}

	p_canvas->draw_set_transform_matrix(Transform2D());
}
