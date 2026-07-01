/**************************************************************************/
/*  web_svg.cpp                                                          */
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

#include "web_svg.h"

#include "core/io/image.h"
#include "core/io/xml_parser.h"
#include "core/object/class_db.h"
#include "scene/gui/web_svg_element.h"
#include "scene/resources/font.h"
#include "scene/resources/image_texture.h"
#include "scene/theme/theme_db.h"
#include "servers/rendering/rendering_server.h"

Ref<StyleBoxCSS> WebSVG::_get_css_style() const {
	return theme_cache.style;
}

StyleBoxCSS::BoxSizing WebSVG::_resolved_box_sizing() const {
	return theme_cache.box_sizing == StyleBoxCSS::BOX_SIZING_BORDER_BOX ? StyleBoxCSS::BOX_SIZING_BORDER_BOX : StyleBoxCSS::BOX_SIZING_CONTENT_BOX;
}

real_t WebSVG::_border_margin(Side p_side) const {
	Ref<StyleBoxCSS> css = _get_css_style();
	return css.is_valid() ? css->get_effective_border_width(p_side) : 0.0;
}

float WebSVG::_padding(Side p_side) const {
	Ref<StyleBoxCSS> css = _get_css_style();
	if (css.is_valid()) {
		return css->get_padding(p_side);
	}
	switch (p_side) {
		case SIDE_LEFT:
			return theme_cache.padding_left;
		case SIDE_TOP:
			return theme_cache.padding_top;
		case SIDE_RIGHT:
			return theme_cache.padding_right;
		default:
			return theme_cache.padding_bottom;
	}
}

// --- Geometry. The node's rect (get_size) is the SVG width/height; box-sizing
// decides whether border + padding grow outward (content-box) or eat inward
// (border-box), exactly like an HTML <svg>. ---

Size2 WebSVG::get_minimum_size() const {
	const real_t mw = _border_margin(SIDE_LEFT) + _padding(SIDE_LEFT) + _border_margin(SIDE_RIGHT) + _padding(SIDE_RIGHT);
	const real_t mh = _border_margin(SIDE_TOP) + _padding(SIDE_TOP) + _border_margin(SIDE_BOTTOM) + _padding(SIDE_BOTTOM);
	return Size2(mw, mh);
}

Rect2 WebSVG::get_border_box() const {
	const Size2 sz = get_size();
	if (_resolved_box_sizing() == StyleBoxCSS::BOX_SIZING_BORDER_BOX) {
		return Rect2(Point2(), sz);
	}
	// content-box: the node size is the content; border + padding grow outward.
	const real_t mw = _border_margin(SIDE_LEFT) + _padding(SIDE_LEFT) + _border_margin(SIDE_RIGHT) + _padding(SIDE_RIGHT);
	const real_t mh = _border_margin(SIDE_TOP) + _padding(SIDE_TOP) + _border_margin(SIDE_BOTTOM) + _padding(SIDE_BOTTOM);
	return Rect2(Point2(), sz + Size2(mw, mh));
}

Rect2 WebSVG::get_padding_box() const {
	const Rect2 bb = get_border_box();
	return bb.grow_individual(-_border_margin(SIDE_LEFT), -_border_margin(SIDE_TOP), -_border_margin(SIDE_RIGHT), -_border_margin(SIDE_BOTTOM));
}

Rect2 WebSVG::get_content_box() const {
	const Rect2 pb = get_padding_box();
	Rect2 cb = pb.grow_individual(-_padding(SIDE_LEFT), -_padding(SIDE_TOP), -_padding(SIDE_RIGHT), -_padding(SIDE_BOTTOM));
	cb.size = cb.size.maxf(0.0);
	return cb;
}

// --- Content rendering: rasterize at the content-box size (1:1) and draw it
// without scaling, so resizing the node clips rather than stretches. ---

String WebSVG::_document_body() const {
	// Animation playback always serializes the typed DOM: the raw body is a
	// static snapshot and cannot reflect per-frame animated values.
	if (!svg_body.is_empty() && !dom_authoritative && !animation_active) {
		return svg_body;
	}
	return WebSVGElement::serialize_children(this);
}

String WebSVG::_build_svg_document(const Size2 &p_viewport) const {
	// Reuse the source viewBox and inherited presentation attributes so the
	// document scales and paints like the browser; width/height are overridden
	// to the node's box so the viewBox maps onto it.
	String svg = "<svg xmlns=\"http://www.w3.org/2000/svg\"";
	if (!svg_open_attrs.is_empty()) {
		svg += " " + svg_open_attrs;
	}
	svg += " width=\"" + rtos(p_viewport.x) + "\" height=\"" + rtos(p_viewport.y) + "\">";
	svg += _document_body();
	svg += "</svg>";
	// CSS `currentColor` resolves to the inherited text color.
	if (svg.find("currentColor") != -1) {
		svg = svg.replace("currentColor", "#" + current_color.to_html(false));
	}
	return svg;
}

void WebSVG::_rebuild_texture_if_needed() {
	const Rect2 cb = get_content_box();
	const Size2 vp = cb.size;
	const String doc = _build_svg_document(vp);
	if (!content_dirty && doc == last_rendered_doc && vp == last_rendered_content.size) {
		return;
	}
	content_dirty = false;
	last_rendered_doc = doc;
	last_rendered_content = cb;

	if (vp.x < 1.0 || vp.y < 1.0) {
		content_texture.unref();
		return;
	}

	Ref<Image> img;
	img.instantiate();
	const Error err = img->load_svg_from_string(doc, 1.0);
	if (err == OK && !img->is_empty()) {
		content_texture = ImageTexture::create_from_image(img);
	} else {
		content_texture.unref();
	}
}

void WebSVG::notify_dom_changed() {
	if (building_dom) {
		return;
	}
	dom_authoritative = true;
	content_dirty = true;
	_update_animation_state();
	queue_redraw();
}

void WebSVG::mark_content_dirty() {
	content_dirty = true;
	queue_redraw();
}

// --- SMIL animation playback ---

bool WebSVG::_has_animation_elements(const Node *p_parent) {
	for (int i = 0; i < p_parent->get_child_count(); i++) {
		const Node *child = p_parent->get_child(i);
		if (Object::cast_to<WebSVGAnimation>(child)) {
			return true;
		}
		if (Object::cast_to<WebSVGElement>(child) && _has_animation_elements(child)) {
			return true;
		}
	}
	return false;
}

void WebSVG::_update_animation_state() {
	const bool active = animations_enabled && is_inside_tree() && _has_animation_elements(this);
	if (active == animation_active) {
		return;
	}
	animation_active = active;
	set_process_internal(active);
	if (!active) {
		// Drop stale overrides so the static document serializes base values.
		_apply_animation_frame(this);
		mark_content_dirty();
	}
}

void WebSVG::_apply_animation_frame(Node *p_parent) {
	for (int i = 0; i < p_parent->get_child_count(); i++) {
		Node *child = p_parent->get_child(i);
		WebSVGElement *element = Object::cast_to<WebSVGElement>(child);
		if (!element) {
			continue;
		}
		element->clear_animation_overrides();
		if (animation_active) {
			for (int j = 0; j < element->get_child_count(); j++) {
				WebSVGAnimation *anim = Object::cast_to<WebSVGAnimation>(element->get_child(j));
				if (!anim) {
					continue;
				}
				String value;
				if (anim->evaluate(animation_time, value)) {
					element->set_animation_override(anim->target_attribute(), value);
				}
			}
		}
		_apply_animation_frame(child);
	}
}

void WebSVG::set_current_color(const Color &p_color) {
	if (current_color == p_color) {
		return;
	}
	current_color = p_color;
	mark_content_dirty();
	queue_redraw();
}

Color WebSVG::get_current_color() const {
	return current_color;
}

void WebSVG::set_animations_enabled(bool p_enabled) {
	if (animations_enabled == p_enabled) {
		return;
	}
	animations_enabled = p_enabled;
	animation_time = 0.0;
	_update_animation_state();
	queue_redraw();
}

bool WebSVG::are_animations_enabled() const {
	return animations_enabled;
}

void WebSVG::set_animation_time(double p_seconds) {
	animation_time = MAX(0.0, p_seconds);
	if (animation_active) {
		_apply_animation_frame(this);
	}
	queue_redraw();
}

double WebSVG::get_animation_time() const {
	return animation_time;
}

void WebSVG::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_DRAW: {
			if (theme_cache.style.is_valid()) {
				theme_cache.style->draw(get_canvas_item(), get_border_box());
			}
			_rebuild_texture_if_needed();
			RenderingServer *rs = RenderingServer::get_singleton();
			const Rect2 cb = get_content_box();
			rs->canvas_item_clear(clipped_content_ci);
			rs->canvas_item_set_transform(clipped_content_ci, Transform2D(0.0, cb.position));
			rs->canvas_item_set_custom_rect(clipped_content_ci, !is_visibility_clip_disabled(), Rect2(Point2(), cb.size));
			rs->canvas_item_set_clip(clipped_content_ci, true);
			rs->canvas_item_set_visibility_layer(clipped_content_ci, get_visibility_layer());
			rs->canvas_item_set_default_texture_filter(clipped_content_ci, RSE::CanvasItemTextureFilter(get_texture_filter_in_tree()));
			if (content_texture.is_valid()) {
				// Texture is rasterized at the content-box size, so this draws 1:1.
				content_texture->draw_rect(clipped_content_ci, Rect2(Point2(), content_texture->get_size()), false);
			}
			_draw_text_elements(clipped_content_ci, this, Point2());
		} break;

		case NOTIFICATION_RESIZED: {
			mark_content_dirty();
		} break;

		case NOTIFICATION_THEME_CHANGED: {
			update_minimum_size();
			mark_content_dirty();
		} break;

		case NOTIFICATION_CHILD_ORDER_CHANGED: {
			if (raw_svg_markup.is_empty()) {
				notify_dom_changed();
			} else {
				_update_animation_state();
			}
		} break;

		case NOTIFICATION_ENTER_TREE: {
			_update_animation_state();
		} break;

		case NOTIFICATION_EXIT_TREE: {
			_update_animation_state();
		} break;

		case NOTIFICATION_INTERNAL_PROCESS: {
			animation_time += get_process_delta_time();
			_apply_animation_frame(this);
			// The document is compared against the last rasterized one at draw
			// time, so unchanged frames (e.g. discrete steps) skip re-rasterizing.
			queue_redraw();
		} break;
	}
}

// --- SVG markup import/export ---

String WebSVG::to_svg_string() const {
	return _build_svg_document(get_content_box().size);
}

void WebSVG::set_svg_markup(const String &p_markup) {
	parse_svg(p_markup);
}

String WebSVG::get_svg_markup() const {
	if (!raw_svg_markup.is_empty() && !dom_authoritative) {
		return raw_svg_markup;
	}
	return to_svg_string();
}

void WebSVG::_clear_elements() {
	Vector<Node *> to_remove;
	for (int i = 0; i < get_child_count(); i++) {
		if (Object::cast_to<WebSVGElement>(get_child(i))) {
			to_remove.push_back(get_child(i));
		}
	}
	for (Node *n : to_remove) {
		remove_child(n);
		memdelete(n);
	}
}

// Rebuilds an attribute string, dropping width/height/xmlns (the rebuilt
// document supplies its own) while preserving every other attribute verbatim,
// including case-sensitive keys like `viewBox` and quoted values with spaces.
static String _strip_reserved_attrs(const String &p_attrs) {
	String out;
	int i = 0;
	const int n = p_attrs.length();
	while (i < n) {
		while (i < n && p_attrs[i] <= ' ') {
			i++;
		}
		if (i >= n) {
			break;
		}
		const int start = i;
		while (i < n && p_attrs[i] > ' ' && p_attrs[i] != '=') {
			i++;
		}
		const String key = p_attrs.substr(start, i - start);
		const int after_key = i;
		while (i < n && p_attrs[i] <= ' ') {
			i++;
		}
		if (i < n && p_attrs[i] == '=') {
			i++;
			while (i < n && p_attrs[i] <= ' ') {
				i++;
			}
			if (i < n && (p_attrs[i] == '"' || p_attrs[i] == '\'')) {
				const char32_t q = p_attrs[i++];
				while (i < n && p_attrs[i] != q) {
					i++;
				}
				if (i < n) {
					i++;
				}
			} else {
				while (i < n && p_attrs[i] > ' ') {
					i++;
				}
			}
		} else {
			i = after_key; // Boolean attribute (no value).
		}
		const String lk = key.to_lower();
		if (lk == "width" || lk == "height" || lk == "xmlns") {
			continue;
		}
		if (!out.is_empty()) {
			out += " ";
		}
		out += p_attrs.substr(start, i - start);
	}
	return out;
}

void WebSVG::_capture_svg_body(const String &p_markup) {
	svg_body = String();
	svg_open_attrs = String();
	const int lt = p_markup.findn("<svg");
	if (lt == -1) {
		return;
	}
	// Find the '>' that ends the opening <svg ...> tag, ignoring quoted attrs.
	int open_end = -1;
	bool in_quote = false;
	char32_t quote = 0;
	for (int i = lt; i < p_markup.length(); i++) {
		const char32_t ch = p_markup[i];
		if (in_quote) {
			if (ch == quote) {
				in_quote = false;
			}
		} else if (ch == '"' || ch == '\'') {
			in_quote = true;
			quote = ch;
		} else if (ch == '>') {
			open_end = i;
			break;
		}
	}
	if (open_end == -1 || p_markup[open_end - 1] == '/') {
		return; // Self-closing or malformed.
	}
	// Attributes between "<svg" and the closing ">".
	const int attrs_start = lt + 4;
	svg_open_attrs = _strip_reserved_attrs(p_markup.substr(attrs_start, open_end - attrs_start));
	const int close = p_markup.rfindn("</svg>");
	if (close == -1 || close <= open_end) {
		return;
	}
	svg_body = p_markup.substr(open_end + 1, close - open_end - 1);
}

void WebSVG::_draw_text_elements(RID p_canvas_item, const Node *p_parent, const Point2 &p_offset) const {
	Ref<Font> font = get_theme_default_font();
	if (font.is_null()) {
		return;
	}

	for (int i = 0; i < p_parent->get_child_count(); i++) {
		const Node *child = p_parent->get_child(i);
		const WebSVGText *text = Object::cast_to<WebSVGText>(child);
		if (text) {
			if (text->get_text().is_empty() || text->is_fill_none()) {
				continue;
			}

			Color fill = text->get_fill();
			fill.a *= text->get_fill_opacity() * text->get_opacity();
			if (fill.a <= 0.0) {
				continue;
			}

			const int font_size = MAX(1, (int)Math::round(text->get_font_size()));
			const Point2 pos = p_offset + text->get_text_position();
			if (text->is_stroke_enabled() && text->get_stroke_width() > 0.0) {
				Color stroke = text->get_stroke();
				stroke.a *= text->get_stroke_opacity() * text->get_opacity();
				if (stroke.a > 0.0) {
					font->draw_string_outline(p_canvas_item, pos, text->get_text(), HORIZONTAL_ALIGNMENT_LEFT, -1.0, font_size, MAX(1, (int)Math::round(text->get_stroke_width())), stroke);
				}
			}
			font->draw_string(p_canvas_item, pos, text->get_text(), HORIZONTAL_ALIGNMENT_LEFT, -1.0, font_size, fill);
		}

		if (Object::cast_to<WebSVGGroup>(child)) {
			_draw_text_elements(p_canvas_item, child, p_offset);
		}
	}
}

float WebSVG::_parse_length(const String &p_value) {
	String v = p_value.strip_edges();
	const String suffixes[] = { "px", "pt", "%", "em", "rem" };
	for (const String &suf : suffixes) {
		if (v.ends_with(suf)) {
			v = v.substr(0, v.length() - suf.length());
			break;
		}
	}
	return v.to_float();
}

bool WebSVG::parse_css_color_value(const String &p_value, Color &r_color) {
	return _parse_css_color(p_value, r_color);
}

// Parses a SMIL clock value ("2s", "750ms", "1.5") to seconds.
static float parse_clock_seconds(const String &p_value) {
	String v = p_value.strip_edges().to_lower();
	if (v.is_empty()) {
		return 0.0f;
	}
	if (v.ends_with("ms")) {
		return v.substr(0, v.length() - 2).to_float() / 1000.0f;
	}
	if (v.ends_with("s")) {
		v = v.substr(0, v.length() - 1);
	}
	return v.to_float();
}

bool WebSVG::_parse_css_color(const String &p_value, Color &r_color) {
	String v = p_value.strip_edges();
	if (v.to_lower() == "none" || v.to_lower() == "transparent") {
		return false;
	}
	if (v.to_lower().begins_with("url(")) {
		return false;
	}
	if (v.to_lower().begins_with("rgb")) {
		const int l = v.find_char('(');
		const int r = v.find_char(')');
		if (l != -1 && r > l) {
			const String inner = v.substr(l + 1, r - l - 1);
			Vector<String> parts = inner.split(",", false);
			if (parts.size() >= 3) {
				const float rr = parts[0].strip_edges().to_float() / 255.0;
				const float gg = parts[1].strip_edges().to_float() / 255.0;
				const float bb = parts[2].strip_edges().to_float() / 255.0;
				const float aa = parts.size() >= 4 ? parts[3].strip_edges().to_float() : 1.0;
				r_color = Color(rr, gg, bb, aa);
				return true;
			}
		}
		return true;
	}
	// CSS redefined the 16 basic VGA color names away from their X11 values that
	// Godot's named-color table uses. Override those to match the browser; the
	// extended palette matches X11 so it falls through to Color::named().
	const String lname = v.to_lower();
	struct CSSColor {
		const char *name;
		uint32_t rgb;
	};
	static const CSSColor css_basics[] = {
		{ "black", 0x000000 }, { "silver", 0xC0C0C0 }, { "gray", 0x808080 }, { "grey", 0x808080 },
		{ "white", 0xFFFFFF }, { "maroon", 0x800000 }, { "red", 0xFF0000 }, { "purple", 0x800080 },
		{ "fuchsia", 0xFF00FF }, { "green", 0x008000 }, { "lime", 0x00FF00 }, { "olive", 0x808000 },
		{ "yellow", 0xFFFF00 }, { "navy", 0x000080 }, { "blue", 0x0000FF }, { "teal", 0x008080 },
		{ "aqua", 0x00FFFF }
	};
	for (const CSSColor &cc : css_basics) {
		if (lname == cc.name) {
			r_color = Color::hex((cc.rgb << 8) | 0xFF);
			return true;
		}
	}

	if (Color::html_is_valid(v)) {
		r_color = Color::html(v);
	} else {
		r_color = Color::named(v, Color(0, 0, 0, 1));
	}
	return true;
}

void WebSVG::_apply_presentation_prop(WebSVGElement *p_element, const String &p_key, const String &p_value) const {
	const String key = p_key.strip_edges().to_lower();
	const String val = p_value.strip_edges();
	if (key == "fill") {
		if (val.to_lower().begins_with("url(")) {
			p_element->set_extra_style(p_element->get_extra_style() + key + ":" + val + ";");
			return;
		}
		Color c;
		if (_parse_css_color(val, c)) {
			p_element->set_fill(c);
		} else {
			p_element->set_fill_none(true);
		}
	} else if (key == "fill-opacity") {
		p_element->set_fill_opacity(val.to_float());
	} else if (key == "stroke") {
		if (val.to_lower().begins_with("url(")) {
			p_element->set_stroke_enabled(true);
			p_element->set_extra_style(p_element->get_extra_style() + key + ":" + val + ";");
			return;
		}
		Color c;
		if (_parse_css_color(val, c)) {
			p_element->set_stroke(c);
		} else {
			p_element->set_stroke_enabled(false);
		}
	} else if (key == "stroke-width") {
		p_element->set_stroke_width(_parse_length(val));
	} else if (key == "stroke-opacity") {
		p_element->set_stroke_opacity(val.to_float());
	} else if (key == "opacity") {
		p_element->set_opacity(val.to_float());
	} else if (key == "transform") {
		p_element->set_transform_string(val);
	} else {
		p_element->set_extra_style(p_element->get_extra_style() + key + ":" + val + ";");
	}
}

void WebSVG::_apply_presentation(WebSVGElement *p_element, const String &p_style) const {
	Vector<String> decls = p_style.split(";", false);
	for (const String &decl : decls) {
		const int c = decl.find_char(':');
		if (c == -1) {
			continue;
		}
		_apply_presentation_prop(p_element, decl.substr(0, c), decl.substr(c + 1));
	}
}

void WebSVG::_apply_box_css(const Ref<StyleBoxCSS> &p_sb, const String &p_style) {
	auto parse_lengths = [](const String &v) {
		Vector<float> r;
		Vector<String> t = v.strip_edges().split(" ", false);
		for (const String &s : t) {
			r.push_back(_parse_length(s));
		}
		return r;
	};
	auto style_from_name = [](const String &n) -> StyleBoxCSS::BorderStyle {
		const String s = n.to_lower();
		if (s == "solid") {
			return StyleBoxCSS::BORDER_STYLE_SOLID;
		} else if (s == "dashed") {
			return StyleBoxCSS::BORDER_STYLE_DASHED;
		} else if (s == "dotted") {
			return StyleBoxCSS::BORDER_STYLE_DOTTED;
		} else if (s == "double") {
			return StyleBoxCSS::BORDER_STYLE_DOUBLE;
		}
		return StyleBoxCSS::BORDER_STYLE_NONE;
	};

	Vector<String> decls = p_style.split(";", false);
	for (const String &decl : decls) {
		const int ci = decl.find_char(':');
		if (ci == -1) {
			continue;
		}
		const String key = decl.substr(0, ci).strip_edges().to_lower();
		const String val = decl.substr(ci + 1).strip_edges();

		if (key == "background-color" || key == "background") {
			Color c;
			if (_parse_css_color(val, c)) {
				p_sb->set_background_color(c);
			}
		} else if (key == "background-clip") {
			if (val == "padding-box") {
				p_sb->set_background_clip(StyleBoxCSS::BACKGROUND_CLIP_PADDING_BOX);
			} else if (val == "content-box") {
				p_sb->set_background_clip(StyleBoxCSS::BACKGROUND_CLIP_CONTENT_BOX);
			} else {
				p_sb->set_background_clip(StyleBoxCSS::BACKGROUND_CLIP_BORDER_BOX);
			}
		} else if (key == "border") {
			Vector<String> t = val.split(" ", false);
			for (const String &tok : t) {
				StyleBoxCSS::BorderStyle bs = style_from_name(tok);
				Color c;
				if (tok == "solid" || tok == "dashed" || tok == "dotted" || tok == "double" || tok == "none") {
					p_sb->set_border_style_all(bs);
				} else if (tok.ends_with("px") || tok.is_valid_float()) {
					p_sb->set_border_width_all(_parse_length(tok));
				} else if (_parse_css_color(tok, c)) {
					p_sb->set_border_color_all(c);
				}
			}
		} else if (key == "border-width") {
			Vector<float> w = parse_lengths(val);
			if (w.size() == 1) {
				p_sb->set_border_width_all(w[0]);
			} else if (w.size() >= 4) {
				p_sb->set_border_width(SIDE_TOP, w[0]);
				p_sb->set_border_width(SIDE_RIGHT, w[1]);
				p_sb->set_border_width(SIDE_BOTTOM, w[2]);
				p_sb->set_border_width(SIDE_LEFT, w[3]);
			}
		} else if (key == "border-style") {
			p_sb->set_border_style_all(style_from_name(val));
		} else if (key == "border-color") {
			Color c;
			if (_parse_css_color(val, c)) {
				p_sb->set_border_color_all(c);
			}
		} else if (key == "border-left-width") {
			p_sb->set_border_width(SIDE_LEFT, _parse_length(val));
		} else if (key == "border-top-width") {
			p_sb->set_border_width(SIDE_TOP, _parse_length(val));
		} else if (key == "border-right-width") {
			p_sb->set_border_width(SIDE_RIGHT, _parse_length(val));
		} else if (key == "border-bottom-width") {
			p_sb->set_border_width(SIDE_BOTTOM, _parse_length(val));
		} else if (key == "border-left-color" || key == "border-top-color" || key == "border-right-color" || key == "border-bottom-color") {
			Color c;
			if (_parse_css_color(val, c)) {
				const Side side = key.begins_with("border-left") ? SIDE_LEFT : key.begins_with("border-top") ? SIDE_TOP
						: key.begins_with("border-right")                                                     ? SIDE_RIGHT
																											   : SIDE_BOTTOM;
				p_sb->set_border_color(side, c);
			}
		} else if (key == "border-left-style" || key == "border-top-style" || key == "border-right-style" || key == "border-bottom-style") {
			const Side side = key.begins_with("border-left") ? SIDE_LEFT : key.begins_with("border-top") ? SIDE_TOP
					: key.begins_with("border-right")                                                     ? SIDE_RIGHT
																										   : SIDE_BOTTOM;
			p_sb->set_border_style(side, style_from_name(val));
		} else if (key == "border-left" || key == "border-top" || key == "border-right" || key == "border-bottom") {
			const Side side = key == "border-left" ? SIDE_LEFT : key == "border-top" ? SIDE_TOP
					: key == "border-right"                                           ? SIDE_RIGHT
																					  : SIDE_BOTTOM;
			Vector<String> t = val.split(" ", false);
			for (const String &tok : t) {
				Color c;
				if (tok == "solid" || tok == "dashed" || tok == "dotted" || tok == "double" || tok == "none") {
					p_sb->set_border_style(side, style_from_name(tok));
				} else if (tok.ends_with("px") || tok.is_valid_float()) {
					p_sb->set_border_width(side, _parse_length(tok));
				} else if (_parse_css_color(tok, c)) {
					p_sb->set_border_color(side, c);
				}
			}
		} else if (key == "border-radius") {
			Vector<float> r = parse_lengths(val);
			if (r.size() == 1) {
				p_sb->set_corner_radius_all(Vector2(r[0], r[0]));
			} else if (r.size() == 2) {
				p_sb->set_corner_radius(CORNER_TOP_LEFT, Vector2(r[0], r[0]));
				p_sb->set_corner_radius(CORNER_BOTTOM_RIGHT, Vector2(r[0], r[0]));
				p_sb->set_corner_radius(CORNER_TOP_RIGHT, Vector2(r[1], r[1]));
				p_sb->set_corner_radius(CORNER_BOTTOM_LEFT, Vector2(r[1], r[1]));
			} else if (r.size() >= 4) {
				p_sb->set_corner_radius(CORNER_TOP_LEFT, Vector2(r[0], r[0]));
				p_sb->set_corner_radius(CORNER_TOP_RIGHT, Vector2(r[1], r[1]));
				p_sb->set_corner_radius(CORNER_BOTTOM_RIGHT, Vector2(r[2], r[2]));
				p_sb->set_corner_radius(CORNER_BOTTOM_LEFT, Vector2(r[3], r[3]));
			}
		} else if (key == "padding") {
			Vector<float> p = parse_lengths(val);
			if (p.size() == 1) {
				p_sb->set_padding_all(p[0]);
			} else if (p.size() == 2) {
				p_sb->set_padding(SIDE_TOP, p[0]);
				p_sb->set_padding(SIDE_BOTTOM, p[0]);
				p_sb->set_padding(SIDE_LEFT, p[1]);
				p_sb->set_padding(SIDE_RIGHT, p[1]);
			} else if (p.size() == 3) {
				p_sb->set_padding(SIDE_TOP, p[0]);
				p_sb->set_padding(SIDE_LEFT, p[1]);
				p_sb->set_padding(SIDE_RIGHT, p[1]);
				p_sb->set_padding(SIDE_BOTTOM, p[2]);
			} else if (p.size() >= 4) {
				p_sb->set_padding(SIDE_TOP, p[0]);
				p_sb->set_padding(SIDE_RIGHT, p[1]);
				p_sb->set_padding(SIDE_BOTTOM, p[2]);
				p_sb->set_padding(SIDE_LEFT, p[3]);
			}
		} else if (key == "padding-left") {
			p_sb->set_padding(SIDE_LEFT, _parse_length(val));
		} else if (key == "padding-top") {
			p_sb->set_padding(SIDE_TOP, _parse_length(val));
		} else if (key == "padding-right") {
			p_sb->set_padding(SIDE_RIGHT, _parse_length(val));
		} else if (key == "padding-bottom") {
			p_sb->set_padding(SIDE_BOTTOM, _parse_length(val));
		} else if (key == "box-sizing") {
			p_sb->set_box_sizing(val == "border-box" ? StyleBoxCSS::BOX_SIZING_BORDER_BOX : StyleBoxCSS::BOX_SIZING_CONTENT_BOX);
		} else if (key == "box-shadow") {
			auto split_top_commas = [](const String &v) {
				Vector<String> out;
				int depth = 0;
				int start = 0;
				for (int i = 0; i < v.length(); i++) {
					const char32_t ch = v[i];
					if (ch == '(') {
						depth++;
					} else if (ch == ')') {
						depth = MAX(0, depth - 1);
					} else if (ch == ',' && depth == 0) {
						out.push_back(v.substr(start, i - start));
						start = i + 1;
					}
				}
				out.push_back(v.substr(start));
				return out;
			};
			TypedArray<WebBoxShadow> shadows;
			Vector<String> layers = split_top_commas(val);
			for (const String &layer : layers) {
				if (layer.strip_edges().is_empty()) {
					continue;
				}
				Vector<String> t = layer.strip_edges().split(" ", false);
				Ref<WebBoxShadow> sh;
				sh.instantiate();
				Vector<float> nums;
				bool inset = false;
				bool has_color = false;
				for (const String &tok : t) {
					if (tok.to_lower() == "inset") {
						inset = true;
					} else if (tok.ends_with("px") || tok.is_valid_float() || tok.lstrip("-").is_valid_float()) {
						nums.push_back(_parse_length(tok));
					} else {
						Color c;
						if (_parse_css_color(tok, c)) {
							sh->set_color(c);
							has_color = true;
						}
					}
				}
				if (nums.size() >= 2) {
					sh->set_offset(Vector2(nums[0], nums[1]));
				}
				if (nums.size() >= 3) {
					sh->set_blur_radius(nums[2]);
				}
				if (nums.size() >= 4) {
					sh->set_spread(nums[3]);
				}
				sh->set_inset(inset);
				if (!has_color) {
					sh->set_color(Color(0, 0, 0, 1));
				}
				shadows.push_back(sh);
			}
			p_sb->set_box_shadows(shadows);
		}
	}
}

Error WebSVG::parse_svg(const String &p_markup) {
	raw_svg_markup = p_markup;

	Ref<XMLParser> parser;
	parser.instantiate();
	const Error oerr = parser->open_buffer(p_markup.to_utf8_buffer());
	if (oerr != OK) {
		return oerr;
	}

	_capture_svg_body(p_markup);

	building_dom = true;
	_clear_elements();
	inline_style.unref();

	Vector<Node *> stack;
	Node *current = this;
	WebSVGText *open_text = nullptr;
	// The innermost non-empty shape/text tag; SMIL <animate> children attach to it.
	WebSVGElement *open_leaf = nullptr;

	while (parser->read() == OK) {
		const XMLParser::NodeType nt = parser->get_node_type();
		if (nt == XMLParser::NODE_TEXT) {
			if (open_text) {
				open_text->set_text(open_text->get_text() + parser->get_node_data().strip_edges());
			}
			continue;
		}
		if (nt == XMLParser::NODE_ELEMENT_END) {
			const String name = parser->get_node_name().to_lower();
			if (name == "text") {
				open_text = nullptr;
				open_leaf = nullptr;
			} else if (name == "g") {
				if (!stack.is_empty()) {
					current = stack[stack.size() - 1];
					stack.remove_at(stack.size() - 1);
				}
			} else if (name == "rect" || name == "circle" || name == "ellipse" || name == "line" || name == "polyline" || name == "polygon" || name == "path") {
				open_leaf = nullptr;
			}
			continue;
		}
		if (nt != XMLParser::NODE_ELEMENT) {
			continue;
		}

		const String name = parser->get_node_name().to_lower();
		const bool empty = parser->is_empty();

		if (name == "svg") {
			const String w = parser->get_named_attribute_value_safe("width");
			const String h = parser->get_named_attribute_value_safe("height");
			if (!w.is_empty() && !h.is_empty()) {
				set_size(Vector2(_parse_length(w), _parse_length(h)));
			}
			const String style = parser->get_named_attribute_value_safe("style");
			if (!style.is_empty()) {
				inline_style.instantiate();
				_apply_box_css(inline_style, style);
			}
			continue;
		}

		if (name == "animate" || name == "animatetransform") {
			WebSVGAnimation *anim = memnew(WebSVGAnimation);
			if (name == "animatetransform") {
				anim->set_animation_kind(WebSVGAnimation::KIND_ANIMATE_TRANSFORM);
				const String type = parser->get_named_attribute_value_safe("type").strip_edges().to_lower();
				if (type == "scale") {
					anim->set_svg_transform_type(WebSVGAnimation::TRANSFORM_SCALE);
				} else if (type == "rotate") {
					anim->set_svg_transform_type(WebSVGAnimation::TRANSFORM_ROTATE);
				} else if (type == "skewx") {
					anim->set_svg_transform_type(WebSVGAnimation::TRANSFORM_SKEW_X);
				} else if (type == "skewy") {
					anim->set_svg_transform_type(WebSVGAnimation::TRANSFORM_SKEW_Y);
				}
			} else {
				anim->set_attribute_name(parser->get_named_attribute_value_safe("attributeName"));
			}
			anim->set_from_value(parser->get_named_attribute_value_safe("from"));
			anim->set_to_value(parser->get_named_attribute_value_safe("to"));
			anim->set_values(parser->get_named_attribute_value_safe("values"));
			anim->set_key_times(parser->get_named_attribute_value_safe("keyTimes"));
			const String dur = parser->get_named_attribute_value_safe("dur");
			if (!dur.is_empty()) {
				anim->set_duration(parse_clock_seconds(dur));
			}
			const String begin = parser->get_named_attribute_value_safe("begin");
			if (!begin.is_empty()) {
				anim->set_begin_delay(parse_clock_seconds(begin));
			}
			const String repeat = parser->get_named_attribute_value_safe("repeatCount").strip_edges().to_lower();
			anim->set_repeat_count(repeat.is_empty() || repeat == "indefinite" ? 0.0f : repeat.to_float());
			anim->set_fill_freeze(parser->get_named_attribute_value_safe("fill").strip_edges().to_lower() == "freeze");
			anim->set_svg_calc_mode(parser->get_named_attribute_value_safe("calcMode").strip_edges().to_lower() == "discrete" ? WebSVGAnimation::CALC_MODE_DISCRETE : WebSVGAnimation::CALC_MODE_LINEAR);
			anim->set_additive(parser->get_named_attribute_value_safe("additive").strip_edges().to_lower() == "sum");
			const String anim_id = parser->get_named_attribute_value_safe("id");
			if (!anim_id.is_empty()) {
				anim->set_element_id(anim_id);
			}
			(open_leaf ? (Node *)open_leaf : current)->add_child(anim);
			continue;
		}

		WebSVGElement *e = nullptr;
		if (name == "rect") {
			WebSVGRect *r = memnew(WebSVGRect);
			r->set_rect_position(Vector2(_parse_length(parser->get_named_attribute_value_safe("x")), _parse_length(parser->get_named_attribute_value_safe("y"))));
			r->set_rect_size(Vector2(_parse_length(parser->get_named_attribute_value_safe("width")), _parse_length(parser->get_named_attribute_value_safe("height"))));
			r->set_corner_radius(Vector2(_parse_length(parser->get_named_attribute_value_safe("rx")), _parse_length(parser->get_named_attribute_value_safe("ry"))));
			e = r;
		} else if (name == "circle") {
			WebSVGCircle *c = memnew(WebSVGCircle);
			c->set_center(Vector2(_parse_length(parser->get_named_attribute_value_safe("cx")), _parse_length(parser->get_named_attribute_value_safe("cy"))));
			c->set_radius(_parse_length(parser->get_named_attribute_value_safe("r")));
			e = c;
		} else if (name == "ellipse") {
			WebSVGEllipse *el = memnew(WebSVGEllipse);
			el->set_center(Vector2(_parse_length(parser->get_named_attribute_value_safe("cx")), _parse_length(parser->get_named_attribute_value_safe("cy"))));
			el->set_radii(Vector2(_parse_length(parser->get_named_attribute_value_safe("rx")), _parse_length(parser->get_named_attribute_value_safe("ry"))));
			e = el;
		} else if (name == "line") {
			WebSVGLine *ln = memnew(WebSVGLine);
			ln->set_from(Vector2(_parse_length(parser->get_named_attribute_value_safe("x1")), _parse_length(parser->get_named_attribute_value_safe("y1"))));
			ln->set_to(Vector2(_parse_length(parser->get_named_attribute_value_safe("x2")), _parse_length(parser->get_named_attribute_value_safe("y2"))));
			e = ln;
		} else if (name == "polyline" || name == "polygon") {
			WebSVGPolyline *pl = (name == "polygon") ? memnew(WebSVGPolygon) : memnew(WebSVGPolyline);
			PackedVector2Array pts;
			Vector<String> n = parser->get_named_attribute_value_safe("points").replace(",", " ").split(" ", false);
			for (int i = 0; i + 1 < n.size(); i += 2) {
				pts.push_back(Vector2(n[i].to_float(), n[i + 1].to_float()));
			}
			pl->set_points(pts);
			e = pl;
		} else if (name == "path") {
			WebSVGPath *pa = memnew(WebSVGPath);
			pa->set_d(parser->get_named_attribute_value_safe("d"));
			e = pa;
		} else if (name == "text") {
			WebSVGText *tx = memnew(WebSVGText);
			tx->set_text_position(Vector2(_parse_length(parser->get_named_attribute_value_safe("x")), _parse_length(parser->get_named_attribute_value_safe("y"))));
			const String fs = parser->get_named_attribute_value_safe("font-size");
			if (!fs.is_empty()) {
				tx->set_font_size(_parse_length(fs));
			}
			tx->set_font_family(parser->get_named_attribute_value_safe("font-family"));
			e = tx;
			if (!empty) {
				open_text = tx;
			}
		} else if (name == "g") {
			e = memnew(WebSVGGroup);
		} else {
			continue; // Unsupported element (e.g. defs/gradient) — kept via raw body.
		}

		const String fill_attr = parser->get_named_attribute_value_safe("fill");
		if (!fill_attr.is_empty()) {
			_apply_presentation_prop(e, "fill", fill_attr);
		}
		const String fill_op = parser->get_named_attribute_value_safe("fill-opacity");
		if (!fill_op.is_empty()) {
			_apply_presentation_prop(e, "fill-opacity", fill_op);
		}
		const String stroke_attr = parser->get_named_attribute_value_safe("stroke");
		if (!stroke_attr.is_empty()) {
			_apply_presentation_prop(e, "stroke", stroke_attr);
		}
		const String sw = parser->get_named_attribute_value_safe("stroke-width");
		if (!sw.is_empty()) {
			_apply_presentation_prop(e, "stroke-width", sw);
		}
		const String so = parser->get_named_attribute_value_safe("stroke-opacity");
		if (!so.is_empty()) {
			_apply_presentation_prop(e, "stroke-opacity", so);
		}
		const String op = parser->get_named_attribute_value_safe("opacity");
		if (!op.is_empty()) {
			_apply_presentation_prop(e, "opacity", op);
		}
		const String tr = parser->get_named_attribute_value_safe("transform");
		if (!tr.is_empty()) {
			_apply_presentation_prop(e, "transform", tr);
		}
		const String id = parser->get_named_attribute_value_safe("id");
		if (!id.is_empty()) {
			e->set_element_id(id);
		}
		const String style = parser->get_named_attribute_value_safe("style");
		if (!style.is_empty()) {
			_apply_presentation(e, style);
		}

		current->add_child(e);

		if (name == "g" && !empty) {
			stack.push_back(current);
			current = e;
		} else if (!empty) {
			open_leaf = e; // Shapes and <text> may carry SMIL animation children.
		}
	}

	building_dom = false;
	dom_authoritative = false; // Render the raw body so gradients/defs survive.

	if (inline_style.is_valid()) {
		add_theme_style_override("style", inline_style);
		// Mirror padding and box-sizing into the theme constants WebSVG uses for geometry.
		add_theme_constant_override("padding_left", (int)Math::round(inline_style->get_padding(SIDE_LEFT)));
		add_theme_constant_override("padding_top", (int)Math::round(inline_style->get_padding(SIDE_TOP)));
		add_theme_constant_override("padding_right", (int)Math::round(inline_style->get_padding(SIDE_RIGHT)));
		add_theme_constant_override("padding_bottom", (int)Math::round(inline_style->get_padding(SIDE_BOTTOM)));
		add_theme_constant_override("box_sizing", (int)inline_style->get_box_sizing());
	}
	_update_animation_state();
	mark_content_dirty();
	update_minimum_size();
	return OK;
}

Dictionary WebSVG::get_computed_style() const {
	Dictionary d;
	d["border_box"] = get_border_box();
	d["padding_box"] = get_padding_box();
	const Rect2 cb = get_content_box();
	d["content_box"] = cb;
	d["svg_document"] = _build_svg_document(cb.size);

	Array pad;
	for (int i = 0; i < 4; i++) {
		pad.push_back(_padding((Side)i));
	}
	d["padding"] = pad; // [left, top, right, bottom]
	d["box_sizing"] = _resolved_box_sizing() == StyleBoxCSS::BOX_SIZING_BORDER_BOX ? "border-box" : "content-box";

	Ref<StyleBoxCSS> css = _get_css_style();
	if (css.is_valid()) {
		d["background_color"] = css->get_background_color();
		d["background_clip"] = (int)css->get_background_clip();

		Array bw;
		Array bc;
		Array bs;
		for (int i = 0; i < 4; i++) {
			bw.push_back(css->get_border_width((Side)i));
			bc.push_back(css->get_border_color((Side)i));
			bs.push_back((int)css->get_border_style((Side)i));
		}
		d["border_width"] = bw;
		d["border_color"] = bc;
		d["border_style"] = bs;

		Array radii;
		for (int i = 0; i < 4; i++) {
			radii.push_back(css->get_corner_radius((Corner)i));
		}
		d["corner_radius"] = radii;

		Array shadows;
		TypedArray<WebBoxShadow> sh = css->get_box_shadows();
		for (int i = 0; i < sh.size(); i++) {
			Ref<WebBoxShadow> s = sh[i];
			if (s.is_null()) {
				continue;
			}
			Dictionary sd;
			sd["offset"] = s->get_offset();
			sd["blur_radius"] = s->get_blur_radius();
			sd["spread"] = s->get_spread();
			sd["color"] = s->get_color();
			sd["inset"] = s->is_inset();
			shadows.push_back(sd);
		}
		d["box_shadows"] = shadows;
	}
	return d;
}

void WebSVG::_bind_methods() {
	ClassDB::bind_method(D_METHOD("parse_svg", "markup"), &WebSVG::parse_svg);
	ClassDB::bind_method(D_METHOD("to_svg_string"), &WebSVG::to_svg_string);
	ClassDB::bind_method(D_METHOD("set_svg_markup", "markup"), &WebSVG::set_svg_markup);
	ClassDB::bind_method(D_METHOD("get_svg_markup"), &WebSVG::get_svg_markup);

	ClassDB::bind_method(D_METHOD("get_border_box"), &WebSVG::get_border_box);
	ClassDB::bind_method(D_METHOD("get_padding_box"), &WebSVG::get_padding_box);
	ClassDB::bind_method(D_METHOD("get_content_box"), &WebSVG::get_content_box);
	ClassDB::bind_method(D_METHOD("get_computed_style"), &WebSVG::get_computed_style);

	ClassDB::bind_method(D_METHOD("set_current_color", "color"), &WebSVG::set_current_color);
	ClassDB::bind_method(D_METHOD("get_current_color"), &WebSVG::get_current_color);
	ClassDB::bind_method(D_METHOD("set_animations_enabled", "enabled"), &WebSVG::set_animations_enabled);
	ClassDB::bind_method(D_METHOD("are_animations_enabled"), &WebSVG::are_animations_enabled);
	ClassDB::bind_method(D_METHOD("set_animation_time", "seconds"), &WebSVG::set_animation_time);
	ClassDB::bind_method(D_METHOD("get_animation_time"), &WebSVG::get_animation_time);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "svg_markup", PROPERTY_HINT_MULTILINE_TEXT), "set_svg_markup", "get_svg_markup");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "animations_enabled"), "set_animations_enabled", "are_animations_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "animation_time", PROPERTY_HINT_NONE, "suffix:s", PROPERTY_USAGE_NONE), "set_animation_time", "get_animation_time");

	BIND_THEME_ITEM_CUSTOM(Theme::DATA_TYPE_STYLEBOX, WebSVG, style, "style");
	BIND_THEME_ITEM_CUSTOM_HINT(Theme::DATA_TYPE_CONSTANT, WebSVG, box_sizing, "box_sizing", PROPERTY_HINT_ENUM, "0 - Content Box:0,1 - Border Box:1");
	BIND_THEME_ITEM_CUSTOM(Theme::DATA_TYPE_CONSTANT, WebSVG, padding_left, "padding_left");
	BIND_THEME_ITEM_CUSTOM(Theme::DATA_TYPE_CONSTANT, WebSVG, padding_top, "padding_top");
	BIND_THEME_ITEM_CUSTOM(Theme::DATA_TYPE_CONSTANT, WebSVG, padding_right, "padding_right");
	BIND_THEME_ITEM_CUSTOM(Theme::DATA_TYPE_CONSTANT, WebSVG, padding_bottom, "padding_bottom");
}

WebSVG::WebSVG() {
	set_mouse_filter(MOUSE_FILTER_STOP);
	set_size(Vector2(300, 150)); // CSS default <svg> intrinsic size.
	clipped_content_ci = RenderingServer::get_singleton()->canvas_item_create();
	RenderingServer::get_singleton()->canvas_item_set_parent(clipped_content_ci, get_canvas_item());
}

WebSVG::~WebSVG() {
	if (clipped_content_ci.is_valid()) {
		RenderingServer::get_singleton()->free_rid(clipped_content_ci);
	}
}
