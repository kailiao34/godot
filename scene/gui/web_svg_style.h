/**************************************************************************/
/*  web_svg_style.h                                                       */
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

#include "core/math/color.h"
#include "core/math/vector2.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/variant/variant.h"

// CSS resolution for SVG documents, done in front of ThorVG.
//
// ThorVG only understands a small CSS subset: element / `.class` selectors,
// a fixed property table, legacy colors and a single `feGaussianBlur`. Rather
// than patch the vendored rasterizer, WebSVG flattens the document first: every
// stylesheet rule is cascaded onto the element it matches and written back as an
// inline `style`, with `var()`, `calc()`, `@media`, modern colors, CSS transforms
// and `filter: blur()` already evaluated. What ThorVG receives is then plain
// SVG 1.1 styling, which it renders the way a browser would.
//
// Like WebSVGDocument these are free functions over markup strings: they touch
// no scene state, so they are unit-testable and reusable by the server-side
// exporter.

struct WebSVGStyleEnv {
	// The SVG viewport. `@media (width)`, `vw`/`vh` and percentage origins resolve
	// against it, exactly as they do for an SVG used as an `<img>`.
	Size2 viewport = Size2(300, 150);
	// `prefers-color-scheme` and `light-dark()`.
	bool dark_color_scheme = false;
	// Resolves `currentColor` where it has to become a number (color-mix()).
	Color current_color = Color(0, 0, 0, 1);
	// `rem`, and the starting `em`.
	float root_font_size = 16.0f;
	// Custom properties visible at the root, as if declared on `:root`. This is how
	// design tokens reach `var(--token)` without being written into the markup.
	// Keys include the leading `--`.
	HashMap<String, String> custom_properties;
};

struct WebSVGStyleReport {
	// CSS the renderer cannot honor (selectors, properties, functions, at-rules).
	PackedStringArray unsupported;
	// Problems worth surfacing: a dangling `var()`, an unresolved `calc()`, ...
	PackedStringArray warnings;

	void add_unsupported(const String &p_what);
	void add_warning(const String &p_what);
};

class WebSVGStyle {
public:
	// Cheap substring check: false means the document has nothing this pass would
	// change, so hot paths can skip it altogether.
	static bool needs_resolution(const String &p_markup);

	// Returns the document with its CSS flattened. On markup that does not parse the
	// input is returned untouched (and a warning recorded).
	static String resolve(const String &p_markup, const WebSVGStyleEnv &p_env, WebSVGStyleReport *r_report = nullptr);

	// Normalizes any CSS color (hex8, rgb()/hsl()/hwb() in either syntax, lab/lch/
	// oklab/oklch, color(), color-mix(), light-dark(), named basics) to an sRGB
	// color. False for anything it does not understand.
	static bool parse_color(const String &p_value, Color &r_color, const WebSVGStyleEnv &p_env);
};
