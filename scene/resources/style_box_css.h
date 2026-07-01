/**************************************************************************/
/*  style_box_css.h                                                       */
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
#include "core/variant/typed_array.h"
#include "scene/resources/gradient.h"
#include "scene/resources/style_box.h"
#include "scene/resources/web_box_shadow.h"

class ImageTexture;

// A StyleBox that reproduces the CSS box model of an HTML element:
// background, border (per-side width/color/style), border-radius (elliptical),
// padding, box-sizing and box-shadow. Defaults match the browser UA stylesheet
// for an `<svg>` element (transparent, borderless, no shadow, no padding).
class StyleBoxCSS : public StyleBox {
	GDCLASS(StyleBoxCSS, StyleBox);

public:
	enum BackgroundClip {
		BACKGROUND_CLIP_BORDER_BOX,
		BACKGROUND_CLIP_PADDING_BOX,
		BACKGROUND_CLIP_CONTENT_BOX,
	};

	enum BorderStyle {
		BORDER_STYLE_NONE,
		BORDER_STYLE_SOLID,
		BORDER_STYLE_DASHED,
		BORDER_STYLE_DOTTED,
		BORDER_STYLE_DOUBLE,
	};

	enum BoxSizing {
		BOX_SIZING_CONTENT_BOX,
		BOX_SIZING_BORDER_BOX,
	};

private:
	Color background_color = Color(0, 0, 0, 0); // CSS `transparent`.
	BackgroundClip background_clip = BACKGROUND_CLIP_BORDER_BOX;

	// CSS linear-gradient() / repeating-linear-gradient() background image,
	// painted over `background_color` inside the same clip box.
	Ref<Gradient> background_gradient;
	float background_gradient_angle = 180.0; // CSS convention: 0deg = to top, 90deg = to right.
	bool background_gradient_repeating = false;
	float background_gradient_period = 0.0; // Repeat period in px; 0 = the full gradient line.

	// 1D strip cache for the gradient, rebuilt when the inputs change.
	mutable Ref<ImageTexture> gradient_strip;
	mutable String gradient_strip_key;

	// Blurred shadow textures created at draw time must outlive the frame
	// (the RenderingServer only keeps the RID); cached per shadow + box size.
	mutable HashMap<String, Ref<ImageTexture>> shadow_textures;

	Ref<ImageTexture> _gradient_strip_texture(float p_line_length) const;
	void _gradient_changed();

	real_t border_width[4] = {};
	Color border_color[4] = { Color(0, 0, 0, 1), Color(0, 0, 0, 1), Color(0, 0, 0, 1), Color(0, 0, 0, 1) };
	BorderStyle border_style[4] = { BORDER_STYLE_NONE, BORDER_STYLE_NONE, BORDER_STYLE_NONE, BORDER_STYLE_NONE };

	Vector2 corner_radius[4] = {}; // x = horizontal radius, y = vertical radius.
	real_t padding[4] = {};

	BoxSizing box_sizing = BOX_SIZING_CONTENT_BOX;

	int corner_detail = 8;

	Vector<Ref<WebBoxShadow>> box_shadows;

	void _shadows_changed();
	// Effective border width for a side (0 when its style is `none`).
	real_t _effective_border(Side p_side) const;

protected:
	virtual float get_style_margin(Side p_side) const override;
	static void _bind_methods();

public:
	void set_background_color(const Color &p_color);
	Color get_background_color() const;

	void set_background_clip(BackgroundClip p_clip);
	BackgroundClip get_background_clip() const;

	void set_background_gradient(const Ref<Gradient> &p_gradient);
	Ref<Gradient> get_background_gradient() const;
	void set_background_gradient_angle(float p_degrees);
	float get_background_gradient_angle() const;
	void set_background_gradient_repeating(bool p_repeating);
	bool is_background_gradient_repeating() const;
	void set_background_gradient_period(float p_period);
	float get_background_gradient_period() const;

	void set_border_width_all(real_t p_width);
	void set_border_width(Side p_side, real_t p_width);
	real_t get_border_width(Side p_side) const;

	void set_border_color_all(const Color &p_color);
	void set_border_color(Side p_side, const Color &p_color);
	Color get_border_color(Side p_side) const;

	void set_border_style_all(BorderStyle p_style);
	void set_border_style(Side p_side, BorderStyle p_style);
	BorderStyle get_border_style(Side p_side) const;

	void set_corner_radius_all(const Vector2 &p_radius);
	void set_corner_radius(Corner p_corner, const Vector2 &p_radius);
	Vector2 get_corner_radius(Corner p_corner) const;

	void set_padding_all(real_t p_padding);
	void set_padding(Side p_side, real_t p_padding);
	real_t get_padding(Side p_side) const;

	void set_box_sizing(BoxSizing p_box_sizing);
	BoxSizing get_box_sizing() const;

	void set_corner_detail(int p_corner_detail);
	int get_corner_detail() const;

	void set_box_shadows(const TypedArray<WebBoxShadow> &p_shadows);
	TypedArray<WebBoxShadow> get_box_shadows() const;

	// Geometry helpers (used by WebSVG and the computed-style bridge).
	Rect2 get_padding_box(const Rect2 &p_border_box) const;
	Rect2 get_content_box(const Rect2 &p_border_box) const;
	// Effective border width of a side (0 when its style is `none`).
	real_t get_effective_border_width(Side p_side) const { return _effective_border(p_side); }

	virtual Rect2 get_draw_rect(const Rect2 &p_rect) const override;
	virtual void draw(RID p_canvas_item, const Rect2 &p_rect) const override;

	StyleBoxCSS() {}
};

VARIANT_ENUM_CAST(StyleBoxCSS::BackgroundClip);
VARIANT_ENUM_CAST(StyleBoxCSS::BorderStyle);
VARIANT_ENUM_CAST(StyleBoxCSS::BoxSizing);
