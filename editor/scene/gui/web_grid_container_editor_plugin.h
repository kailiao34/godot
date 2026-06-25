/**************************************************************************/
/*  web_grid_container_editor_plugin.h                                    */
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

#include "editor/plugins/editor_plugin.h"

class WebGridContainer;
class Button;

// Canvas editor: draws the grid lines and cell selection over every WebGridContainer
// whose `draw_grid` mode asks for it (the selected one, or all of them). Grid-line
// dragging and cell selection / merging are all delegated to the node's interaction
// bridge (so the editor and runtime share one implementation, and it is testable
// headlessly). The plugin only converts viewport coordinates to the node's local space
// and wraps edits in undo/redo. Editing is refused entirely for a node whose mode is
// WebGridContainer.DRAW_GRID_NEVER, which then behaves like any other container.
class WebGridContainerEditorPlugin : public EditorPlugin {
	GDCLASS(WebGridContainerEditorPlugin, EditorPlugin);

	WebGridContainer *edited = nullptr;
	bool visible = false;

	// Canvas toolbar buttons, shown only while a WebGridContainer is being edited.
	Button *merge_button = nullptr;
	Button *unmerge_button = nullptr;

	bool dragging_line = false;
	bool dragging_cells = false;
	bool drag_is_columns = false;
	bool cursor_overridden = false;

	// Track state snapshot captured at line-drag start, for a single undo step.
	Vector<int> snap_col_units;
	Vector<float> snap_col_values;
	Vector<int> snap_row_units;
	Vector<float> snap_row_values;
	int snap_col_count = 1;
	int snap_row_count = 1;

	Transform2D _xform_of(const CanvasItem *p_node) const;
	Transform2D _xform() const;
	Point2 _to_local(const Point2 &p_viewport) const;
	// Collects every WebGridContainer in the edited scene whose `draw_grid` mode says
	// its overlay should be visible right now (always, or selected-and-selected).
	void _collect_visible_grids(Node *p_node, Vector<WebGridContainer *> *r_grids) const;
	float _line_radius_local() const;

	void _snapshot_tracks();
	void _commit_line_drag();
	void _merge_selection();
	void _unmerge_selection();
	bool _selection_has_merge() const;
	void _update_toolbar();
	void _set_cursor(Control::CursorShape p_shape);

	void _on_grid_changed();
	void _on_cells_selected(const Rect2i &p_rect);
	void _connect_node();
	void _disconnect_node();

protected:
	void _notification(int p_what);

public:
	virtual String get_plugin_name() const override { return "WebGridContainer"; }
	virtual bool handles(Object *p_object) const override;
	virtual void edit(Object *p_object) override;
	virtual void make_visible(bool p_visible) override;
	virtual bool forward_canvas_gui_input(const Ref<InputEvent> &p_event) override;
	// The overlay is drawn from the *force* hook, not forward_canvas_draw_over_viewport:
	// the latter only runs while a WebGridContainer is selected, which cannot serve
	// WebGridContainer.DRAW_GRID_ALWAYS nodes.
	virtual void forward_canvas_force_draw_over_viewport(Control *p_overlay) override;

	WebGridContainerEditorPlugin();
};
