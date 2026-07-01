/**************************************************************************/
/*  style_box_css.cpp                                                     */
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

#include "style_box_css.h"

#include "core/io/image.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/resources/image_texture.h"
#include "servers/rendering/rendering_server.h"

// Builds the closed outline of a rounded rectangle with per-corner elliptical
// radii. Corner order matches Godot's Corner enum: TL, TR, BR, BL. The same
// `p_detail` is used for every corner so two outlines built for nested rects
// have an identical point count (needed to fill a border ring).
static void build_rounded_outline(const Rect2 &p_rect, const Vector2 p_radius[4], int p_detail, Vector<Vector2> &r_out) {
	r_out.clear();
	const real_t half_w = p_rect.size.x * 0.5;
	const real_t half_h = p_rect.size.y * 0.5;

	Vector2 r[4];
	for (int i = 0; i < 4; i++) {
		r[i].x = CLAMP(p_radius[i].x, 0.0, half_w);
		r[i].y = CLAMP(p_radius[i].y, 0.0, half_h);
	}

	const Vector2 tl = p_rect.position;
	const Vector2 br = p_rect.position + p_rect.size;

	// Center of each corner's ellipse and the arc start angle (radians).
	const Vector2 centers[4] = {
		Vector2(tl.x + r[CORNER_TOP_LEFT].x, tl.y + r[CORNER_TOP_LEFT].y),
		Vector2(br.x - r[CORNER_TOP_RIGHT].x, tl.y + r[CORNER_TOP_RIGHT].y),
		Vector2(br.x - r[CORNER_BOTTOM_RIGHT].x, br.y - r[CORNER_BOTTOM_RIGHT].y),
		Vector2(tl.x + r[CORNER_BOTTOM_LEFT].x, br.y - r[CORNER_BOTTOM_LEFT].y),
	};
	const real_t start_angle[4] = { Math::PI, Math::PI * 1.5, 0.0, Math::PI * 0.5 };

	const real_t quarter = Math::PI * 0.5;
	for (int c = 0; c < 4; c++) {
		for (int d = 0; d <= p_detail; d++) {
			const real_t a = start_angle[c] + quarter * ((real_t)d / (real_t)p_detail);
			r_out.push_back(Vector2(
					centers[c].x + Math::cos(a) * r[c].x,
					centers[c].y + Math::sin(a) * r[c].y));
		}
	}
}

// Fills a convex outline as a triangle fan.
static void fill_convex(RID p_ci, const Vector<Vector2> &p_outline, const Color &p_color) {
	const int n = p_outline.size();
	if (n < 3) {
		return;
	}
	Vector<int> indices;
	indices.resize((n - 2) * 3);
	int *ip = indices.ptrw();
	for (int i = 0; i < n - 2; i++) {
		ip[i * 3 + 0] = 0;
		ip[i * 3 + 1] = i + 1;
		ip[i * 3 + 2] = i + 2;
	}
	Vector<Color> colors;
	colors.resize(n);
	Color *cp = colors.ptrw();
	for (int i = 0; i < n; i++) {
		cp[i] = p_color;
	}
	RenderingServer::get_singleton()->canvas_item_add_triangle_array(p_ci, indices, p_outline, colors);
}

// Fills a quad a->b->c->d (in order) with a single color.
static void fill_quad(RID p_ci, const Vector2 &a, const Vector2 &b, const Vector2 &c, const Vector2 &d, const Color &col) {
	Vector<Vector2> pts;
	pts.push_back(a);
	pts.push_back(b);
	pts.push_back(c);
	pts.push_back(d);
	Vector<Color> cols;
	cols.resize(4);
	for (int i = 0; i < 4; i++) {
		cols.write[i] = col;
	}
	Vector<int> idx;
	idx.push_back(0);
	idx.push_back(1);
	idx.push_back(2);
	idx.push_back(0);
	idx.push_back(2);
	idx.push_back(3);
	RenderingServer::get_singleton()->canvas_item_add_triangle_array(p_ci, idx, pts, cols);
}

// Side that each corner's arc belongs to (incoming half / outgoing half), and the
// side that each straight edge (after a corner) belongs to. Corner order: TL,TR,BR,BL.
static const Side CORNER_INCOMING[4] = { SIDE_LEFT, SIDE_TOP, SIDE_RIGHT, SIDE_BOTTOM };
static const Side CORNER_OUTGOING[4] = { SIDE_TOP, SIDE_RIGHT, SIDE_BOTTOM, SIDE_LEFT };
static const Side EDGE_SIDE[4] = { SIDE_TOP, SIDE_RIGHT, SIDE_BOTTOM, SIDE_LEFT };

// Fills the rounded-corner arcs of the border ring, splitting each quarter arc
// between its two adjacent sides so per-side colors blend at the corner.
static void draw_corner_arcs(RID p_ci, const Vector<Vector2> &outer, const Vector<Vector2> &inner, const Color colors[4], const StyleBoxCSS::BorderStyle styles[4], int p_detail) {
	const int pc = p_detail + 1;
	if (outer.size() != 4 * pc || inner.size() != 4 * pc) {
		return;
	}
	for (int c = 0; c < 4; c++) {
		for (int d = 0; d < pc - 1; d++) {
			const int i = c * pc + d;
			const Side side = (d < pc / 2) ? CORNER_INCOMING[c] : CORNER_OUTGOING[c];
			if (styles[side] == StyleBoxCSS::BORDER_STYLE_NONE) {
				continue;
			}
			fill_quad(p_ci, outer[i], outer[i + 1], inner[i + 1], inner[i], colors[side]);
		}
	}
}

// Draws one straight border edge according to its style.
static void draw_edge(RID p_ci, const Vector2 &oA, const Vector2 &oB, const Vector2 &iA, const Vector2 &iB, const Color &col, StyleBoxCSS::BorderStyle style, float width) {
	switch (style) {
		case StyleBoxCSS::BORDER_STYLE_NONE:
			return;
		case StyleBoxCSS::BORDER_STYLE_DOUBLE: {
			// Two lines, each a third of the width, separated by a third-width gap.
			const Vector2 a1 = oA.lerp(iA, 1.0 / 3.0);
			const Vector2 a2 = oA.lerp(iA, 2.0 / 3.0);
			const Vector2 b1 = oB.lerp(iB, 1.0 / 3.0);
			const Vector2 b2 = oB.lerp(iB, 2.0 / 3.0);
			fill_quad(p_ci, oA, oB, b1, a1, col);
			fill_quad(p_ci, a2, b2, iB, iA, col);
		} break;
		case StyleBoxCSS::BORDER_STYLE_DASHED:
		case StyleBoxCSS::BORDER_STYLE_DOTTED: {
			const float edge_len = oA.distance_to(oB);
			if (edge_len <= 0.0) {
				return;
			}
			const float dash = style == StyleBoxCSS::BORDER_STYLE_DOTTED ? MAX(1.0f, width) : MAX(1.0f, width * 3.0f);
			const float gap = style == StyleBoxCSS::BORDER_STYLE_DOTTED ? MAX(1.0f, width) : MAX(1.0f, width * 3.0f);
			float pos = 0.0;
			while (pos < edge_len) {
				const float t0 = pos / edge_len;
				const float t1 = MIN(pos + dash, edge_len) / edge_len;
				fill_quad(p_ci, oA.lerp(oB, t0), oA.lerp(oB, t1), iA.lerp(iB, t1), iA.lerp(iB, t0), col);
				pos += dash + gap;
			}
		} break;
		default: { // Solid.
			fill_quad(p_ci, oA, oB, iB, iA, col);
		} break;
	}
}

// True if p is inside a rounded rect with per-corner elliptical radii.
static bool point_in_rounded(const Rect2 &r, const Vector2 p_radius[4], const Vector2 &p) {
	if (p.x < r.position.x || p.y < r.position.y || p.x > r.position.x + r.size.x || p.y > r.position.y + r.size.y) {
		return false;
	}
	const real_t hw = r.size.x * 0.5;
	const real_t hh = r.size.y * 0.5;
	Vector2 rad[4];
	for (int i = 0; i < 4; i++) {
		rad[i] = Vector2(CLAMP(p_radius[i].x, 0.0, hw), CLAMP(p_radius[i].y, 0.0, hh));
	}
	const Vector2 tl = r.position;
	const Vector2 br = r.position + r.size;
	Vector2 center;
	Vector2 rr;
	if (p.x < tl.x + rad[CORNER_TOP_LEFT].x && p.y < tl.y + rad[CORNER_TOP_LEFT].y) {
		center = Vector2(tl.x + rad[CORNER_TOP_LEFT].x, tl.y + rad[CORNER_TOP_LEFT].y);
		rr = rad[CORNER_TOP_LEFT];
	} else if (p.x > br.x - rad[CORNER_TOP_RIGHT].x && p.y < tl.y + rad[CORNER_TOP_RIGHT].y) {
		center = Vector2(br.x - rad[CORNER_TOP_RIGHT].x, tl.y + rad[CORNER_TOP_RIGHT].y);
		rr = rad[CORNER_TOP_RIGHT];
	} else if (p.x > br.x - rad[CORNER_BOTTOM_RIGHT].x && p.y > br.y - rad[CORNER_BOTTOM_RIGHT].y) {
		center = Vector2(br.x - rad[CORNER_BOTTOM_RIGHT].x, br.y - rad[CORNER_BOTTOM_RIGHT].y);
		rr = rad[CORNER_BOTTOM_RIGHT];
	} else if (p.x < tl.x + rad[CORNER_BOTTOM_LEFT].x && p.y > br.y - rad[CORNER_BOTTOM_LEFT].y) {
		center = Vector2(tl.x + rad[CORNER_BOTTOM_LEFT].x, br.y - rad[CORNER_BOTTOM_LEFT].y);
		rr = rad[CORNER_BOTTOM_LEFT];
	} else {
		return true; // Straight (non-corner) region.
	}
	if (rr.x <= 0 || rr.y <= 0) {
		return true;
	}
	const real_t dx = (p.x - center.x) / rr.x;
	const real_t dy = (p.y - center.y) / rr.y;
	return dx * dx + dy * dy <= 1.0;
}

// Separable Gaussian blur over a single-channel coverage buffer.
static void blur_coverage(Vector<float> &cov, int w, int h, float sigma) {
	if (sigma <= 0.0 || w <= 0 || h <= 0) {
		return;
	}
	const int radius = MAX(1, (int)Math::ceil(sigma * 3.0));
	Vector<float> kernel;
	kernel.resize(radius * 2 + 1);
	float *kp = kernel.ptrw();
	float sum = 0.0;
	for (int i = -radius; i <= radius; i++) {
		const float v = Math::exp(-(float)(i * i) / (2.0f * sigma * sigma));
		kp[i + radius] = v;
		sum += v;
	}
	for (int i = 0; i < kernel.size(); i++) {
		kp[i] /= sum;
	}

	Vector<float> tmp;
	tmp.resize(w * h);
	float *tp = tmp.ptrw();
	const float *cp = cov.ptr();
	// Horizontal.
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			float acc = 0.0;
			for (int k = -radius; k <= radius; k++) {
				const int sx = CLAMP(x + k, 0, w - 1);
				acc += cp[y * w + sx] * kp[k + radius];
			}
			tp[y * w + x] = acc;
		}
	}
	// Vertical.
	float *op = cov.ptrw();
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			float acc = 0.0;
			for (int k = -radius; k <= radius; k++) {
				const int sy = CLAMP(y + k, 0, h - 1);
				acc += tp[sy * w + x] * kp[k + radius];
			}
			op[y * w + x] = acc;
		}
	}
}

// Builds a texture of the given solid color modulated by the coverage buffer's alpha.
static Ref<ImageTexture> coverage_to_texture(const Vector<float> &cov, int w, int h, const Color &p_color) {
	Vector<uint8_t> data;
	data.resize(w * h * 4);
	uint8_t *dp = data.ptrw();
	const float *cp = cov.ptr();
	const uint8_t r8 = (uint8_t)CLAMP(p_color.r * 255.0, 0.0, 255.0);
	const uint8_t g8 = (uint8_t)CLAMP(p_color.g * 255.0, 0.0, 255.0);
	const uint8_t b8 = (uint8_t)CLAMP(p_color.b * 255.0, 0.0, 255.0);
	for (int i = 0; i < w * h; i++) {
		const float a = CLAMP(cp[i] * p_color.a, 0.0, 1.0);
		dp[i * 4 + 0] = r8;
		dp[i * 4 + 1] = g8;
		dp[i * 4 + 2] = b8;
		dp[i * 4 + 3] = (uint8_t)(a * 255.0);
	}
	Ref<Image> img = Image::create_from_data(w, h, false, Image::FORMAT_RGBA8, data);
	return ImageTexture::create_from_image(img);
}

// Draws one outset box-shadow: a rounded-rect coverage mask, blurred and offset.
static void draw_outset_shadow(RID p_ci, const Rect2 &p_border_box, const Vector2 p_radius[4], const Ref<WebBoxShadow> &s, int p_corner_detail, HashMap<String, Ref<ImageTexture>> &r_cache) {
	const float blur = s->get_blur_radius();
	const float spread = s->get_spread();
	const float sigma = blur * 0.5;

	Rect2 shape = p_border_box.grow(spread);
	if (shape.size.x <= 0 || shape.size.y <= 0) {
		return;
	}
	Vector2 srad[4];
	for (int i = 0; i < 4; i++) {
		srad[i] = (p_radius[i] + Vector2(spread, spread)).maxf(0.0);
	}

	const int pad = (int)Math::ceil(sigma * 3.0) + 2;
	const int w = (int)Math::ceil(shape.size.x) + pad * 2;
	const int h = (int)Math::ceil(shape.size.y) + pad * 2;
	if (w <= 0 || h <= 0 || (int64_t)w * h > 16777216) {
		return;
	}

	const String key = vformat("o|%d|%d|%.2f|%.2f|%s|%.2f,%.2f;%.2f,%.2f;%.2f,%.2f;%.2f,%.2f",
			w, h, blur, spread, s->get_color().to_html(),
			srad[0].x, srad[0].y, srad[1].x, srad[1].y, srad[2].x, srad[2].y, srad[3].x, srad[3].y);
	Ref<ImageTexture> tex;
	if (const Ref<ImageTexture> *found = r_cache.getptr(key)) {
		tex = *found;
	} else {
		Vector<float> cov;
		cov.resize(w * h);
		float *cp = cov.ptrw();
		const Rect2 local = Rect2(pad, pad, shape.size.x, shape.size.y);
		for (int y = 0; y < h; y++) {
			for (int x = 0; x < w; x++) {
				cp[y * w + x] = point_in_rounded(local, srad, Vector2(x + 0.5, y + 0.5)) ? 1.0f : 0.0f;
			}
		}
		blur_coverage(cov, w, h, sigma);
		tex = coverage_to_texture(cov, w, h, s->get_color());
		if (r_cache.size() > 8) {
			r_cache.clear();
		}
		r_cache.insert(key, tex);
	}
	const Vector2 pos = shape.position - Vector2(pad, pad) + s->get_offset();
	RenderingServer::get_singleton()->canvas_item_add_texture_rect(p_ci, Rect2(pos, Size2(w, h)), tex->get_rid());
}

// Draws one inset box-shadow: the blurred complement of the (spread/offset) shape, clipped to the padding box.
static void draw_inset_shadow(RID p_ci, const Rect2 &p_padding_box, const Vector2 p_inner_radius[4], const Ref<WebBoxShadow> &s, HashMap<String, Ref<ImageTexture>> &r_cache) {
	const float blur = s->get_blur_radius();
	const float spread = s->get_spread();
	const float sigma = blur * 0.5;

	const Rect2 box = p_padding_box;
	if (box.size.x <= 0 || box.size.y <= 0) {
		return;
	}
	const int w = (int)Math::ceil(box.size.x);
	const int h = (int)Math::ceil(box.size.y);
	if (w <= 0 || h <= 0 || (int64_t)w * h > 16777216) {
		return;
	}

	const String key = vformat("i|%d|%d|%.2f|%.2f|%s|%.2f,%.2f|%.2f,%.2f;%.2f,%.2f;%.2f,%.2f;%.2f,%.2f",
			w, h, blur, spread, s->get_color().to_html(),
			s->get_offset().x, s->get_offset().y,
			p_inner_radius[0].x, p_inner_radius[0].y, p_inner_radius[1].x, p_inner_radius[1].y,
			p_inner_radius[2].x, p_inner_radius[2].y, p_inner_radius[3].x, p_inner_radius[3].y);
	Ref<ImageTexture> tex;
	if (const Ref<ImageTexture> *found = r_cache.getptr(key)) {
		tex = *found;
	} else {
		// Inner hole: padding box shrunk by spread, shifted by offset.
		Rect2 hole = box.grow(-spread);
		hole.position += s->get_offset() - box.position; // into local space
		Vector2 hrad[4];
		for (int i = 0; i < 4; i++) {
			hrad[i] = (p_inner_radius[i] - Vector2(spread, spread)).maxf(0.0);
		}
		const Rect2 local_box = Rect2(0, 0, box.size.x, box.size.y);

		Vector<float> cov;
		cov.resize(w * h);
		float *cp = cov.ptrw();
		for (int y = 0; y < h; y++) {
			for (int x = 0; x < w; x++) {
				const Vector2 p(x + 0.5, y + 0.5);
				cp[y * w + x] = point_in_rounded(hole, hrad, p) ? 0.0f : 1.0f;
			}
		}
		blur_coverage(cov, w, h, sigma);

		// Clip to the padding box's rounded shape.
		for (int y = 0; y < h; y++) {
			for (int x = 0; x < w; x++) {
				if (!point_in_rounded(local_box, p_inner_radius, Vector2(x + 0.5, y + 0.5))) {
					cp[y * w + x] = 0.0f;
				}
			}
		}
		tex = coverage_to_texture(cov, w, h, s->get_color());
		if (r_cache.size() > 8) {
			r_cache.clear();
		}
		r_cache.insert(key, tex);
	}
	RenderingServer::get_singleton()->canvas_item_add_texture_rect(p_ci, Rect2(box.position, Size2(w, h)), tex->get_rid());
}

real_t StyleBoxCSS::_effective_border(Side p_side) const {
	return border_style[p_side] == BORDER_STYLE_NONE ? 0.0 : border_width[p_side];
}

float StyleBoxCSS::get_style_margin(Side p_side) const {
	ERR_FAIL_INDEX_V((int)p_side, 4, 0.0);
	return _effective_border(p_side) + padding[p_side];
}

Rect2 StyleBoxCSS::get_padding_box(const Rect2 &p_border_box) const {
	return p_border_box.grow_individual(
			-_effective_border(SIDE_LEFT), -_effective_border(SIDE_TOP),
			-_effective_border(SIDE_RIGHT), -_effective_border(SIDE_BOTTOM));
}

Rect2 StyleBoxCSS::get_content_box(const Rect2 &p_border_box) const {
	const Rect2 pad = get_padding_box(p_border_box);
	return pad.grow_individual(-padding[SIDE_LEFT], -padding[SIDE_TOP], -padding[SIDE_RIGHT], -padding[SIDE_BOTTOM]);
}

// Inner corner radii for the padding box: CSS shrinks each radius by the
// adjacent border widths, clamped to zero.
static void inner_radii(const Vector2 p_radius[4], const real_t p_border[4], Vector2 r_inner[4]) {
	r_inner[CORNER_TOP_LEFT] = Vector2(MAX(p_radius[CORNER_TOP_LEFT].x - p_border[SIDE_LEFT], 0.0), MAX(p_radius[CORNER_TOP_LEFT].y - p_border[SIDE_TOP], 0.0));
	r_inner[CORNER_TOP_RIGHT] = Vector2(MAX(p_radius[CORNER_TOP_RIGHT].x - p_border[SIDE_RIGHT], 0.0), MAX(p_radius[CORNER_TOP_RIGHT].y - p_border[SIDE_TOP], 0.0));
	r_inner[CORNER_BOTTOM_RIGHT] = Vector2(MAX(p_radius[CORNER_BOTTOM_RIGHT].x - p_border[SIDE_RIGHT], 0.0), MAX(p_radius[CORNER_BOTTOM_RIGHT].y - p_border[SIDE_BOTTOM], 0.0));
	r_inner[CORNER_BOTTOM_LEFT] = Vector2(MAX(p_radius[CORNER_BOTTOM_LEFT].x - p_border[SIDE_LEFT], 0.0), MAX(p_radius[CORNER_BOTTOM_LEFT].y - p_border[SIDE_BOTTOM], 0.0));
}

Rect2 StyleBoxCSS::get_draw_rect(const Rect2 &p_rect) const {
	Rect2 draw_rect = p_rect;
	for (const Ref<WebBoxShadow> &s : box_shadows) {
		if (s.is_null() || s->is_inset()) {
			continue;
		}
		Rect2 sr = p_rect.grow(s->get_spread() + s->get_blur_radius());
		sr.position += s->get_offset();
		draw_rect = draw_rect.merge(sr);
	}
	return draw_rect;
}

void StyleBoxCSS::draw(RID p_ci, const Rect2 &p_rect) const {
	const Rect2 border_box = p_rect;
	if (border_box.size.x <= 0 || border_box.size.y <= 0) {
		return;
	}

	real_t eb[4];
	for (int i = 0; i < 4; i++) {
		eb[i] = _effective_border((Side)i);
	}
	const bool has_border = eb[0] > 0 || eb[1] > 0 || eb[2] > 0 || eb[3] > 0;
	const bool has_radius = corner_radius[0].length() > 0 || corner_radius[1].length() > 0 || corner_radius[2].length() > 0 || corner_radius[3].length() > 0;

	const Rect2 padding_box = get_padding_box(border_box);
	const Rect2 content_box = get_content_box(border_box);

	Vector2 inner_rad[4];
	inner_radii(corner_radius, eb, inner_rad);

	// 1. Outset shadows (painted behind the box). Drawn back-to-front so the
	//    first shadow in the list ends up on top, matching CSS.
	for (int i = box_shadows.size() - 1; i >= 0; i--) {
		const Ref<WebBoxShadow> &s = box_shadows[i];
		if (s.is_null() || s->is_inset() || s->get_color().a <= 0.0) {
			continue;
		}
		if (s->get_blur_radius() > 0.0) {
			draw_outset_shadow(p_ci, border_box, corner_radius, s, corner_detail, shadow_textures);
		} else {
			Rect2 sr = border_box.grow(s->get_spread());
			sr.position += s->get_offset();
			Vector2 srad[4];
			for (int j = 0; j < 4; j++) {
				srad[j] = (corner_radius[j] + Vector2(s->get_spread(), s->get_spread())).maxf(0.0);
			}
			Vector<Vector2> outline;
			build_rounded_outline(sr, srad, corner_detail, outline);
			fill_convex(p_ci, outline, s->get_color());
		}
	}

	// 2. Background, clipped to the requested box. The gradient (CSS
	//    background-image) paints over the background color.
	const bool has_gradient = background_gradient.is_valid() && background_gradient->get_point_count() > 0;
	if (background_color.a > 0.0 || has_gradient) {
		Rect2 bg_rect;
		Vector2 bg_rad[4];
		switch (background_clip) {
			case BACKGROUND_CLIP_PADDING_BOX:
				bg_rect = padding_box;
				for (int i = 0; i < 4; i++) {
					bg_rad[i] = inner_rad[i];
				}
				break;
			case BACKGROUND_CLIP_CONTENT_BOX:
				bg_rect = content_box;
				for (int i = 0; i < 4; i++) {
					bg_rad[i] = inner_rad[i];
				}
				break;
			default:
				bg_rect = border_box;
				for (int i = 0; i < 4; i++) {
					bg_rad[i] = corner_radius[i];
				}
				break;
		}
		Vector<Vector2> outline;
		build_rounded_outline(bg_rect, bg_rad, corner_detail, outline);
		if (background_color.a > 0.0) {
			fill_convex(p_ci, outline, background_color);
		}
		if (has_gradient && bg_rect.size.x > 0 && bg_rect.size.y > 0) {
			// CSS angle convention: 0deg points up, angles rotate clockwise.
			const float ang = Math::deg_to_rad(background_gradient_angle);
			const Vector2 dir(Math::sin(ang), -Math::cos(ang));
			const float line_len = Math::abs(bg_rect.size.x * dir.x) + Math::abs(bg_rect.size.y * dir.y);
			Ref<ImageTexture> strip = _gradient_strip_texture(line_len);
			if (strip.is_valid() && line_len >= 1.0f) {
				const Vector2 start = bg_rect.get_center() - dir * (line_len * 0.5f);
				Vector<Point2> uvs;
				Vector<Color> cols;
				uvs.resize(outline.size());
				cols.resize(outline.size());
				for (int i = 0; i < outline.size(); i++) {
					const float t = (outline[i] - start).dot(dir) / line_len;
					uvs.write[i] = Point2(CLAMP(t, 0.0f, 1.0f), 0.5f);
					cols.write[i] = Color(1, 1, 1, 1);
				}
				RenderingServer::get_singleton()->canvas_item_add_polygon(p_ci, outline, cols, uvs, strip->get_rid());
			}
		}
	}

	// 2.5 Inset shadows, painted over the background and clipped to the padding box.
	for (int i = box_shadows.size() - 1; i >= 0; i--) {
		const Ref<WebBoxShadow> &s = box_shadows[i];
		if (s.is_null() || !s->is_inset() || s->get_color().a <= 0.0) {
			continue;
		}
		draw_inset_shadow(p_ci, padding_box, inner_rad, s, shadow_textures);
	}

	// 3. Border. Each side is drawn with its own color and style; rounded corners
	//    blend the two adjacent side colors.
	if (has_border) {
		Vector<Vector2> outer;
		Vector<Vector2> inner;
		build_rounded_outline(border_box, corner_radius, corner_detail, outer);
		build_rounded_outline(padding_box, inner_rad, corner_detail, inner);

		draw_corner_arcs(p_ci, outer, inner, border_color, border_style, corner_detail);

		const int pc = corner_detail + 1;
		for (int c = 0; c < 4; c++) {
			const Side side = EDGE_SIDE[c];
			if (eb[side] <= 0.0 || border_style[side] == BORDER_STYLE_NONE) {
				continue;
			}
			const int last = c * pc + (pc - 1);
			const int next = ((c + 1) % 4) * pc;
			draw_edge(p_ci, outer[last], outer[next], inner[last], inner[next], border_color[side], border_style[side], eb[side]);
		}
	}
}

void StyleBoxCSS::_shadows_changed() {
	shadow_textures.clear();
	emit_changed();
}

// --- Property accessors ---

void StyleBoxCSS::set_background_color(const Color &p_color) {
	background_color = p_color;
	emit_changed();
}

Color StyleBoxCSS::get_background_color() const {
	return background_color;
}

void StyleBoxCSS::set_background_clip(BackgroundClip p_clip) {
	background_clip = p_clip;
	emit_changed();
}

StyleBoxCSS::BackgroundClip StyleBoxCSS::get_background_clip() const {
	return background_clip;
}

void StyleBoxCSS::_gradient_changed() {
	emit_changed();
}

void StyleBoxCSS::set_background_gradient(const Ref<Gradient> &p_gradient) {
	const Callable changed_cb = callable_mp(this, &StyleBoxCSS::_gradient_changed);
	if (background_gradient.is_valid() && background_gradient->is_connected("changed", changed_cb)) {
		background_gradient->disconnect_changed(changed_cb);
	}
	background_gradient = p_gradient;
	if (background_gradient.is_valid()) {
		background_gradient->connect_changed(changed_cb);
	}
	gradient_strip_key = String();
	emit_changed();
}

Ref<Gradient> StyleBoxCSS::get_background_gradient() const {
	return background_gradient;
}

void StyleBoxCSS::set_background_gradient_angle(float p_degrees) {
	background_gradient_angle = p_degrees;
	emit_changed();
}

float StyleBoxCSS::get_background_gradient_angle() const {
	return background_gradient_angle;
}

void StyleBoxCSS::set_background_gradient_repeating(bool p_repeating) {
	background_gradient_repeating = p_repeating;
	gradient_strip_key = String();
	emit_changed();
}

bool StyleBoxCSS::is_background_gradient_repeating() const {
	return background_gradient_repeating;
}

void StyleBoxCSS::set_background_gradient_period(float p_period) {
	background_gradient_period = MAX(0.0f, p_period);
	gradient_strip_key = String();
	emit_changed();
}

float StyleBoxCSS::get_background_gradient_period() const {
	return background_gradient_period;
}

// Rasterizes the gradient into an N x 1 strip covering the whole gradient line,
// so repeats are baked in and the polygon fill only needs plain 0..1 UVs.
Ref<ImageTexture> StyleBoxCSS::_gradient_strip_texture(float p_line_length) const {
	if (background_gradient.is_null() || background_gradient->get_point_count() == 0) {
		return Ref<ImageTexture>();
	}
	const int n = CLAMP((int)Math::ceil(p_line_length), 2, 4096);

	String key = itos(n) + "|" + (background_gradient_repeating ? "r" : "-") + rtos(background_gradient_period) + "|";
	for (int i = 0; i < background_gradient->get_point_count(); i++) {
		key += rtos(background_gradient->get_offset(i)) + ":" + background_gradient->get_color(i).to_html() + ";";
	}
	key += itos((int)background_gradient->get_interpolation_mode());
	if (gradient_strip.is_valid() && key == gradient_strip_key) {
		return gradient_strip;
	}

	Vector<uint8_t> data;
	data.resize(n * 4);
	uint8_t *dp = data.ptrw();
	const float period = background_gradient_repeating && background_gradient_period > 0.0f ? background_gradient_period : p_line_length;
	for (int i = 0; i < n; i++) {
		const float px = (i + 0.5f) / (float)n * p_line_length;
		float t;
		if (background_gradient_repeating) {
			t = Math::fmod(px, period) / period;
		} else {
			t = (i + 0.5f) / (float)n;
		}
		const Color c = background_gradient->get_color_at_offset(t);
		dp[i * 4 + 0] = (uint8_t)CLAMP((int)Math::round(c.r * 255.0f), 0, 255);
		dp[i * 4 + 1] = (uint8_t)CLAMP((int)Math::round(c.g * 255.0f), 0, 255);
		dp[i * 4 + 2] = (uint8_t)CLAMP((int)Math::round(c.b * 255.0f), 0, 255);
		dp[i * 4 + 3] = (uint8_t)CLAMP((int)Math::round(c.a * 255.0f), 0, 255);
	}
	Ref<Image> img = Image::create_from_data(n, 1, false, Image::FORMAT_RGBA8, data);
	gradient_strip = ImageTexture::create_from_image(img);
	gradient_strip_key = key;
	return gradient_strip;
}

void StyleBoxCSS::set_border_width_all(real_t p_width) {
	for (int i = 0; i < 4; i++) {
		border_width[i] = p_width;
	}
	emit_changed();
}

void StyleBoxCSS::set_border_width(Side p_side, real_t p_width) {
	ERR_FAIL_INDEX((int)p_side, 4);
	border_width[p_side] = MAX(0.0, p_width);
	emit_changed();
}

real_t StyleBoxCSS::get_border_width(Side p_side) const {
	ERR_FAIL_INDEX_V((int)p_side, 4, 0.0);
	return border_width[p_side];
}

void StyleBoxCSS::set_border_color_all(const Color &p_color) {
	for (int i = 0; i < 4; i++) {
		border_color[i] = p_color;
	}
	emit_changed();
}

void StyleBoxCSS::set_border_color(Side p_side, const Color &p_color) {
	ERR_FAIL_INDEX((int)p_side, 4);
	border_color[p_side] = p_color;
	emit_changed();
}

Color StyleBoxCSS::get_border_color(Side p_side) const {
	ERR_FAIL_INDEX_V((int)p_side, 4, Color());
	return border_color[p_side];
}

void StyleBoxCSS::set_border_style_all(BorderStyle p_style) {
	for (int i = 0; i < 4; i++) {
		border_style[i] = p_style;
	}
	emit_changed();
}

void StyleBoxCSS::set_border_style(Side p_side, BorderStyle p_style) {
	ERR_FAIL_INDEX((int)p_side, 4);
	border_style[p_side] = p_style;
	emit_changed();
}

StyleBoxCSS::BorderStyle StyleBoxCSS::get_border_style(Side p_side) const {
	ERR_FAIL_INDEX_V((int)p_side, 4, BORDER_STYLE_NONE);
	return border_style[p_side];
}

void StyleBoxCSS::set_corner_radius_all(const Vector2 &p_radius) {
	for (int i = 0; i < 4; i++) {
		corner_radius[i] = p_radius;
	}
	emit_changed();
}

void StyleBoxCSS::set_corner_radius(Corner p_corner, const Vector2 &p_radius) {
	ERR_FAIL_INDEX((int)p_corner, 4);
	corner_radius[p_corner] = p_radius.maxf(0.0);
	emit_changed();
}

Vector2 StyleBoxCSS::get_corner_radius(Corner p_corner) const {
	ERR_FAIL_INDEX_V((int)p_corner, 4, Vector2());
	return corner_radius[p_corner];
}

void StyleBoxCSS::set_padding_all(real_t p_padding) {
	for (int i = 0; i < 4; i++) {
		padding[i] = p_padding;
	}
	emit_changed();
}

void StyleBoxCSS::set_padding(Side p_side, real_t p_padding) {
	ERR_FAIL_INDEX((int)p_side, 4);
	padding[p_side] = MAX(0.0, p_padding);
	emit_changed();
}

real_t StyleBoxCSS::get_padding(Side p_side) const {
	ERR_FAIL_INDEX_V((int)p_side, 4, 0.0);
	return padding[p_side];
}

void StyleBoxCSS::set_box_sizing(BoxSizing p_box_sizing) {
	box_sizing = p_box_sizing;
	emit_changed();
}

StyleBoxCSS::BoxSizing StyleBoxCSS::get_box_sizing() const {
	return box_sizing;
}

void StyleBoxCSS::set_corner_detail(int p_corner_detail) {
	corner_detail = CLAMP(p_corner_detail, 1, 20);
	emit_changed();
}

int StyleBoxCSS::get_corner_detail() const {
	return corner_detail;
}

void StyleBoxCSS::set_box_shadows(const TypedArray<WebBoxShadow> &p_shadows) {
	const Callable changed_cb = callable_mp(this, &StyleBoxCSS::_shadows_changed);
	for (const Ref<WebBoxShadow> &s : box_shadows) {
		if (s.is_valid() && s->is_connected("changed", changed_cb)) {
			s->disconnect_changed(changed_cb);
		}
	}
	box_shadows.clear();
	for (int i = 0; i < p_shadows.size(); i++) {
		Ref<WebBoxShadow> s = p_shadows[i];
		if (s.is_valid()) {
			s->connect_changed(changed_cb);
		}
		box_shadows.push_back(s);
	}
	emit_changed();
}

TypedArray<WebBoxShadow> StyleBoxCSS::get_box_shadows() const {
	TypedArray<WebBoxShadow> ret;
	ret.resize(box_shadows.size());
	for (int i = 0; i < box_shadows.size(); i++) {
		ret[i] = box_shadows[i];
	}
	return ret;
}

void StyleBoxCSS::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_background_color", "color"), &StyleBoxCSS::set_background_color);
	ClassDB::bind_method(D_METHOD("get_background_color"), &StyleBoxCSS::get_background_color);

	ClassDB::bind_method(D_METHOD("set_background_clip", "clip"), &StyleBoxCSS::set_background_clip);
	ClassDB::bind_method(D_METHOD("get_background_clip"), &StyleBoxCSS::get_background_clip);

	ClassDB::bind_method(D_METHOD("set_background_gradient", "gradient"), &StyleBoxCSS::set_background_gradient);
	ClassDB::bind_method(D_METHOD("get_background_gradient"), &StyleBoxCSS::get_background_gradient);
	ClassDB::bind_method(D_METHOD("set_background_gradient_angle", "degrees"), &StyleBoxCSS::set_background_gradient_angle);
	ClassDB::bind_method(D_METHOD("get_background_gradient_angle"), &StyleBoxCSS::get_background_gradient_angle);
	ClassDB::bind_method(D_METHOD("set_background_gradient_repeating", "repeating"), &StyleBoxCSS::set_background_gradient_repeating);
	ClassDB::bind_method(D_METHOD("is_background_gradient_repeating"), &StyleBoxCSS::is_background_gradient_repeating);
	ClassDB::bind_method(D_METHOD("set_background_gradient_period", "period"), &StyleBoxCSS::set_background_gradient_period);
	ClassDB::bind_method(D_METHOD("get_background_gradient_period"), &StyleBoxCSS::get_background_gradient_period);

	ClassDB::bind_method(D_METHOD("set_border_width_all", "width"), &StyleBoxCSS::set_border_width_all);
	ClassDB::bind_method(D_METHOD("set_border_width", "side", "width"), &StyleBoxCSS::set_border_width);
	ClassDB::bind_method(D_METHOD("get_border_width", "side"), &StyleBoxCSS::get_border_width);

	ClassDB::bind_method(D_METHOD("set_border_color_all", "color"), &StyleBoxCSS::set_border_color_all);
	ClassDB::bind_method(D_METHOD("set_border_color", "side", "color"), &StyleBoxCSS::set_border_color);
	ClassDB::bind_method(D_METHOD("get_border_color", "side"), &StyleBoxCSS::get_border_color);

	ClassDB::bind_method(D_METHOD("set_border_style_all", "style"), &StyleBoxCSS::set_border_style_all);
	ClassDB::bind_method(D_METHOD("set_border_style", "side", "style"), &StyleBoxCSS::set_border_style);
	ClassDB::bind_method(D_METHOD("get_border_style", "side"), &StyleBoxCSS::get_border_style);

	ClassDB::bind_method(D_METHOD("set_corner_radius_all", "radius"), &StyleBoxCSS::set_corner_radius_all);
	ClassDB::bind_method(D_METHOD("set_corner_radius", "corner", "radius"), &StyleBoxCSS::set_corner_radius);
	ClassDB::bind_method(D_METHOD("get_corner_radius", "corner"), &StyleBoxCSS::get_corner_radius);

	ClassDB::bind_method(D_METHOD("set_padding_all", "padding"), &StyleBoxCSS::set_padding_all);
	ClassDB::bind_method(D_METHOD("set_padding", "side", "padding"), &StyleBoxCSS::set_padding);
	ClassDB::bind_method(D_METHOD("get_padding", "side"), &StyleBoxCSS::get_padding);

	ClassDB::bind_method(D_METHOD("set_box_sizing", "box_sizing"), &StyleBoxCSS::set_box_sizing);
	ClassDB::bind_method(D_METHOD("get_box_sizing"), &StyleBoxCSS::get_box_sizing);

	ClassDB::bind_method(D_METHOD("set_corner_detail", "detail"), &StyleBoxCSS::set_corner_detail);
	ClassDB::bind_method(D_METHOD("get_corner_detail"), &StyleBoxCSS::get_corner_detail);

	ClassDB::bind_method(D_METHOD("set_box_shadows", "box_shadows"), &StyleBoxCSS::set_box_shadows);
	ClassDB::bind_method(D_METHOD("get_box_shadows"), &StyleBoxCSS::get_box_shadows);

	ADD_GROUP("Background", "background_");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "background_color"), "set_background_color", "get_background_color");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "background_clip", PROPERTY_HINT_ENUM, "Border Box,Padding Box,Content Box"), "set_background_clip", "get_background_clip");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "background_gradient", PROPERTY_HINT_RESOURCE_TYPE, "Gradient"), "set_background_gradient", "get_background_gradient");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "background_gradient_angle", PROPERTY_HINT_RANGE, "-360,360,0.1,suffix:deg"), "set_background_gradient_angle", "get_background_gradient_angle");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "background_gradient_repeating"), "set_background_gradient_repeating", "is_background_gradient_repeating");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "background_gradient_period", PROPERTY_HINT_RANGE, "0,1000,0.1,or_greater,suffix:px"), "set_background_gradient_period", "get_background_gradient_period");

	ADD_GROUP("Border Width", "border_width_");
	ADD_PROPERTYI(PropertyInfo(Variant::FLOAT, "border_width_left", PROPERTY_HINT_RANGE, "0,100,0.1,or_greater,suffix:px"), "set_border_width", "get_border_width", SIDE_LEFT);
	ADD_PROPERTYI(PropertyInfo(Variant::FLOAT, "border_width_top", PROPERTY_HINT_RANGE, "0,100,0.1,or_greater,suffix:px"), "set_border_width", "get_border_width", SIDE_TOP);
	ADD_PROPERTYI(PropertyInfo(Variant::FLOAT, "border_width_right", PROPERTY_HINT_RANGE, "0,100,0.1,or_greater,suffix:px"), "set_border_width", "get_border_width", SIDE_RIGHT);
	ADD_PROPERTYI(PropertyInfo(Variant::FLOAT, "border_width_bottom", PROPERTY_HINT_RANGE, "0,100,0.1,or_greater,suffix:px"), "set_border_width", "get_border_width", SIDE_BOTTOM);

	ADD_GROUP("Border Color", "border_color_");
	ADD_PROPERTYI(PropertyInfo(Variant::COLOR, "border_color_left"), "set_border_color", "get_border_color", SIDE_LEFT);
	ADD_PROPERTYI(PropertyInfo(Variant::COLOR, "border_color_top"), "set_border_color", "get_border_color", SIDE_TOP);
	ADD_PROPERTYI(PropertyInfo(Variant::COLOR, "border_color_right"), "set_border_color", "get_border_color", SIDE_RIGHT);
	ADD_PROPERTYI(PropertyInfo(Variant::COLOR, "border_color_bottom"), "set_border_color", "get_border_color", SIDE_BOTTOM);

	ADD_GROUP("Border Style", "border_style_");
	const char *border_style_hint = "None,Solid,Dashed,Dotted,Double";
	ADD_PROPERTYI(PropertyInfo(Variant::INT, "border_style_left", PROPERTY_HINT_ENUM, border_style_hint), "set_border_style", "get_border_style", SIDE_LEFT);
	ADD_PROPERTYI(PropertyInfo(Variant::INT, "border_style_top", PROPERTY_HINT_ENUM, border_style_hint), "set_border_style", "get_border_style", SIDE_TOP);
	ADD_PROPERTYI(PropertyInfo(Variant::INT, "border_style_right", PROPERTY_HINT_ENUM, border_style_hint), "set_border_style", "get_border_style", SIDE_RIGHT);
	ADD_PROPERTYI(PropertyInfo(Variant::INT, "border_style_bottom", PROPERTY_HINT_ENUM, border_style_hint), "set_border_style", "get_border_style", SIDE_BOTTOM);

	ADD_GROUP("Corner Radius", "corner_radius_");
	ADD_PROPERTYI(PropertyInfo(Variant::VECTOR2, "corner_radius_top_left", PROPERTY_HINT_NONE, "suffix:px"), "set_corner_radius", "get_corner_radius", CORNER_TOP_LEFT);
	ADD_PROPERTYI(PropertyInfo(Variant::VECTOR2, "corner_radius_top_right", PROPERTY_HINT_NONE, "suffix:px"), "set_corner_radius", "get_corner_radius", CORNER_TOP_RIGHT);
	ADD_PROPERTYI(PropertyInfo(Variant::VECTOR2, "corner_radius_bottom_right", PROPERTY_HINT_NONE, "suffix:px"), "set_corner_radius", "get_corner_radius", CORNER_BOTTOM_RIGHT);
	ADD_PROPERTYI(PropertyInfo(Variant::VECTOR2, "corner_radius_bottom_left", PROPERTY_HINT_NONE, "suffix:px"), "set_corner_radius", "get_corner_radius", CORNER_BOTTOM_LEFT);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "corner_detail", PROPERTY_HINT_RANGE, "1,20,1"), "set_corner_detail", "get_corner_detail");

	ADD_GROUP("Padding", "padding_");
	ADD_PROPERTYI(PropertyInfo(Variant::FLOAT, "padding_left", PROPERTY_HINT_RANGE, "0,100,0.1,or_greater,suffix:px"), "set_padding", "get_padding", SIDE_LEFT);
	ADD_PROPERTYI(PropertyInfo(Variant::FLOAT, "padding_top", PROPERTY_HINT_RANGE, "0,100,0.1,or_greater,suffix:px"), "set_padding", "get_padding", SIDE_TOP);
	ADD_PROPERTYI(PropertyInfo(Variant::FLOAT, "padding_right", PROPERTY_HINT_RANGE, "0,100,0.1,or_greater,suffix:px"), "set_padding", "get_padding", SIDE_RIGHT);
	ADD_PROPERTYI(PropertyInfo(Variant::FLOAT, "padding_bottom", PROPERTY_HINT_RANGE, "0,100,0.1,or_greater,suffix:px"), "set_padding", "get_padding", SIDE_BOTTOM);

	ADD_GROUP("Box", "");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "box_sizing", PROPERTY_HINT_ENUM, "Content Box,Border Box"), "set_box_sizing", "get_box_sizing");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "box_shadows", PROPERTY_HINT_ARRAY_TYPE, vformat("%d/%d:WebBoxShadow", Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE)), "set_box_shadows", "get_box_shadows");

	BIND_ENUM_CONSTANT(BACKGROUND_CLIP_BORDER_BOX);
	BIND_ENUM_CONSTANT(BACKGROUND_CLIP_PADDING_BOX);
	BIND_ENUM_CONSTANT(BACKGROUND_CLIP_CONTENT_BOX);

	BIND_ENUM_CONSTANT(BORDER_STYLE_NONE);
	BIND_ENUM_CONSTANT(BORDER_STYLE_SOLID);
	BIND_ENUM_CONSTANT(BORDER_STYLE_DASHED);
	BIND_ENUM_CONSTANT(BORDER_STYLE_DOTTED);
	BIND_ENUM_CONSTANT(BORDER_STYLE_DOUBLE);

	BIND_ENUM_CONSTANT(BOX_SIZING_CONTENT_BOX);
	BIND_ENUM_CONSTANT(BOX_SIZING_BORDER_BOX);
}
