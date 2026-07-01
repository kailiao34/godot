/**************************************************************************/
/*  web_svg.h                                                             */
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

#include "scene/gui/control.h"
#include "scene/resources/style_box_css.h"

class ImageTexture;

// A Control that renders a scriptable SVG document inside a CSS-faithful box.
// Mimics the HTML `<svg>` element: the node's rect size is the SVG width/height,
// content is drawn at 1:1 and clipped (never scaled) when the node resizes, and
// the CSS box model (border, background, box-shadow, padding, box-sizing) comes
// from the `style`, `padding_*` and `box_sizing` theme items.
class WebSVG : public Control {
	GDCLASS(WebSVG, Control);

	struct ThemeCache {
		Ref<StyleBox> style;
		int box_sizing = 0; // 0 = content-box, 1 = border-box.
		int padding_left = 0;
		int padding_top = 0;
		int padding_right = 0;
		int padding_bottom = 0;
	} theme_cache;

	// Inline `<svg style="...">` box CSS parsed from markup, applied as a style override.
	Ref<StyleBoxCSS> inline_style;

	// Raw inner SVG markup captured by parse_svg() and rendered verbatim (so
	// gradients, <defs>, filters, etc. survive). When the typed child DOM is
	// edited, rendering switches to the serialized DOM instead.
	String raw_svg_markup;
	String svg_body;
	// Attributes from the source `<svg …>` opening tag (viewBox plus inherited
	// presentation attrs like fill/stroke), minus width/height/xmlns which the
	// rebuilt document sets itself. Preserving these keeps scaling and default
	// paint identical to the browser.
	String svg_open_attrs;
	bool dom_authoritative = false;
	bool building_dom = false;

	// Resolves CSS `currentColor` in the markup, matching the element's
	// inherited `color` in the browser.
	Color current_color = Color(0, 0, 0, 1);

	// Content rasterization cache.
	String last_rendered_doc;
	Rect2 last_rendered_content;
	Ref<ImageTexture> content_texture;
	RID clipped_content_ci;
	bool content_dirty = true;

	// SMIL animation playback. When the DOM contains WebSVGAnimation elements
	// and playback is enabled, the document is re-serialized with the current
	// animated values every frame (re-rasterizing only when it changed).
	bool animations_enabled = true;
	double animation_time = 0.0;
	bool animation_active = false;

	static bool _has_animation_elements(const Node *p_parent);
	void _update_animation_state();
	void _apply_animation_frame(Node *p_parent);

	Ref<StyleBoxCSS> _get_css_style() const;
	StyleBoxCSS::BoxSizing _resolved_box_sizing() const;
	real_t _border_margin(Side p_side) const;
	float _padding(Side p_side) const;

	String _document_body() const;
	String _build_svg_document(const Size2 &p_viewport) const;
	void _rebuild_texture_if_needed();
	void _capture_svg_body(const String &p_markup);
	void _draw_text_elements(RID p_canvas_item, const Node *p_parent, const Point2 &p_offset) const;

	void _clear_elements();
	static bool _parse_css_color(const String &p_value, Color &r_color);
	static float _parse_length(const String &p_value);
	void _apply_presentation_prop(class WebSVGElement *p_element, const String &p_key, const String &p_value) const;
	void _apply_presentation(class WebSVGElement *p_element, const String &p_style) const;
	void _apply_box_css(const Ref<StyleBoxCSS> &p_sb, const String &p_style);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	virtual Size2 get_minimum_size() const override;

	// Parses a CSS color value ("#abc", "rgb(...)", named). Returns false for
	// `none`/`transparent`/`url(...)`. Shared with WebSVGAnimation interpolation.
	static bool parse_css_color_value(const String &p_value, Color &r_color);

	// SVG markup import/export.
	Error parse_svg(const String &p_markup);
	String to_svg_string() const;
	void set_svg_markup(const String &p_markup);
	String get_svg_markup() const;

	// Called by child WebSVGElement nodes when their content changes.
	void notify_dom_changed();
	void mark_content_dirty();

	// CSS `currentColor` resolution (the inherited text color).
	void set_current_color(const Color &p_color);
	Color get_current_color() const;

	// SMIL animation playback.
	void set_animations_enabled(bool p_enabled);
	bool are_animations_enabled() const;
	void set_animation_time(double p_seconds);
	double get_animation_time() const;

	// Resolved box geometry, used by the headless computed-style bridge and tests.
	Rect2 get_border_box() const;
	Rect2 get_padding_box() const;
	Rect2 get_content_box() const;
	Dictionary get_computed_style() const;

	WebSVG();
	~WebSVG();
};
