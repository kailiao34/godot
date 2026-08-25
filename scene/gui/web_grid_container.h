/**************************************************************************/
/*  web_grid_container.h                                                  */
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

class Button;
class InputEvent;

// A Container that reproduces the CSS Grid layout model in the browser.
// Child controls are placed in a grid of `row_count` x `column_count` cells,
// filled in document order using row-major logical-cell auto-placement. Track sizes
// and alignment behave like the equivalent CSS properties.
class WebGridContainer : public Container {
	GDCLASS(WebGridContainer, Container);

public:
	// grid-template-*: the sizing function of a single track.
	enum TrackUnit {
		UNIT_AUTO, // CSS `auto`: content-based sizing. Default.
		UNIT_PX, // CSS `<length>` in pixels.
		UNIT_PERCENT, // CSS `<percentage>` of the container size on the axis.
		UNIT_FR, // CSS `<flex>`: a fraction of the remaining free space.
	};

	// justify-items / align-items: default alignment of items inside their area.
	// CSS initial value is `normal`, which for grid items resolves to `stretch`.
	enum ItemsAlign {
		ITEMS_STRETCH,
		ITEMS_START,
		ITEMS_END,
		ITEMS_CENTER,
	};

	// justify-self / align-self: per-child override. CSS initial value is
	// `auto`, which resolves to the container's justify-items / align-items.
	enum SelfAlign {
		SELF_AUTO,
		SELF_STRETCH,
		SELF_START,
		SELF_END,
		SELF_CENTER,
	};

	// justify-content / align-content: distribution of leftover space between
	// and around tracks. CSS initial value is `normal`, which for grid behaves
	// as `start`.
	enum ContentAlign {
		CONTENT_START,
		CONTENT_END,
		CONTENT_CENTER,
		CONTENT_STRETCH,
		CONTENT_SPACE_BETWEEN,
		CONTENT_SPACE_AROUND,
		CONTENT_SPACE_EVENLY,
	};

	// When the DevTools-style cell / grid-line overlay is drawn. This is an authoring
	// affordance, not part of the CSS model: it controls the overlay in the editor
	// canvas and, while `runtime_interactive` is on, in the running scene.
	enum DrawGrid {
		DRAW_GRID_SELECTED, // Only while this node is selected in the editor. Default.
		DRAW_GRID_ALWAYS, // Always, even when something else is selected.
		DRAW_GRID_NEVER, // Never, and grid editing (line drag, cell select, merge) is off.
	};

	struct GridTrack {
		TrackUnit unit = UNIT_AUTO;
		float value = 0.0f;
	};

	struct ChildAlign {
		SelfAlign justify_self = SELF_AUTO;
		SelfAlign align_self = SELF_AUTO;
	};

	// Where a child ends up after auto-placement, in 0-indexed track coordinates.
	struct GridArea {
		int col = 0;
		int row = 0;
		int col_span = 1;
		int row_span = 1;
	};

	// Godot Theme cache. "panel" draws the background/border. "padding_*" constants
	// inset the grid content area (equivalent to CSS padding). All zero by default.
	struct ThemeCache {
		Ref<StyleBox> panel_style;
		int padding_left = 0;
		int padding_top = 0;
		int padding_right = 0;
		int padding_bottom = 0;
	} theme_cache;

private:
	int column_count = 1;
	int row_count = 1;
	Vector<GridTrack> column_tracks;
	Vector<GridTrack> row_tracks;

	float column_gap = 0.0f;
	float row_gap = 0.0f;

	ItemsAlign justify_items = ITEMS_STRETCH;
	ItemsAlign align_items = ITEMS_STRETCH;
	// CSS initial value `normal` behaves as `stretch` for grid content (it grows
	// auto-sized tracks to fill leftover space), so default to CONTENT_STRETCH.
	ContentAlign justify_content = CONTENT_STRETCH;
	ContentAlign align_content = CONTENT_STRETCH;

	Vector<ChildAlign> child_aligns;

	// The sole source of grid merge topology. Children occupy these logical cells but
	// never own, create, or remove them, so topology survives every child lifecycle.
	Vector<Rect2i> merged_cell_rects;

	// Lazily rebuilt spatial index for merged cells. The boundary masks make drawing,
	// gap generation and hit-testing O(rows * columns) instead of scanning every merge
	// for every cell/boundary. The grid is capped to a small number of tracks in the
	// inspector, so dense masks are both faster and smaller than a tree structure.
	mutable bool merge_cache_dirty = true;
	mutable Vector<int> merged_cell_owner_cache;
	mutable Vector<bool> merged_vertical_interior_cache;
	mutable Vector<bool> merged_horizontal_interior_cache;
	mutable int merge_cache_columns = 0;
	mutable int merge_cache_rows = 0;

	// Availability signals are edge-triggered. Defer updates while topology mutations
	// and selection changes are being committed so scripts never see transient states.
	bool merge_operation_in_progress = false;
	bool last_merge_available = false;
	bool last_unmerge_available = false;

	// Effective track counts after auto-placement: the explicit count grows to fit
	// items whose placement (start + span, or auto-flow overflow) needs implicit
	// tracks. Implicit tracks are `auto`-sized. Updated by _resort().
	mutable int effective_column_count = 1;
	mutable int effective_row_count = 1;

	// --- Interaction state (shared by the editor plugin and runtime input) ---
	DrawGrid draw_grid = DRAW_GRID_SELECTED;
	bool runtime_interactive = false;
	bool show_merge_button = false;

	// Line drag.
	bool line_dragging = false;
	bool line_drag_is_columns = false;
	int line_drag_boundary = -1; // boundary between track i and i+1.
	bool line_drag_ctrl = false;
	Vector<int> line_drag_units; // snapshot of each track's unit at drag start.
	Vector<float> line_drag_values; // snapshot of each track's stored value at drag start.
	Vector<float> line_drag_sizes; // snapshot of each track's resolved px size at drag start.
	float line_drag_available = 0.0f;

	// Cell selection (Excel-like). Empty rect (size 0) means no selection.
	bool has_selection = false;
	Vector2i selection_anchor; // (col, row) the range is anchored to.
	Rect2i selection_rect; // position = top-left cell, size = (cols, rows).
	bool cell_dragging = false;

	// Result of resolving one axis: per-track position (offset) and size.
	struct AxisLayout {
		Vector<float> positions;
		Vector<float> sizes;
	};

	void _resize_tracks(Vector<GridTrack> &p_tracks, int p_count);
	int _get_sortable_child_count() const;
	void _sync_child_aligns();

	// Row-major logical-cell auto-placement. Fills r_areas (one entry per sortable
	// child) and reports the effective track counts, including implicit rows.
	void _place_items(Vector<GridArea> &r_areas, int &r_cols, int &r_rows) const;

	// Per-track content-based minimum on the given axis, span-aware: an item that
	// spans several tracks distributes its minimum across the spanned tracks.
	void _compute_track_content_min(bool p_is_columns, const Vector<GridArea> &p_areas, int p_count, float p_available, Vector<float> &r_content_min) const;
	// Resolves one axis. p_available is the CONTENT size (already minus border+padding);
	// p_origin is added to every track position so the grid sits inside the content box.
	AxisLayout _resolve_axis(bool p_is_columns, float p_available, const Vector<GridArea> &p_areas, int p_count, float p_origin = 0.0f) const;
	// Convenience: resolve an axis against the current content box (size minus the
	// StyleBox insets), offsetting positions by the leading inset. Used by every
	// layout/drawing/hit-test consumer so they share one coordinate space.
	AxisLayout _resolve_axis_box(bool p_is_columns, const Vector<GridArea> &p_areas, int p_count) const;

	// Content inset per side from the panel StyleBox margins (0 when no StyleBox).
	float _inset_left() const;
	float _inset_top() const;
	float _inset_right() const;
	float _inset_bottom() const;
	// Paints the panel StyleBox (NOTIFICATION_DRAW).
	void _draw_box();

	void _resort();
	Size2 _get_minimum_size() const;

	// Rect (in local container space) covering the cell range [p_col,p_row] with
	// the given span, using the current resolved layout. Used by hit-testing,
	// drawing, and the merge button placement.
	Rect2 _cell_range_rect(int p_col, int p_row, int p_col_span, int p_row_span) const;

	// Internal overlay used only at runtime to capture mouse input above the
	// child controls and to draw the grid lines / cell selection. Created lazily.
	// Hosts with a flat logical canvas may provide an external parent so the
	// overlay can participate in their scene-tree paint order instead of using a
	// canvas-global maximum z-index.
	Control *runtime_overlay_parent = nullptr;
	Control *interaction_root = nullptr;
	Control *interaction_overlay = nullptr;
	Button *merge_button = nullptr;
	void _ensure_overlay();
	void _destroy_overlay();
	// Creates or destroys the runtime overlay so it matches the current settings
	// (running scene + `runtime_interactive` + a `draw_grid` mode that shows it).
	void _refresh_overlay_presence();
	void _update_overlay();
	void _overlay_draw();
	void _overlay_gui_input(const Ref<InputEvent> &p_event);
	void _merge_button_pressed();

	// Writes a track directly (no signal / no queue_sort) for smooth interactive
	// dragging; callers emit `grid_changed` / queue_sort once when finished.
	void _set_track_silent(bool p_is_columns, int p_index, int p_unit, float p_value);
	void _apply_line_target(int p_track, float p_target_size, bool p_ctrl);
	// Boundary drag: trade size between the two tracks adjacent to p_boundary,
	// keeping their combined size constant. fr|fr trades in fr-value space (so other
	// fr tracks are untouched); a fixed|fr pair sets only the fixed side and lets the
	// fr track absorb the rest; auto tracks become px when dragged. See update_line_drag.
	void _apply_boundary_target(int p_boundary, float p_target_size_i);
	void _set_boundary_side(int p_index, float p_target_px); // helper for one side.

	void _invalidate_merge_cache();
	void _rebuild_merge_cache(int p_columns, int p_rows) const;
	void _update_merge_availability();
	bool _selection_is_inside_grid() const;
	// Grid-owned merged areas in track coordinates (pos = col,row; size = span).
	Vector<Rect2i> _merged_rects() const;
	// Grow p_rect so it fully encloses every merged area it overlaps (so a selection
	// always snaps to whole merged cells). Repeated until stable.
	Rect2i _snap_rect_to_merges(const Rect2i &p_rect) const;

	// True when boundary p_boundary (on the given axis) passes through the interior of
	// a merged cell at the perpendicular position of p_local. Such a boundary has no
	// visible grid line there, so it must not be hit-tested or dragged (item: merged
	// interior lines are fully inert).
	bool _boundary_interior_at(bool p_is_columns, int p_boundary, const Point2 &p_local, const AxisLayout &p_cols, const AxisLayout &p_rows, const Vector<Rect2i> &p_merged) const;

	// Compute the stored track value that keeps a track's resolved pixel size when its
	// unit changes (item: switching a unit must not move the grid line).
	float _unit_preserving_value(bool p_is_columns, int p_index, TrackUnit p_new_unit) const;

	// Build a CSS track value token (e.g. "200px", "1fr", "auto", "50%").
	static String _track_to_css(TrackUnit p_unit, float p_value);
	// Parse a single CSS track token into unit + value.
	static bool _css_to_track(const String &p_token, TrackUnit &r_unit, float &r_value);

protected:
	bool _set(const StringName &p_name, const Variant &p_value);
	bool _get(const StringName &p_name, Variant &r_ret) const;
	void _get_property_list(List<PropertyInfo> *p_list) const;
	bool _property_can_revert(const StringName &p_name) const;
	bool _property_get_revert(const StringName &p_name, Variant &r_property) const;
	// Tells the editor inspector which other properties an edit also rewrites, so undo
	// captures them. Switching a track's unit also recomputes its stored value, so a
	// `.../unit` edit links the sibling `.../value`.
	PackedStringArray _get_linked_undo_properties(const String &p_property, const Variant &p_new_value) const;

	virtual void add_child_notify(Node *p_child) override;
	virtual void move_child_notify(Node *p_child) override;
	virtual void remove_child_notify(Node *p_child) override;

	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_column_count(int p_count);
	int get_column_count() const;
	void set_row_count(int p_count);
	int get_row_count() const;

	void set_column_gap(float p_gap);
	float get_column_gap() const;
	void set_row_gap(float p_gap);
	float get_row_gap() const;

	void set_justify_items(ItemsAlign p_align);
	ItemsAlign get_justify_items() const;
	void set_align_items(ItemsAlign p_align);
	ItemsAlign get_align_items() const;
	void set_justify_content(ContentAlign p_align);
	ContentAlign get_justify_content() const;
	void set_align_content(ContentAlign p_align);
	ContentAlign get_align_content() const;

	void set_column_track_unit(int p_index, TrackUnit p_unit);
	TrackUnit get_column_track_unit(int p_index) const;
	void set_column_track_value(int p_index, float p_value);
	float get_column_track_value(int p_index) const;
	void set_row_track_unit(int p_index, TrackUnit p_unit);
	TrackUnit get_row_track_unit(int p_index) const;
	void set_row_track_value(int p_index, float p_value);
	float get_row_track_value(int p_index) const;

	void set_child_justify_self(int p_index, SelfAlign p_align);
	SelfAlign get_child_justify_self(int p_index) const;
	void set_child_align_self(int p_index, SelfAlign p_align);
	SelfAlign get_child_align_self(int p_index) const;

	// Overlay visibility. DRAW_GRID_NEVER also turns off every grid-editing
	// interaction, so the node behaves like any other container.
	void set_draw_grid(DrawGrid p_mode);
	DrawGrid get_draw_grid() const;
	// False only in DRAW_GRID_NEVER. The editor plugin and the runtime input handler
	// both check this before touching lines, cell selection or merging.
	bool is_grid_editable() const;

	// Runtime interactivity: when enabled, the container handles mouse input at
	// runtime to drag grid lines and select/merge cells (the same behavior the
	// editor offers at edit time).
	void set_runtime_interactive(bool p_enabled);
	bool is_runtime_interactive() const;
	void set_runtime_overlay_parent(Control *p_parent);
	Control *get_runtime_overlay_parent() const;
	Control *get_runtime_overlay_control() const;
	void set_show_merge_button(bool p_enabled);
	bool is_show_merge_button() const;

	// Resolved (laid-out) track offsets/sizes for the current container size.
	// Used by the canvas editor to draw and hit-test grid lines. Each array has
	// one entry per track on the requested axis (including implicit tracks).
	PackedFloat32Array get_resolved_column_offsets() const;
	PackedFloat32Array get_resolved_column_sizes() const;
	PackedFloat32Array get_resolved_row_offsets() const;
	PackedFloat32Array get_resolved_row_sizes() const;
	float get_axis_available(bool p_is_columns) const;
	int get_effective_column_count() const;
	int get_effective_row_count() const;
	Rect2i get_child_resolved_grid_rect(int p_index) const;

	// --- Interaction bridge (local container coordinates) ---
	// These drive grid-line dragging and cell selection. The editor plugin and
	// the runtime input handler both call these; tests call them directly to
	// exercise the interaction logic without synthesizing OS input events.

	// Hit-test a grid line near p_local. Returns { "axis": "column"|"row"|"",
	// "index": boundary }. Uses a dynamically shrunk radius so two close lines
	// never overlap their hit zones.
	Dictionary find_line_at(const Point2 &p_local, float p_radius = 8.0f) const;
	void begin_line_drag(bool p_is_columns, int p_boundary, bool p_ctrl);
	void update_line_drag(const Point2 &p_local);
	void end_line_drag();
	bool is_line_dragging() const;

	// Cell at a local point, or (-1,-1). col in x, row in y.
	Vector2i cell_at(const Point2 &p_local) const;
	void select_cell(const Vector2i &p_cell, bool p_shift);
	void begin_cell_drag(const Vector2i &p_cell);
	void update_cell_drag(const Vector2i &p_cell);
	void end_cell_drag();
	void clear_cell_selection();
	bool has_cell_selection() const;
	Rect2i get_selection_rect() const; // pos = top-left cell, size = (cols, rows).
	bool can_merge_selected_cells() const;
	bool can_unmerge_selected_cells() const;

	// Merge API: records the selected region on the grid independently of children.
	// Returns the child index, -2 for a successful childless merge, or -1 on failure.
	// A single-cell selection is always rejected. Emits `cells_merged`.
	int merge_selected_cells();

	// Unmerge API: removes every grid-owned merge overlapped by the selection without
	// changing child properties. Returns the number of logical merged regions removed.
	int unmerge_selected_cells();

	// Serialized grid merge topology.
	void set_merged_cell_rects(const Array &p_rects);
	Array get_merged_cell_rects() const;

	// Merged areas as an Array of Rect2i (pos = col,row; size = col_span,row_span).
	// Used by the editor / overlay drawing to skip interior grid lines, and to test
	// merge / unmerge headlessly.
	Array get_merged_rects() const;

	// Grid-line segments to draw, in local container coordinates. Each entry is a
	// PackedVector2Array of [start, end]. Lines are continuous across gaps (one segment
	// per run of cells) and split where they would cross a merged cell's interior, so
	// they line up exactly with the laid-out cells. Shared by the runtime overlay, the
	// canvas editor gizmo, and the headless tests.
	Array get_grid_line_segments() const;

	// Gap regions (the spacing between tracks) as an Array of Rect2 in local container
	// coordinates, like the browser DevTools grid-gap overlay. A gap strip that falls
	// inside a merged cell is omitted (the merged area reads as one block). Empty when
	// both gaps are 0. Shared by the runtime overlay and the canvas editor gizmo.
	Array get_gap_rects() const;

	// Export the current grid as a CSS string (grid-template-*, gap, and per-child
	// justify-self/align-self). Children with default alignment are omitted.
	// import_css() applies a CSS string back.
	String export_css() const;
	void import_css(const String &p_css);

	virtual Size2 get_minimum_size() const override;

	WebGridContainer();
};

VARIANT_ENUM_CAST(WebGridContainer::DrawGrid);
VARIANT_ENUM_CAST(WebGridContainer::TrackUnit);
VARIANT_ENUM_CAST(WebGridContainer::ItemsAlign);
VARIANT_ENUM_CAST(WebGridContainer::SelfAlign);
VARIANT_ENUM_CAST(WebGridContainer::ContentAlign);
