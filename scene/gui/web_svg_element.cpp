/**************************************************************************/
/*  web_svg_element.cpp                                                   */
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

#include "web_svg_element.h"

#include "core/math/math_funcs.h"
#include "core/object/class_db.h"
#include "scene/gui/web_svg.h"

static String svg_num(float p_v) {
	return rtos(p_v);
}

static String svg_escape(const String &p_text) {
	String s = p_text;
	s = s.replace("&", "&amp;");
	s = s.replace("<", "&lt;");
	s = s.replace(">", "&gt;");
	s = s.replace("\"", "&quot;");
	return s;
}

void WebSVGElement::_mark_dirty() {
	Node *p = get_parent();
	while (p) {
		WebSVG *s = Object::cast_to<WebSVG>(p);
		if (s) {
			s->notify_dom_changed();
			return;
		}
		p = p->get_parent();
	}
}

void WebSVGElement::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_PARENTED:
		case NOTIFICATION_UNPARENTED: {
			_mark_dirty();
		} break;
	}
}

String WebSVGElement::_presentation_attributes() const {
	String a;
	auto attr_or_override = [&](const String &p_name, const String &p_base, bool p_emit_base) {
		const String *ov = animation_overrides.getptr(p_name);
		if (ov) {
			a += " " + p_name + "=\"" + svg_escape(*ov) + "\"";
		} else if (p_emit_base) {
			a += " " + p_name + "=\"" + p_base + "\"";
		}
	};

	if (fill_none) {
		attr_or_override("fill", "none", true);
	} else {
		attr_or_override("fill", "#" + fill.to_html(false), fill != Color(0, 0, 0, 1));
	}
	attr_or_override("fill-opacity", svg_num(fill_opacity), !fill_none && fill_opacity < 1.0);
	if (stroke_enabled) {
		attr_or_override("stroke", "#" + stroke.to_html(false), true);
		attr_or_override("stroke-width", svg_num(stroke_width), true);
		attr_or_override("stroke-opacity", svg_num(stroke_opacity), stroke_opacity < 1.0);
	} else {
		attr_or_override("stroke", String(), false);
		attr_or_override("stroke-width", String(), false);
		attr_or_override("stroke-opacity", String(), false);
	}
	attr_or_override("opacity", svg_num(opacity), opacity < 1.0);
	return a;
}

String WebSVGElement::_common_attributes() const {
	String a;
	if (!element_id.is_empty()) {
		a += " id=\"" + element_id + "\"";
	}
	const String *transform_ov = animation_overrides.getptr("transform");
	if (transform_ov) {
		const String combined = transform.is_empty() ? *transform_ov : transform + " " + *transform_ov;
		a += " transform=\"" + svg_escape(combined) + "\"";
	} else if (!transform.is_empty()) {
		a += " transform=\"" + transform + "\"";
	}
	a += _presentation_attributes();
	if (!extra_style.is_empty()) {
		String st = extra_style;
		if (!st.ends_with(";")) {
			st += ";";
		}
		a += " style=\"" + st + "\"";
	}
	return a;
}

String WebSVGElement::_attr(const String &p_name, float p_value) const {
	const String *ov = animation_overrides.getptr(p_name);
	return " " + p_name + "=\"" + (ov ? svg_escape(*ov) : svg_num(p_value)) + "\"";
}

String WebSVGElement::_attr_opt(const String &p_name, float p_value, bool p_emit_base) const {
	const String *ov = animation_overrides.getptr(p_name);
	if (ov) {
		return " " + p_name + "=\"" + svg_escape(*ov) + "\"";
	}
	if (p_emit_base) {
		return " " + p_name + "=\"" + svg_num(p_value) + "\"";
	}
	return String();
}

String WebSVGElement::_tag_end(const String &p_tag) const {
	const String inner = serialize_children(this);
	if (inner.is_empty()) {
		return " />";
	}
	return ">" + inner + "</" + p_tag + ">";
}

String WebSVGElement::serialize_children(const Node *p_parent) {
	String out;
	const int count = p_parent->get_child_count();
	for (int i = 0; i < count; i++) {
		const WebSVGElement *e = Object::cast_to<WebSVGElement>(p_parent->get_child(i));
		if (e) {
			out += e->to_svg();
		}
	}
	return out;
}

void WebSVGElement::clear_animation_overrides() {
	animation_overrides.clear();
}

void WebSVGElement::set_animation_override(const String &p_attribute, const String &p_value) {
	animation_overrides[p_attribute] = p_value;
}

bool WebSVGElement::has_animation_override(const String &p_attribute) const {
	return animation_overrides.has(p_attribute);
}

// --- Common accessors ---

void WebSVGElement::set_element_id(const String &p_id) {
	element_id = p_id;
	_mark_dirty();
}
String WebSVGElement::get_element_id() const { return element_id; }

void WebSVGElement::set_fill(const Color &p_fill) {
	fill = p_fill;
	_mark_dirty();
}
Color WebSVGElement::get_fill() const { return fill; }

void WebSVGElement::set_fill_none(bool p_none) {
	fill_none = p_none;
	_mark_dirty();
}
bool WebSVGElement::is_fill_none() const { return fill_none; }

void WebSVGElement::set_fill_opacity(float p_opacity) {
	fill_opacity = CLAMP(p_opacity, 0.0, 1.0);
	_mark_dirty();
}
float WebSVGElement::get_fill_opacity() const { return fill_opacity; }

void WebSVGElement::set_stroke(const Color &p_stroke) {
	stroke = p_stroke;
	stroke_enabled = true;
	_mark_dirty();
}
Color WebSVGElement::get_stroke() const { return stroke; }

void WebSVGElement::set_stroke_enabled(bool p_enabled) {
	stroke_enabled = p_enabled;
	_mark_dirty();
}
bool WebSVGElement::is_stroke_enabled() const { return stroke_enabled; }

void WebSVGElement::set_stroke_opacity(float p_opacity) {
	stroke_opacity = CLAMP(p_opacity, 0.0, 1.0);
	_mark_dirty();
}
float WebSVGElement::get_stroke_opacity() const { return stroke_opacity; }

void WebSVGElement::set_stroke_width(float p_width) {
	stroke_width = MAX(0.0, p_width);
	_mark_dirty();
}
float WebSVGElement::get_stroke_width() const { return stroke_width; }

void WebSVGElement::set_opacity(float p_opacity) {
	opacity = CLAMP(p_opacity, 0.0, 1.0);
	_mark_dirty();
}
float WebSVGElement::get_opacity() const { return opacity; }

void WebSVGElement::set_transform_string(const String &p_transform) {
	transform = p_transform;
	_mark_dirty();
}
String WebSVGElement::get_transform_string() const { return transform; }

void WebSVGElement::set_extra_style(const String &p_extra) {
	extra_style = p_extra;
	_mark_dirty();
}
String WebSVGElement::get_extra_style() const { return extra_style; }

void WebSVGElement::_bind_methods() {
	ClassDB::bind_method(D_METHOD("to_svg"), &WebSVGElement::to_svg);

	ClassDB::bind_method(D_METHOD("set_element_id", "id"), &WebSVGElement::set_element_id);
	ClassDB::bind_method(D_METHOD("get_element_id"), &WebSVGElement::get_element_id);
	ClassDB::bind_method(D_METHOD("set_fill", "fill"), &WebSVGElement::set_fill);
	ClassDB::bind_method(D_METHOD("get_fill"), &WebSVGElement::get_fill);
	ClassDB::bind_method(D_METHOD("set_fill_none", "none"), &WebSVGElement::set_fill_none);
	ClassDB::bind_method(D_METHOD("is_fill_none"), &WebSVGElement::is_fill_none);
	ClassDB::bind_method(D_METHOD("set_fill_opacity", "opacity"), &WebSVGElement::set_fill_opacity);
	ClassDB::bind_method(D_METHOD("get_fill_opacity"), &WebSVGElement::get_fill_opacity);
	ClassDB::bind_method(D_METHOD("set_stroke", "stroke"), &WebSVGElement::set_stroke);
	ClassDB::bind_method(D_METHOD("get_stroke"), &WebSVGElement::get_stroke);
	ClassDB::bind_method(D_METHOD("set_stroke_enabled", "enabled"), &WebSVGElement::set_stroke_enabled);
	ClassDB::bind_method(D_METHOD("is_stroke_enabled"), &WebSVGElement::is_stroke_enabled);
	ClassDB::bind_method(D_METHOD("set_stroke_opacity", "opacity"), &WebSVGElement::set_stroke_opacity);
	ClassDB::bind_method(D_METHOD("get_stroke_opacity"), &WebSVGElement::get_stroke_opacity);
	ClassDB::bind_method(D_METHOD("set_stroke_width", "width"), &WebSVGElement::set_stroke_width);
	ClassDB::bind_method(D_METHOD("get_stroke_width"), &WebSVGElement::get_stroke_width);
	ClassDB::bind_method(D_METHOD("set_opacity", "opacity"), &WebSVGElement::set_opacity);
	ClassDB::bind_method(D_METHOD("get_opacity"), &WebSVGElement::get_opacity);
	ClassDB::bind_method(D_METHOD("set_transform_string", "transform"), &WebSVGElement::set_transform_string);
	ClassDB::bind_method(D_METHOD("get_transform_string"), &WebSVGElement::get_transform_string);
	ClassDB::bind_method(D_METHOD("set_extra_style", "extra_style"), &WebSVGElement::set_extra_style);
	ClassDB::bind_method(D_METHOD("get_extra_style"), &WebSVGElement::get_extra_style);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "element_id"), "set_element_id", "get_element_id");
	ADD_GROUP("Fill", "fill_");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "fill"), "set_fill", "get_fill");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "fill_none"), "set_fill_none", "is_fill_none");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fill_opacity", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_fill_opacity", "get_fill_opacity");
	ADD_GROUP("Stroke", "stroke_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "stroke_enabled"), "set_stroke_enabled", "is_stroke_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "stroke"), "set_stroke", "get_stroke");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "stroke_width", PROPERTY_HINT_RANGE, "0,100,0.1,or_greater,suffix:px"), "set_stroke_width", "get_stroke_width");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "stroke_opacity", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_stroke_opacity", "get_stroke_opacity");
	ADD_GROUP("", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "opacity", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_opacity", "get_opacity");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "transform"), "set_transform_string", "get_transform_string");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "extra_style", PROPERTY_HINT_MULTILINE_TEXT), "set_extra_style", "get_extra_style");
}

// --- WebSVGRect ---

String WebSVGRect::to_svg() const {
	String s = "<rect" + _attr("x", x) + _attr("y", y) + _attr("width", width) + _attr("height", height);
	s += _attr_opt("rx", rx, rx > 0);
	s += _attr_opt("ry", ry, ry > 0);
	s += _common_attributes() + _tag_end("rect");
	return s;
}

void WebSVGRect::set_rect_position(const Vector2 &p_pos) {
	x = p_pos.x;
	y = p_pos.y;
	_mark_dirty();
}
Vector2 WebSVGRect::get_rect_position() const { return Vector2(x, y); }

void WebSVGRect::set_rect_size(const Vector2 &p_size) {
	width = p_size.x;
	height = p_size.y;
	_mark_dirty();
}
Vector2 WebSVGRect::get_rect_size() const { return Vector2(width, height); }

void WebSVGRect::set_corner_radius(const Vector2 &p_radius) {
	rx = p_radius.x;
	ry = p_radius.y;
	_mark_dirty();
}
Vector2 WebSVGRect::get_corner_radius() const { return Vector2(rx, ry); }

void WebSVGRect::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_rect_position", "position"), &WebSVGRect::set_rect_position);
	ClassDB::bind_method(D_METHOD("get_rect_position"), &WebSVGRect::get_rect_position);
	ClassDB::bind_method(D_METHOD("set_rect_size", "size"), &WebSVGRect::set_rect_size);
	ClassDB::bind_method(D_METHOD("get_rect_size"), &WebSVGRect::get_rect_size);
	ClassDB::bind_method(D_METHOD("set_corner_radius", "radius"), &WebSVGRect::set_corner_radius);
	ClassDB::bind_method(D_METHOD("get_corner_radius"), &WebSVGRect::get_corner_radius);

	ADD_GROUP("Geometry", "");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "position", PROPERTY_HINT_NONE, "suffix:px"), "set_rect_position", "get_rect_position");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "size", PROPERTY_HINT_NONE, "suffix:px"), "set_rect_size", "get_rect_size");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "corner_radius", PROPERTY_HINT_NONE, "suffix:px"), "set_corner_radius", "get_corner_radius");
}

// --- WebSVGCircle ---

String WebSVGCircle::to_svg() const {
	String s = "<circle" + _attr("cx", cx) + _attr("cy", cy) + _attr("r", r);
	s += _common_attributes() + _tag_end("circle");
	return s;
}

void WebSVGCircle::set_center(const Vector2 &p_center) {
	cx = p_center.x;
	cy = p_center.y;
	_mark_dirty();
}
Vector2 WebSVGCircle::get_center() const { return Vector2(cx, cy); }

void WebSVGCircle::set_radius(float p_radius) {
	r = MAX(0.0, p_radius);
	_mark_dirty();
}
float WebSVGCircle::get_radius() const { return r; }

void WebSVGCircle::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_center", "center"), &WebSVGCircle::set_center);
	ClassDB::bind_method(D_METHOD("get_center"), &WebSVGCircle::get_center);
	ClassDB::bind_method(D_METHOD("set_radius", "radius"), &WebSVGCircle::set_radius);
	ClassDB::bind_method(D_METHOD("get_radius"), &WebSVGCircle::get_radius);

	ADD_GROUP("Geometry", "");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "center", PROPERTY_HINT_NONE, "suffix:px"), "set_center", "get_center");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius", PROPERTY_HINT_RANGE, "0,1000,0.1,or_greater,suffix:px"), "set_radius", "get_radius");
}

// --- WebSVGEllipse ---

String WebSVGEllipse::to_svg() const {
	String s = "<ellipse" + _attr("cx", cx) + _attr("cy", cy) + _attr("rx", rx) + _attr("ry", ry);
	s += _common_attributes() + _tag_end("ellipse");
	return s;
}

void WebSVGEllipse::set_center(const Vector2 &p_center) {
	cx = p_center.x;
	cy = p_center.y;
	_mark_dirty();
}
Vector2 WebSVGEllipse::get_center() const { return Vector2(cx, cy); }

void WebSVGEllipse::set_radii(const Vector2 &p_radii) {
	rx = MAX(0.0, p_radii.x);
	ry = MAX(0.0, p_radii.y);
	_mark_dirty();
}
Vector2 WebSVGEllipse::get_radii() const { return Vector2(rx, ry); }

void WebSVGEllipse::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_center", "center"), &WebSVGEllipse::set_center);
	ClassDB::bind_method(D_METHOD("get_center"), &WebSVGEllipse::get_center);
	ClassDB::bind_method(D_METHOD("set_radii", "radii"), &WebSVGEllipse::set_radii);
	ClassDB::bind_method(D_METHOD("get_radii"), &WebSVGEllipse::get_radii);

	ADD_GROUP("Geometry", "");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "center", PROPERTY_HINT_NONE, "suffix:px"), "set_center", "get_center");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "radii", PROPERTY_HINT_NONE, "suffix:px"), "set_radii", "get_radii");
}

// --- WebSVGLine ---

String WebSVGLine::to_svg() const {
	String s = "<line" + _attr("x1", x1) + _attr("y1", y1) + _attr("x2", x2) + _attr("y2", y2);
	s += _common_attributes() + _tag_end("line");
	return s;
}

void WebSVGLine::set_from(const Vector2 &p_from) {
	x1 = p_from.x;
	y1 = p_from.y;
	_mark_dirty();
}
Vector2 WebSVGLine::get_from() const { return Vector2(x1, y1); }

void WebSVGLine::set_to(const Vector2 &p_to) {
	x2 = p_to.x;
	y2 = p_to.y;
	_mark_dirty();
}
Vector2 WebSVGLine::get_to() const { return Vector2(x2, y2); }

void WebSVGLine::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_from", "from"), &WebSVGLine::set_from);
	ClassDB::bind_method(D_METHOD("get_from"), &WebSVGLine::get_from);
	ClassDB::bind_method(D_METHOD("set_to", "to"), &WebSVGLine::set_to);
	ClassDB::bind_method(D_METHOD("get_to"), &WebSVGLine::get_to);

	ADD_GROUP("Geometry", "");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "from", PROPERTY_HINT_NONE, "suffix:px"), "set_from", "get_from");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "to", PROPERTY_HINT_NONE, "suffix:px"), "set_to", "get_to");
}

// --- WebSVGPolyline / WebSVGPolygon ---

String WebSVGPolyline::to_svg() const {
	String pts;
	for (int i = 0; i < points.size(); i++) {
		if (i > 0) {
			pts += " ";
		}
		pts += svg_num(points[i].x) + "," + svg_num(points[i].y);
	}
	const String tag = _is_closed() ? "polygon" : "polyline";
	const String *pts_ov = animation_overrides.getptr("points");
	String s = "<" + tag + " points=\"" + (pts_ov ? *pts_ov : pts) + "\"";
	s += _common_attributes() + _tag_end(tag);
	return s;
}

void WebSVGPolyline::set_points(const PackedVector2Array &p_points) {
	points = p_points;
	_mark_dirty();
}
PackedVector2Array WebSVGPolyline::get_points() const { return points; }

void WebSVGPolyline::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_points", "points"), &WebSVGPolyline::set_points);
	ClassDB::bind_method(D_METHOD("get_points"), &WebSVGPolyline::get_points);
	ADD_GROUP("Geometry", "");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_VECTOR2_ARRAY, "points"), "set_points", "get_points");
}

// --- WebSVGPath ---

String WebSVGPath::to_svg() const {
	const String *d_ov = animation_overrides.getptr("d");
	String s = "<path d=\"" + (d_ov ? *d_ov : d) + "\"";
	s += _common_attributes() + _tag_end("path");
	return s;
}

void WebSVGPath::set_d(const String &p_d) {
	d = p_d;
	_mark_dirty();
}
String WebSVGPath::get_d() const { return d; }

void WebSVGPath::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_d", "d"), &WebSVGPath::set_d);
	ClassDB::bind_method(D_METHOD("get_d"), &WebSVGPath::get_d);
	ADD_GROUP("Geometry", "");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "d", PROPERTY_HINT_MULTILINE_TEXT), "set_d", "get_d");
}

// --- WebSVGText ---

String WebSVGText::to_svg() const {
	String s = "<text" + _attr("x", x) + _attr("y", y) + _attr("font-size", font_size);
	if (!font_family.is_empty()) {
		s += " font-family=\"" + font_family + "\"";
	}
	s += _common_attributes() + ">" + serialize_children(this) + svg_escape(text) + "</text>";
	return s;
}

void WebSVGText::set_text_position(const Vector2 &p_pos) {
	x = p_pos.x;
	y = p_pos.y;
	_mark_dirty();
}
Vector2 WebSVGText::get_text_position() const { return Vector2(x, y); }

void WebSVGText::set_font_size(float p_size) {
	font_size = MAX(0.0, p_size);
	_mark_dirty();
}
float WebSVGText::get_font_size() const { return font_size; }

void WebSVGText::set_text(const String &p_text) {
	text = p_text;
	_mark_dirty();
}
String WebSVGText::get_text() const { return text; }

void WebSVGText::set_font_family(const String &p_family) {
	font_family = p_family;
	_mark_dirty();
}
String WebSVGText::get_font_family() const { return font_family; }

void WebSVGText::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_text_position", "position"), &WebSVGText::set_text_position);
	ClassDB::bind_method(D_METHOD("get_text_position"), &WebSVGText::get_text_position);
	ClassDB::bind_method(D_METHOD("set_font_size", "size"), &WebSVGText::set_font_size);
	ClassDB::bind_method(D_METHOD("get_font_size"), &WebSVGText::get_font_size);
	ClassDB::bind_method(D_METHOD("set_text", "text"), &WebSVGText::set_text);
	ClassDB::bind_method(D_METHOD("get_text"), &WebSVGText::get_text);
	ClassDB::bind_method(D_METHOD("set_font_family", "family"), &WebSVGText::set_font_family);
	ClassDB::bind_method(D_METHOD("get_font_family"), &WebSVGText::get_font_family);

	ADD_GROUP("Geometry", "");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "position", PROPERTY_HINT_NONE, "suffix:px"), "set_text_position", "get_text_position");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "text", PROPERTY_HINT_MULTILINE_TEXT), "set_text", "get_text");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "font_size", PROPERTY_HINT_RANGE, "1,256,0.5,or_greater,suffix:px"), "set_font_size", "get_font_size");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "font_family"), "set_font_family", "get_font_family");
}

// --- WebSVGGroup ---

String WebSVGGroup::to_svg() const {
	String s = "<g";
	s += _common_attributes() + ">";
	s += serialize_children(this);
	s += "</g>";
	return s;
}

void WebSVGGroup::_bind_methods() {
}

// --- WebSVGAnimation ---

static const char *SVG_TRANSFORM_TYPE_NAMES[] = { "translate", "scale", "rotate", "skewX", "skewY" };

String WebSVGAnimation::target_attribute() const {
	return kind == KIND_ANIMATE_TRANSFORM ? String("transform") : attribute_name;
}

Vector<String> WebSVGAnimation::_value_list() const {
	Vector<String> out;
	if (!values.is_empty()) {
		Vector<String> parts = values.split(";", false);
		for (const String &p : parts) {
			out.push_back(p.strip_edges());
		}
		return out;
	}
	if (!from_value.is_empty()) {
		out.push_back(from_value.strip_edges());
	}
	if (!to_value.is_empty()) {
		out.push_back(to_value.strip_edges());
	}
	return out;
}

// Parses a value as a list of numbers ("12", "10px", "0 60 60", "4,2"). Returns
// false when any token is not numeric.
static bool parse_number_list(const String &p_value, Vector<float> &r_out) {
	r_out.clear();
	Vector<String> tokens = p_value.replace(",", " ").split(" ", false);
	if (tokens.is_empty()) {
		return false;
	}
	for (String tok : tokens) {
		tok = tok.strip_edges();
		if (tok.ends_with("px")) {
			tok = tok.substr(0, tok.length() - 2);
		}
		if (!tok.is_valid_float()) {
			return false;
		}
		r_out.push_back(tok.to_float());
	}
	return true;
}

String WebSVGAnimation::_interpolate(const String &p_a, const String &p_b, float p_t) const {
	Vector<float> na;
	Vector<float> nb;
	if (parse_number_list(p_a, na) && parse_number_list(p_b, nb) && na.size() == nb.size()) {
		String out;
		for (int i = 0; i < na.size(); i++) {
			if (i > 0) {
				out += " ";
			}
			out += svg_num(Math::lerp(na[i], nb[i], p_t));
		}
		return out;
	}
	Color ca;
	Color cb;
	if (WebSVG::parse_css_color_value(p_a, ca) && WebSVG::parse_css_color_value(p_b, cb)) {
		const Color c = ca.lerp(cb, p_t);
		return "#" + c.to_html(c.a < 1.0);
	}
	// Non-interpolable values fall back to discrete stepping.
	return p_t < 1.0 ? p_a : p_b;
}

String WebSVGAnimation::_transform_value(const String &p_args) const {
	return String(SVG_TRANSFORM_TYPE_NAMES[transform_type]) + "(" + p_args + ")";
}

bool WebSVGAnimation::evaluate(double p_time, String &r_value) const {
	const Vector<String> vals = _value_list();
	if (vals.is_empty() || duration <= 0.0f) {
		return false;
	}
	const double local = p_time - begin_delay;
	if (local < 0.0) {
		return false;
	}

	float progress;
	const double iterations = local / duration;
	if (repeat_count > 0.0f && iterations >= repeat_count) {
		if (!fill_freeze) {
			return false;
		}
		const double frac = repeat_count - Math::floor(repeat_count);
		progress = frac <= 0.0 ? 1.0f : (float)frac;
	} else {
		progress = (float)(Math::fmod(local, (double)duration) / duration);
	}

	String value;
	if (vals.size() == 1) {
		value = vals[0];
	} else {
		// Resolve keyTimes; fall back to a uniform spacing per SMIL defaults.
		Vector<float> kt;
		if (!key_times.is_empty()) {
			Vector<String> parts = key_times.split(";", false);
			for (const String &p : parts) {
				kt.push_back(CLAMP(p.strip_edges().to_float(), 0.0f, 1.0f));
			}
		}
		if (kt.size() != vals.size()) {
			kt.clear();
			const int n = vals.size();
			for (int i = 0; i < n; i++) {
				if (calc_mode == CALC_MODE_DISCRETE) {
					kt.push_back((float)i / (float)n);
				} else {
					kt.push_back(n > 1 ? (float)i / (float)(n - 1) : 0.0f);
				}
			}
		}

		if (calc_mode == CALC_MODE_DISCRETE) {
			int idx = 0;
			for (int i = 0; i < kt.size(); i++) {
				if (progress >= kt[i]) {
					idx = i;
				}
			}
			value = vals[idx];
		} else {
			if (progress <= kt[0]) {
				value = vals[0];
			} else if (progress >= kt[kt.size() - 1]) {
				value = vals[vals.size() - 1];
			} else {
				int seg = 0;
				for (int i = 0; i < kt.size() - 1; i++) {
					if (progress >= kt[i] && progress <= kt[i + 1]) {
						seg = i;
						break;
					}
				}
				const float span = kt[seg + 1] - kt[seg];
				const float local_t = span <= 0.0f ? 1.0f : (progress - kt[seg]) / span;
				value = _interpolate(vals[seg], vals[seg + 1], local_t);
			}
		}
	}

	r_value = kind == KIND_ANIMATE_TRANSFORM ? _transform_value(value) : value;
	return true;
}

String WebSVGAnimation::to_svg() const {
	String s;
	if (kind == KIND_ANIMATE_TRANSFORM) {
		s = String("<animateTransform attributeName=\"transform\" type=\"") + SVG_TRANSFORM_TYPE_NAMES[transform_type] + "\"";
	} else {
		s = "<animate attributeName=\"" + svg_escape(attribute_name) + "\"";
	}
	if (!values.is_empty()) {
		s += " values=\"" + svg_escape(values) + "\"";
		if (!key_times.is_empty()) {
			s += " keyTimes=\"" + svg_escape(key_times) + "\"";
		}
	} else {
		if (!from_value.is_empty()) {
			s += " from=\"" + svg_escape(from_value) + "\"";
		}
		if (!to_value.is_empty()) {
			s += " to=\"" + svg_escape(to_value) + "\"";
		}
	}
	s += " dur=\"" + svg_num(duration) + "s\"";
	if (begin_delay != 0.0f) {
		s += " begin=\"" + svg_num(begin_delay) + "s\"";
	}
	if (repeat_count > 0.0f) {
		s += " repeatCount=\"" + svg_num(repeat_count) + "\"";
	} else {
		s += " repeatCount=\"indefinite\"";
	}
	if (fill_freeze) {
		s += " fill=\"freeze\"";
	}
	if (calc_mode == CALC_MODE_DISCRETE) {
		s += " calcMode=\"discrete\"";
	}
	if (additive) {
		s += " additive=\"sum\"";
	}
	if (!element_id.is_empty()) {
		s += " id=\"" + element_id + "\"";
	}
	s += " />";
	return s;
}

void WebSVGAnimation::set_animation_kind(AnimationKind p_kind) {
	kind = p_kind;
	notify_property_list_changed();
	_mark_dirty();
}
WebSVGAnimation::AnimationKind WebSVGAnimation::get_animation_kind() const { return kind; }

void WebSVGAnimation::set_attribute_name(const String &p_name) {
	attribute_name = p_name.strip_edges();
	_mark_dirty();
}
String WebSVGAnimation::get_attribute_name() const { return attribute_name; }

void WebSVGAnimation::set_svg_transform_type(SVGTransformType p_type) {
	transform_type = p_type;
	_mark_dirty();
}
WebSVGAnimation::SVGTransformType WebSVGAnimation::get_svg_transform_type() const { return transform_type; }

void WebSVGAnimation::set_from_value(const String &p_value) {
	from_value = p_value;
	_mark_dirty();
}
String WebSVGAnimation::get_from_value() const { return from_value; }

void WebSVGAnimation::set_to_value(const String &p_value) {
	to_value = p_value;
	_mark_dirty();
}
String WebSVGAnimation::get_to_value() const { return to_value; }

void WebSVGAnimation::set_values(const String &p_values) {
	values = p_values;
	_mark_dirty();
}
String WebSVGAnimation::get_values() const { return values; }

void WebSVGAnimation::set_key_times(const String &p_key_times) {
	key_times = p_key_times;
	_mark_dirty();
}
String WebSVGAnimation::get_key_times() const { return key_times; }

void WebSVGAnimation::set_duration(float p_seconds) {
	duration = MAX(0.0f, p_seconds);
	_mark_dirty();
}
float WebSVGAnimation::get_duration() const { return duration; }

void WebSVGAnimation::set_begin_delay(float p_seconds) {
	begin_delay = p_seconds;
	_mark_dirty();
}
float WebSVGAnimation::get_begin_delay() const { return begin_delay; }

void WebSVGAnimation::set_repeat_count(float p_count) {
	repeat_count = MAX(0.0f, p_count);
	_mark_dirty();
}
float WebSVGAnimation::get_repeat_count() const { return repeat_count; }

void WebSVGAnimation::set_fill_freeze(bool p_freeze) {
	fill_freeze = p_freeze;
	_mark_dirty();
}
bool WebSVGAnimation::is_fill_freeze() const { return fill_freeze; }

void WebSVGAnimation::set_svg_calc_mode(SVGCalcMode p_mode) {
	calc_mode = p_mode;
	_mark_dirty();
}
WebSVGAnimation::SVGCalcMode WebSVGAnimation::get_svg_calc_mode() const { return calc_mode; }

void WebSVGAnimation::set_additive(bool p_additive) {
	additive = p_additive;
	_mark_dirty();
}
bool WebSVGAnimation::is_additive() const { return additive; }

void WebSVGAnimation::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_animation_kind", "kind"), &WebSVGAnimation::set_animation_kind);
	ClassDB::bind_method(D_METHOD("get_animation_kind"), &WebSVGAnimation::get_animation_kind);
	ClassDB::bind_method(D_METHOD("set_attribute_name", "name"), &WebSVGAnimation::set_attribute_name);
	ClassDB::bind_method(D_METHOD("get_attribute_name"), &WebSVGAnimation::get_attribute_name);
	ClassDB::bind_method(D_METHOD("set_svg_transform_type", "type"), &WebSVGAnimation::set_svg_transform_type);
	ClassDB::bind_method(D_METHOD("get_svg_transform_type"), &WebSVGAnimation::get_svg_transform_type);
	ClassDB::bind_method(D_METHOD("set_from_value", "value"), &WebSVGAnimation::set_from_value);
	ClassDB::bind_method(D_METHOD("get_from_value"), &WebSVGAnimation::get_from_value);
	ClassDB::bind_method(D_METHOD("set_to_value", "value"), &WebSVGAnimation::set_to_value);
	ClassDB::bind_method(D_METHOD("get_to_value"), &WebSVGAnimation::get_to_value);
	ClassDB::bind_method(D_METHOD("set_values", "values"), &WebSVGAnimation::set_values);
	ClassDB::bind_method(D_METHOD("get_values"), &WebSVGAnimation::get_values);
	ClassDB::bind_method(D_METHOD("set_key_times", "key_times"), &WebSVGAnimation::set_key_times);
	ClassDB::bind_method(D_METHOD("get_key_times"), &WebSVGAnimation::get_key_times);
	ClassDB::bind_method(D_METHOD("set_duration", "seconds"), &WebSVGAnimation::set_duration);
	ClassDB::bind_method(D_METHOD("get_duration"), &WebSVGAnimation::get_duration);
	ClassDB::bind_method(D_METHOD("set_begin_delay", "seconds"), &WebSVGAnimation::set_begin_delay);
	ClassDB::bind_method(D_METHOD("get_begin_delay"), &WebSVGAnimation::get_begin_delay);
	ClassDB::bind_method(D_METHOD("set_repeat_count", "count"), &WebSVGAnimation::set_repeat_count);
	ClassDB::bind_method(D_METHOD("get_repeat_count"), &WebSVGAnimation::get_repeat_count);
	ClassDB::bind_method(D_METHOD("set_fill_freeze", "freeze"), &WebSVGAnimation::set_fill_freeze);
	ClassDB::bind_method(D_METHOD("is_fill_freeze"), &WebSVGAnimation::is_fill_freeze);
	ClassDB::bind_method(D_METHOD("set_svg_calc_mode", "mode"), &WebSVGAnimation::set_svg_calc_mode);
	ClassDB::bind_method(D_METHOD("get_svg_calc_mode"), &WebSVGAnimation::get_svg_calc_mode);
	ClassDB::bind_method(D_METHOD("set_additive", "additive"), &WebSVGAnimation::set_additive);
	ClassDB::bind_method(D_METHOD("is_additive"), &WebSVGAnimation::is_additive);
	ClassDB::bind_method(D_METHOD("evaluate", "time"), &WebSVGAnimation::evaluate_bind);
	ClassDB::bind_method(D_METHOD("target_attribute"), &WebSVGAnimation::target_attribute);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "animation_kind", PROPERTY_HINT_ENUM, "Animate,Animate Transform"), "set_animation_kind", "get_animation_kind");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "attribute_name"), "set_attribute_name", "get_attribute_name");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "svg_transform_type", PROPERTY_HINT_ENUM, "Translate,Scale,Rotate,Skew X,Skew Y"), "set_svg_transform_type", "get_svg_transform_type");
	ADD_GROUP("Values", "");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "from_value"), "set_from_value", "get_from_value");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "to_value"), "set_to_value", "get_to_value");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "values"), "set_values", "get_values");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "key_times"), "set_key_times", "get_key_times");
	ADD_GROUP("Timing", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "duration", PROPERTY_HINT_RANGE, "0,60,0.01,or_greater,suffix:s"), "set_duration", "get_duration");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "begin_delay", PROPERTY_HINT_RANGE, "0,60,0.01,or_greater,suffix:s"), "set_begin_delay", "get_begin_delay");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "repeat_count", PROPERTY_HINT_RANGE, "0,100,0.1,or_greater"), "set_repeat_count", "get_repeat_count");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "fill_freeze"), "set_fill_freeze", "is_fill_freeze");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "svg_calc_mode", PROPERTY_HINT_ENUM, "Linear,Discrete"), "set_svg_calc_mode", "get_svg_calc_mode");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "additive"), "set_additive", "is_additive");

	BIND_ENUM_CONSTANT(KIND_ANIMATE);
	BIND_ENUM_CONSTANT(KIND_ANIMATE_TRANSFORM);
	BIND_ENUM_CONSTANT(TRANSFORM_TRANSLATE);
	BIND_ENUM_CONSTANT(TRANSFORM_SCALE);
	BIND_ENUM_CONSTANT(TRANSFORM_ROTATE);
	BIND_ENUM_CONSTANT(TRANSFORM_SKEW_X);
	BIND_ENUM_CONSTANT(TRANSFORM_SKEW_Y);
	BIND_ENUM_CONSTANT(CALC_MODE_LINEAR);
	BIND_ENUM_CONSTANT(CALC_MODE_DISCRETE);
}

String WebSVGAnimation::evaluate_bind(double p_time) const {
	String value;
	if (evaluate(p_time, value)) {
		return value;
	}
	return String();
}

void WebSVGAnimation::_validate_property(PropertyInfo &p_property) const {
	// Hide the inherited paint properties: an animation has no visual box of its own.
	static const char *hidden[] = { "fill", "fill_none", "fill_opacity", "stroke_enabled", "stroke", "stroke_width", "stroke_opacity", "opacity", "transform", "extra_style" };
	for (const char *name : hidden) {
		if (p_property.name == name) {
			p_property.usage = PROPERTY_USAGE_NONE;
			return;
		}
	}
	if (kind != KIND_ANIMATE_TRANSFORM && p_property.name == "svg_transform_type") {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR;
	}
	if (kind == KIND_ANIMATE_TRANSFORM && p_property.name == "attribute_name") {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR;
	}
}
