/**************************************************************************/
/*  web_grid_container_editor_plugin.cpp                                  */
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

#include "web_grid_container_editor_plugin.h"

#include "core/input/input_event.h"
#include "core/object/callable_mp.h"
#include "core/os/keyboard.h"
#include "editor/editor_node.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/scene/canvas_item_editor_plugin.h"
#include "scene/gui/button.h"
#include "scene/gui/web_grid_container.h"
#include "scene/gui/web_grid_overlay.h"

//
// WebGridContainerEditorPlugin (canvas grid-line editing + cell selection).
//
// The WebGridContainer's own properties (counts, tracks, gaps, alignment, per-child
// placement, runtime toggles) are edited directly through the standard inspector via
// the node's exported variables; there is no custom inspector panel.
//

Transform2D WebGridContainerEditorPlugin::_xform_of(const CanvasItem *p_node) const {
	return CanvasItemEditor::get_singleton()->get_canvas_transform() * p_node->get_global_transform();
}

Transform2D WebGridContainerEditorPlugin::_xform() const {
	return _xform_of(edited);
}

void WebGridContainerEditorPlugin::_collect_visible_grids(Node *p_node, Vector<WebGridContainer *> *r_grids) const {
	if (!p_node) {
		return;
	}
	WebGridContainer *grid = Object::cast_to<WebGridContainer>(p_node);
	if (grid && grid->is_inside_tree() && grid->is_visible_in_tree()) {
		switch (grid->get_draw_grid()) {
			case WebGridContainer::DRAW_GRID_ALWAYS: {
				r_grids->push_back(grid);
			} break;
			case WebGridContainer::DRAW_GRID_SELECTED: {
				// The editor selection (not just `edited`) so every selected grid of a
				// multi-selection shows its overlay.
				if (EditorNode::get_singleton()->get_editor_selection()->is_selected(grid)) {
					r_grids->push_back(grid);
				}
			} break;
			case WebGridContainer::DRAW_GRID_NEVER:
				break;
		}
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		_collect_visible_grids(p_node->get_child(i), r_grids);
	}
}

Point2 WebGridContainerEditorPlugin::_to_local(const Point2 &p_viewport) const {
	return _xform().affine_inverse().xform(p_viewport);
}

float WebGridContainerEditorPlugin::_line_radius_local() const {
	// 8 screen pixels, expressed in the node's local space (so the hit zone stays
	// constant on screen regardless of the canvas zoom).
	float scale = _xform().get_scale().x;
	return 8.0f / MAX(scale, 0.0001f);
}

void WebGridContainerEditorPlugin::_snapshot_tracks() {
	if (!edited) {
		return;
	}
	snap_col_count = edited->get_column_count();
	snap_row_count = edited->get_row_count();
	snap_col_units.resize(snap_col_count);
	snap_col_values.resize(snap_col_count);
	snap_row_units.resize(snap_row_count);
	snap_row_values.resize(snap_row_count);
	for (int j = 0; j < snap_col_count; j++) {
		snap_col_units.write[j] = (int)edited->get_column_track_unit(j);
		snap_col_values.write[j] = edited->get_column_track_value(j);
	}
	for (int j = 0; j < snap_row_count; j++) {
		snap_row_units.write[j] = (int)edited->get_row_track_unit(j);
		snap_row_values.write[j] = edited->get_row_track_value(j);
	}
}

void WebGridContainerEditorPlugin::_commit_line_drag() {
	if (!edited) {
		return;
	}
	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	ur->create_action(TTR("Resize Grid Track"));

	// Do: restore the current (already-applied) state. Counts first so the track
	// indices are valid; values follow.
	int cc = edited->get_column_count();
	int rc = edited->get_row_count();
	ur->add_do_method(edited, "set_column_count", cc);
	ur->add_do_method(edited, "set_row_count", rc);
	for (int j = 0; j < cc; j++) {
		ur->add_do_method(edited, "set_column_track_unit", j, (int)edited->get_column_track_unit(j));
		ur->add_do_method(edited, "set_column_track_value", j, edited->get_column_track_value(j));
	}
	for (int j = 0; j < rc; j++) {
		ur->add_do_method(edited, "set_row_track_unit", j, (int)edited->get_row_track_unit(j));
		ur->add_do_method(edited, "set_row_track_value", j, edited->get_row_track_value(j));
	}

	// Undo: restore the snapshot taken before the drag began.
	ur->add_undo_method(edited, "set_column_count", snap_col_count);
	ur->add_undo_method(edited, "set_row_count", snap_row_count);
	for (int j = 0; j < snap_col_count; j++) {
		ur->add_undo_method(edited, "set_column_track_unit", j, snap_col_units[j]);
		ur->add_undo_method(edited, "set_column_track_value", j, snap_col_values[j]);
	}
	for (int j = 0; j < snap_row_count; j++) {
		ur->add_undo_method(edited, "set_row_track_unit", j, snap_row_units[j]);
		ur->add_undo_method(edited, "set_row_track_value", j, snap_row_values[j]);
	}

	ur->commit_action(false); // State already applied during the drag.
}

void WebGridContainerEditorPlugin::_merge_selection() {
	if (!edited || !edited->is_grid_editable() || !edited->has_cell_selection()) {
		return;
	}
	Array old_merged_rects = edited->get_merged_cell_rects();

	int merged = edited->merge_selected_cells();
	if (merged == -1) {
		return;
	}
	Array new_merged_rects = edited->get_merged_cell_rects();

	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	ur->create_action(TTR("Merge Grid Cells"));
	ur->add_do_method(edited, "set_merged_cell_rects", new_merged_rects);
	ur->add_undo_method(edited, "set_merged_cell_rects", old_merged_rects);
	ur->commit_action(false);
	update_overlays();
	_update_toolbar();
}

void WebGridContainerEditorPlugin::_unmerge_selection() {
	if (!edited || !edited->is_grid_editable() || !edited->has_cell_selection()) {
		return;
	}
	Array old_merged_rects = edited->get_merged_cell_rects();

	int count = edited->unmerge_selected_cells();
	if (count <= 0) {
		return;
	}
	Array new_merged_rects = edited->get_merged_cell_rects();

	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	ur->create_action(TTR("Unmerge Grid Cells"));
	ur->add_do_method(edited, "set_merged_cell_rects", new_merged_rects);
	ur->add_undo_method(edited, "set_merged_cell_rects", old_merged_rects);
	ur->commit_action(false);
	update_overlays();
	_update_toolbar();
}

void WebGridContainerEditorPlugin::_update_toolbar() {
	// DRAW_GRID_NEVER hides the merge / unmerge actions entirely: with no overlay there
	// is no way to select cells, so the buttons would never be usable.
	bool has_node = edited != nullptr && visible && edited->is_grid_editable();
	if (merge_button) {
		merge_button->set_visible(has_node);
	}
	if (unmerge_button) {
		unmerge_button->set_visible(has_node);
	}
	if (!has_node) {
		return;
	}
	if (merge_button) {
		merge_button->set_disabled(!edited->can_merge_selected_cells());
	}
	if (unmerge_button) {
		unmerge_button->set_disabled(!edited->can_unmerge_selected_cells());
	}
}

void WebGridContainerEditorPlugin::_on_cells_selected(const Rect2i &p_rect) {
	_update_toolbar();
	update_overlays();
}

void WebGridContainerEditorPlugin::_set_cursor(Control::CursorShape p_shape) {
	CanvasItemEditor::get_singleton()->set_cursor_shape_override(p_shape);
	cursor_overridden = p_shape != Control::CURSOR_ARROW;
}

void WebGridContainerEditorPlugin::_on_grid_changed() {
	// Keep the canvas overlay in sync with inspector edits immediately (no need to
	// move the mouse into the viewport for the lines to refresh).
	update_overlays();
	_update_toolbar();
}

void WebGridContainerEditorPlugin::_connect_node() {
	if (!edited) {
		return;
	}
	if (!edited->is_connected("grid_changed", callable_mp(this, &WebGridContainerEditorPlugin::_on_grid_changed))) {
		edited->connect("grid_changed", callable_mp(this, &WebGridContainerEditorPlugin::_on_grid_changed));
	}
	if (!edited->is_connected("cells_selected", callable_mp(this, &WebGridContainerEditorPlugin::_on_cells_selected))) {
		edited->connect("cells_selected", callable_mp(this, &WebGridContainerEditorPlugin::_on_cells_selected));
	}
}

void WebGridContainerEditorPlugin::_disconnect_node() {
	if (!edited) {
		return;
	}
	// Leaving this node: drop any cell selection so it does not linger when the node
	// regains focus later (item: clear selection when the grid loses focus).
	edited->clear_cell_selection();
	if (edited->is_connected("grid_changed", callable_mp(this, &WebGridContainerEditorPlugin::_on_grid_changed))) {
		edited->disconnect("grid_changed", callable_mp(this, &WebGridContainerEditorPlugin::_on_grid_changed));
	}
	if (edited->is_connected("cells_selected", callable_mp(this, &WebGridContainerEditorPlugin::_on_cells_selected))) {
		edited->disconnect("cells_selected", callable_mp(this, &WebGridContainerEditorPlugin::_on_cells_selected));
	}
}

bool WebGridContainerEditorPlugin::handles(Object *p_object) const {
	return Object::cast_to<WebGridContainer>(p_object) != nullptr;
}

void WebGridContainerEditorPlugin::edit(Object *p_object) {
	_disconnect_node();
	edited = Object::cast_to<WebGridContainer>(p_object);
	dragging_line = false;
	dragging_cells = false;
	_connect_node();
	_update_toolbar();
	// A DRAW_GRID_SELECTED overlay appears / disappears with the selection.
	update_overlays();
}

void WebGridContainerEditorPlugin::make_visible(bool p_visible) {
	visible = p_visible;
	if (!p_visible) {
		if (cursor_overridden) {
			_set_cursor(Control::CURSOR_ARROW);
		}
		_disconnect_node();
		edited = nullptr;
		dragging_line = false;
		dragging_cells = false;
	}
	_update_toolbar();
	CanvasItemEditor::get_singleton()->update_viewport();
}

bool WebGridContainerEditorPlugin::forward_canvas_gui_input(const Ref<InputEvent> &p_event) {
	if (!edited || !visible) {
		return false;
	}

	if (!edited->is_grid_editable()) {
		// DRAW_GRID_NEVER: hands off, so the node can be picked, moved and edited exactly
		// like any other container.
		if (cursor_overridden) {
			_set_cursor(Control::CURSOR_ARROW);
		}
		return false;
	}

	// Grid editing (cell selection, line dragging) only happens with the Select tool.
	// In any other tool (Move, Rotate, ...) we leave input alone so the canvas editor
	// can grab and move the WebGridContainer itself (item: clicking the grid no longer
	// always steals the click for a cell, so the node can be dragged in Move mode).
	if (CanvasItemEditor::get_singleton()->get_current_tool() != CanvasItemEditor::TOOL_SELECT) {
		if (cursor_overridden) {
			_set_cursor(Control::CURSOR_ARROW);
		}
		return false;
	}

	Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_valid() && mb->get_button_index() == MouseButton::LEFT) {
		if (mb->is_pressed()) {
			Point2 local = _to_local(mb->get_position());
			Dictionary line = edited->find_line_at(local, _line_radius_local());
			if ((int)line["index"] >= 0) {
				_snapshot_tracks();
				drag_is_columns = String(line["axis"]) == "column";
				edited->begin_line_drag(drag_is_columns, (int)line["index"], mb->is_ctrl_pressed());
				dragging_line = true;
				update_overlays();
				return true;
			}
			// Clicking inside the container selects cells (never a child).
			Size2 sz = edited->get_size();
			if (local.x >= 0 && local.y >= 0 && local.x <= sz.width && local.y <= sz.height) {
				Vector2i cell = edited->cell_at(local);
				if (mb->is_shift_pressed()) {
					edited->select_cell(cell, true);
				} else {
					edited->begin_cell_drag(cell);
				}
				dragging_cells = true;
				update_overlays();
				return true;
			}
			return false;
		} else {
			if (dragging_line) {
				edited->end_line_drag();
				_commit_line_drag();
				dragging_line = false;
				update_overlays();
				return true;
			}
			if (dragging_cells) {
				edited->end_cell_drag();
				dragging_cells = false;
				update_overlays();
				return true;
			}
		}
	}

	Ref<InputEventMouseMotion> mm = p_event;
	if (mm.is_valid()) {
		Point2 local = _to_local(mm->get_position());
		if (dragging_line) {
			edited->update_line_drag(local);
			_set_cursor(drag_is_columns ? Control::CURSOR_HSIZE : Control::CURSOR_VSIZE);
			update_overlays();
			return true;
		}
		if (dragging_cells) {
			edited->update_cell_drag(edited->cell_at(local));
			update_overlays();
			return true;
		}
		Dictionary line = edited->find_line_at(local, _line_radius_local());
		if ((int)line["index"] >= 0) {
			_set_cursor(String(line["axis"]) == "column" ? Control::CURSOR_HSIZE : Control::CURSOR_VSIZE);
			return true;
		} else if (cursor_overridden) {
			_set_cursor(Control::CURSOR_ARROW);
		}
	}

	Ref<InputEventKey> k = p_event;
	if (k.is_valid() && k->is_pressed() && !k->is_echo()) {
		Key kc = k->get_keycode();
		if (kc == Key::ENTER || kc == Key::KP_ENTER) {
			if (edited->has_cell_selection()) {
				_merge_selection();
				update_overlays();
				return true;
			}
		} else if (kc == Key::ESCAPE) {
			if (edited->has_cell_selection()) {
				edited->clear_cell_selection();
				update_overlays();
				return true;
			}
		}
	}

	return false;
}

void WebGridContainerEditorPlugin::forward_canvas_force_draw_over_viewport(Control *p_overlay) {
	// This hook runs on every canvas repaint, whatever is selected, which is what lets a
	// DRAW_GRID_ALWAYS grid keep its overlay while another node is being edited.
	Vector<WebGridContainer *> grids;
	_collect_visible_grids(EditorNode::get_singleton()->get_edited_scene(), &grids);

	// The whole DevTools-style overlay (hatched cells and gaps, merged blocks, the cell
	// selection and the frame) is rendered by the shared drawer, which the node's runtime
	// overlay also uses, so the canvas gizmo and the running scene are identical.
	for (int i = 0; i < grids.size(); i++) {
		WebGridOverlay::draw(p_overlay, grids[i], _xform_of(grids[i]));
	}
}

void WebGridContainerEditorPlugin::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			// Ask to be called on every canvas repaint, not just while a WebGridContainer
			// is selected, so a grid set to DRAW_GRID_ALWAYS keeps its overlay while
			// another node is being edited. Done here rather than in the constructor
			// because the plugin lists only exist once the editor is up.
			set_force_draw_over_forwarding_enabled();
		} break;
	}
}

WebGridContainerEditorPlugin::WebGridContainerEditorPlugin() {
	// Canvas toolbar buttons. Hidden until a WebGridContainer is selected.
	merge_button = memnew(Button);
	merge_button->set_text(TTR("Merge Cells"));
	merge_button->set_tooltip_text(TTR("Merge the selected cells into one (Enter)."));
	merge_button->set_flat(false);
	// Prominent accent so the action stands out in the toolbar; greys out when disabled.
	merge_button->add_theme_color_override("font_color", Color(0.32, 0.92, 0.48));
	merge_button->add_theme_color_override("font_hover_color", Color(0.45, 1.0, 0.6));
	merge_button->hide();
	merge_button->set_disabled(true);
	merge_button->connect("pressed", callable_mp(this, &WebGridContainerEditorPlugin::_merge_selection));
	add_control_to_container(CONTAINER_CANVAS_EDITOR_MENU, merge_button);

	unmerge_button = memnew(Button);
	unmerge_button->set_text(TTR("Unmerge"));
	unmerge_button->set_tooltip_text(TTR("Split the selected merged cell back into single cells."));
	unmerge_button->set_flat(false);
	unmerge_button->hide();
	unmerge_button->set_disabled(true);
	unmerge_button->connect("pressed", callable_mp(this, &WebGridContainerEditorPlugin::_unmerge_selection));
	add_control_to_container(CONTAINER_CANVAS_EDITOR_MENU, unmerge_button);
}
