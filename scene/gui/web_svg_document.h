/**************************************************************************/
/*  web_svg_document.h                                                    */
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

#include "core/math/rect2.h"
#include "core/math/vector2i.h"
#include "core/string/ustring.h"
#include "core/templates/vector.h"
#include "core/variant/variant.h"

// Everything WebSVG needs to turn an arbitrary `.svg` file into a document it can
// safely render and hand to the HTML renderer: decoding, sanitizing, making the
// document self-contained, and reading the root element's sizing attributes.
//
// These are free functions over markup strings on purpose — they touch no scene
// state, so they are unit-testable and reusable by the server-side exporter.

struct WebSVGDocumentInfo {
	// Sizing, per the CSS rules for replaced elements.
	Vector2 intrinsic_size = Vector2(300, 150);
	bool has_intrinsic_size = false; // False when we fell back to the 300x150 default.
	Rect2 view_box;
	bool has_view_box = false;
	String preserve_aspect_ratio;

	// The root `<svg …>` opening tag's attributes (minus width/height/xmlns), and
	// everything between the root tags, both verbatim.
	String root_attributes;
	String body;
	bool valid = false;

	// Cheap flags computed while scanning, so hot paths can skip work.
	bool has_current_color = false;
	bool has_animations = false;
	int element_count = 0;

	// Reporting, surfaced through WebSVG.get_document_info().
	PackedStringArray unsupported; // Features ThorVG cannot render.
	PackedStringArray removed; // Content sanitizing stripped.
	PackedStringArray warnings;
};

class WebSVGDocument {
public:
	// Refuse anything larger than this once decompressed; a runaway `.svgz` would
	// otherwise expand without bound.
	static constexpr int64_t MAX_DOCUMENT_BYTES = 8 * 1024 * 1024;
	// Guards against `<image href="a.svg">` cycles.
	static constexpr int MAX_INLINE_DEPTH = 4;

	// Bytes to markup: gunzips `.svgz`, strips a BOM, and decodes UTF-8 or UTF-16.
	static Error decode(const Vector<uint8_t> &p_bytes, String &r_markup);

	// Strips `<script>`, `<foreignObject>`, `on*` handlers, unsafe `href` schemes,
	// `<!DOCTYPE>` internal subsets and external stylesheet processing instructions.
	// Required: this markup is forwarded to the HTML renderer and served as a page.
	static String sanitize(const String &p_markup, PackedStringArray &r_removed);

	// Rewrites `<image href="local.png">` to a `data:` URI so the document renders
	// identically in ThorVG (which resolves no external paths) and in a browser.
	static String inline_resources(const String &p_markup, const String &p_base_dir, PackedStringArray &r_warnings, int p_depth = 0);

	// Collects element counts and features that will not render, without rewriting.
	static void scan(const String &p_markup, WebSVGDocumentInfo &r_info);

	// Locates the root `<svg>` and fills in attributes, body and sizing.
	// Returns false when there is no root element.
	static bool split_root(const String &p_markup, WebSVGDocumentInfo &r_info);

	// Document-order [start, end) character spans of every element, from its start
	// tag through its matching end tag. The index matches the order XMLParser
	// reports NODE_ELEMENT over the same markup, which is how the DOM builder
	// pairs an element it does not model with its verbatim source.
	static Vector<Vector2i> element_spans(const String &p_markup);

	// CSS length to pixels, honoring px/pt/pc/in/cm/mm/em/rem/ex/ch/q.
	// `r_ok` is false for empty or non-numeric values, `r_percent` for percentages.
	static float parse_length(const String &p_value, bool *r_ok = nullptr, bool *r_percent = nullptr);

	// Removes the named attributes (compared lowercased) from a raw attribute run,
	// preserving the spelling and quoting of everything it keeps.
	static String strip_attributes(const String &p_attrs, const Vector<String> &p_names);

	// True when the value is a fragment, a `data:` image, or a scheme-less local
	// reference. Used by the sanitizer and by the resource inliner.
	static bool is_safe_href(const String &p_value);
};
