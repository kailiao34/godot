/**************************************************************************/
/*  web_svg_element.h                                                     */
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

#include "core/templates/hash_map.h"
#include "scene/main/node.h"

// Base class for the scriptable SVG DOM. Each element is a Node parented under a
// WebSVG (or a WebSVGGroup) and carries the SVG presentation attributes shared by
// all shapes. Subclasses add their own geometry and override `to_svg()`.
class WebSVGElement : public Node {
	GDCLASS(WebSVGElement, Node);

protected:
	String element_id;

	Color fill = Color(0, 0, 0, 1); // SVG default: black.
	bool fill_none = false;
	float fill_opacity = 1.0;

	Color stroke = Color(0, 0, 0, 1);
	bool stroke_enabled = false; // SVG default: no stroke.
	float stroke_opacity = 1.0;
	float stroke_width = 1.0;

	float opacity = 1.0;
	String transform;
	String extra_style; // Raw "prop:val;..." for attributes not modeled as typed properties (preserves round-trip).

	// Current SMIL animation values, keyed by attribute name. Populated by the
	// owning WebSVG each playback frame (from child WebSVGAnimation elements)
	// and consulted while serializing, so the rasterized frame reflects them.
	HashMap<String, String> animation_overrides;

	void _mark_dirty();

	// Builds the shared presentation attributes (fill, stroke, opacity, ...),
	// emitting only values that differ from the SVG defaults. Emitted as XML
	// presentation attributes (not `style`) so SMIL `<animate>` can override
	// them in a browser; `extra_style` still round-trips through `style`.
	String _presentation_attributes() const;
	// Builds the common attributes (id, transform, presentation, style) appended inside a tag.
	String _common_attributes() const;
	// Emits ` name="value"` honoring an animation override for `name`.
	String _attr(const String &p_name, float p_value) const;
	// Same, but skipped entirely when the base value is unset and not animated.
	String _attr_opt(const String &p_name, float p_value, bool p_emit_base) const;
	// Closes the element tag: `/>` when there are no child elements (animations),
	// otherwise serializes them inside `>...</p_tag>`.
	String _tag_end(const String &p_tag) const;

	static void _bind_methods();
	void _notification(int p_what);

public:
	// Serializes this element (and, for containers, its children) to SVG markup.
	virtual String to_svg() const { return String(); }

	// Concatenates the SVG of every direct WebSVGElement child of p_parent, in document order.
	static String serialize_children(const Node *p_parent);

	void clear_animation_overrides();
	void set_animation_override(const String &p_attribute, const String &p_value);
	bool has_animation_override(const String &p_attribute) const;

	void set_element_id(const String &p_id);
	String get_element_id() const;

	void set_fill(const Color &p_fill);
	Color get_fill() const;
	void set_fill_none(bool p_none);
	bool is_fill_none() const;
	void set_fill_opacity(float p_opacity);
	float get_fill_opacity() const;

	void set_stroke(const Color &p_stroke);
	Color get_stroke() const;
	void set_stroke_enabled(bool p_enabled);
	bool is_stroke_enabled() const;
	void set_stroke_opacity(float p_opacity);
	float get_stroke_opacity() const;
	void set_stroke_width(float p_width);
	float get_stroke_width() const;

	void set_opacity(float p_opacity);
	float get_opacity() const;
	void set_transform_string(const String &p_transform);
	String get_transform_string() const;
	void set_extra_style(const String &p_extra);
	String get_extra_style() const;

	WebSVGElement() {}
};

class WebSVGRect : public WebSVGElement {
	GDCLASS(WebSVGRect, WebSVGElement);
	float x = 0, y = 0, width = 0, height = 0, rx = 0, ry = 0;

protected:
	static void _bind_methods();

public:
	virtual String to_svg() const override;
	void set_rect_position(const Vector2 &p_pos);
	Vector2 get_rect_position() const;
	void set_rect_size(const Vector2 &p_size);
	Vector2 get_rect_size() const;
	void set_corner_radius(const Vector2 &p_radius);
	Vector2 get_corner_radius() const;
};

class WebSVGCircle : public WebSVGElement {
	GDCLASS(WebSVGCircle, WebSVGElement);
	float cx = 0, cy = 0, r = 0;

protected:
	static void _bind_methods();

public:
	virtual String to_svg() const override;
	void set_center(const Vector2 &p_center);
	Vector2 get_center() const;
	void set_radius(float p_radius);
	float get_radius() const;
};

class WebSVGEllipse : public WebSVGElement {
	GDCLASS(WebSVGEllipse, WebSVGElement);
	float cx = 0, cy = 0, rx = 0, ry = 0;

protected:
	static void _bind_methods();

public:
	virtual String to_svg() const override;
	void set_center(const Vector2 &p_center);
	Vector2 get_center() const;
	void set_radii(const Vector2 &p_radii);
	Vector2 get_radii() const;
};

class WebSVGLine : public WebSVGElement {
	GDCLASS(WebSVGLine, WebSVGElement);
	float x1 = 0, y1 = 0, x2 = 0, y2 = 0;

protected:
	static void _bind_methods();

public:
	virtual String to_svg() const override;
	void set_from(const Vector2 &p_from);
	Vector2 get_from() const;
	void set_to(const Vector2 &p_to);
	Vector2 get_to() const;
};

class WebSVGPolyline : public WebSVGElement {
	GDCLASS(WebSVGPolyline, WebSVGElement);

protected:
	PackedVector2Array points;
	static void _bind_methods();
	virtual bool _is_closed() const { return false; }

public:
	virtual String to_svg() const override;
	void set_points(const PackedVector2Array &p_points);
	PackedVector2Array get_points() const;
};

class WebSVGPolygon : public WebSVGPolyline {
	GDCLASS(WebSVGPolygon, WebSVGPolyline);

protected:
	virtual bool _is_closed() const override { return true; }

public:
	WebSVGPolygon() {
		// Polygons default to a visible fill like SVG.
	}
};

class WebSVGPath : public WebSVGElement {
	GDCLASS(WebSVGPath, WebSVGElement);
	String d;

protected:
	static void _bind_methods();

public:
	virtual String to_svg() const override;
	void set_d(const String &p_d);
	String get_d() const;
};

class WebSVGText : public WebSVGElement {
	GDCLASS(WebSVGText, WebSVGElement);
	float x = 0, y = 0;
	float font_size = 16;
	String text;
	String font_family;

protected:
	static void _bind_methods();

public:
	WebSVGText() { fill = Color(0, 0, 0, 1); }
	virtual String to_svg() const override;
	void set_text_position(const Vector2 &p_pos);
	Vector2 get_text_position() const;
	void set_font_size(float p_size);
	float get_font_size() const;
	void set_text(const String &p_text);
	String get_text() const;
	void set_font_family(const String &p_family);
	String get_font_family() const;
};

class WebSVGGroup : public WebSVGElement {
	GDCLASS(WebSVGGroup, WebSVGElement);

protected:
	static void _bind_methods();

public:
	virtual String to_svg() const override;
};

// A SMIL animation (`<animate>` / `<animateTransform>`) attached to its parent
// WebSVGElement. Serialized verbatim into the parent's tag so browsers play it
// natively; the owning WebSVG also evaluates it every frame for live canvas
// playback in the editor and at runtime.
class WebSVGAnimation : public WebSVGElement {
	GDCLASS(WebSVGAnimation, WebSVGElement);

public:
	enum AnimationKind {
		KIND_ANIMATE,
		KIND_ANIMATE_TRANSFORM,
	};
	enum SVGTransformType {
		TRANSFORM_TRANSLATE,
		TRANSFORM_SCALE,
		TRANSFORM_ROTATE,
		TRANSFORM_SKEW_X,
		TRANSFORM_SKEW_Y,
	};
	enum SVGCalcMode {
		CALC_MODE_LINEAR,
		CALC_MODE_DISCRETE,
	};

private:
	AnimationKind kind = KIND_ANIMATE;
	String attribute_name = "opacity";
	SVGTransformType transform_type = TRANSFORM_TRANSLATE;
	String from_value;
	String to_value;
	String values; // Semicolon-separated list; overrides from/to when non-empty.
	String key_times; // Semicolon-separated 0..1 list matching `values`.
	float duration = 1.0; // SMIL `dur`, seconds.
	float begin_delay = 0.0; // SMIL `begin`, seconds.
	float repeat_count = 0.0; // SMIL `repeatCount`; 0 = indefinite.
	bool fill_freeze = false; // SMIL `fill="freeze"`.
	SVGCalcMode calc_mode = CALC_MODE_LINEAR;
	bool additive = false; // SMIL `additive="sum"`.

	Vector<String> _value_list() const;
	String _interpolate(const String &p_a, const String &p_b, float p_t) const;
	String _transform_value(const String &p_args) const;

protected:
	static void _bind_methods();
	void _validate_property(PropertyInfo &p_property) const;

public:
	virtual String to_svg() const override;

	// The attribute this animation drives ("transform" for animateTransform).
	String target_attribute() const;
	// Script-facing wrapper for evaluate(); returns "" when inactive.
	String evaluate_bind(double p_time) const;
	// Computes the animated value at p_time seconds since playback started.
	// Returns false when the animation has no effect at that time (before its
	// begin delay, or finished without fill="freeze").
	bool evaluate(double p_time, String &r_value) const;

	void set_animation_kind(AnimationKind p_kind);
	AnimationKind get_animation_kind() const;
	void set_attribute_name(const String &p_name);
	String get_attribute_name() const;
	void set_svg_transform_type(SVGTransformType p_type);
	SVGTransformType get_svg_transform_type() const;
	void set_from_value(const String &p_value);
	String get_from_value() const;
	void set_to_value(const String &p_value);
	String get_to_value() const;
	void set_values(const String &p_values);
	String get_values() const;
	void set_key_times(const String &p_key_times);
	String get_key_times() const;
	void set_duration(float p_seconds);
	float get_duration() const;
	void set_begin_delay(float p_seconds);
	float get_begin_delay() const;
	void set_repeat_count(float p_count);
	float get_repeat_count() const;
	void set_fill_freeze(bool p_freeze);
	bool is_fill_freeze() const;
	void set_svg_calc_mode(SVGCalcMode p_mode);
	SVGCalcMode get_svg_calc_mode() const;
	void set_additive(bool p_additive);
	bool is_additive() const;
};

VARIANT_ENUM_CAST(WebSVGAnimation::AnimationKind);
VARIANT_ENUM_CAST(WebSVGAnimation::SVGTransformType);
VARIANT_ENUM_CAST(WebSVGAnimation::SVGCalcMode);
