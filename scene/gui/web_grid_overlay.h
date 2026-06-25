/**************************************************************************/
/*  web_grid_overlay.h                                                    */
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

#include "core/math/transform_2d.h"

class Control;
class WebGridContainer;

// Browser-DevTools-style overlay for a WebGridContainer: hatched cells, hatched
// gap strips, merged-cell blocks, the cell selection and the grid frame. The overlay
// is purely graphical - it never draws text over the canvas.
//
// Both the canvas editor gizmo (WebGridContainerEditorPlugin) and the node's own
// runtime interaction overlay render through this one entry point, so the two
// surfaces are pixel-identical and the look only ever has to be tuned in one place.
//
// Everything is authored in the grid's local coordinates and mapped through
// p_xform, while stroke widths, hatch spacing and labels are compensated for the
// transform's scale so they keep a constant size on screen at any canvas zoom.
namespace WebGridOverlay {

// Draws the overlay for p_grid onto p_canvas. p_xform maps the grid's local space to
// p_canvas' space (identity when the canvas is the node's own overlay). Everything is
// drawn inside the grid rect, so the overlay never spills over neighbouring nodes.
void draw(Control *p_canvas, const WebGridContainer *p_grid, const Transform2D &p_xform = Transform2D());

} // namespace WebGridOverlay
