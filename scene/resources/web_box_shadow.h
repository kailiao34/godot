/**************************************************************************/
/*  web_box_shadow.h                                                      */
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

#include "core/io/resource.h"

// A single CSS `box-shadow` layer.
// Mirrors the CSS syntax: `<offset-x> <offset-y> <blur-radius> <spread-radius> <color> [inset]`.
class WebBoxShadow : public Resource {
	GDCLASS(WebBoxShadow, Resource);

	Vector2 offset;
	real_t blur_radius = 0.0;
	real_t spread = 0.0;
	Color color = Color(0, 0, 0, 1);
	bool inset = false;

protected:
	static void _bind_methods();

public:
	void set_offset(const Vector2 &p_offset);
	Vector2 get_offset() const;

	void set_blur_radius(real_t p_blur_radius);
	real_t get_blur_radius() const;

	void set_spread(real_t p_spread);
	real_t get_spread() const;

	void set_color(const Color &p_color);
	Color get_color() const;

	void set_inset(bool p_inset);
	bool is_inset() const;

	WebBoxShadow() {}
};
