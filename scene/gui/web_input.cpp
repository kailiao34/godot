/**************************************************************************/
/*  web_input.cpp                                                         */
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

#include "web_input.h"

#include "scene/resources/text_line.h"

#include "core/input/input.h"
#include "core/input/input_event.h"
#include "core/io/file_access.h"
#include "core/io/image.h"
#include "core/io/resource_loader.h"
#include "core/os/keyboard.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/object/ref_counted.h"
#include "core/os/os.h"
#include "scene/gui/color_picker.h"
#include "scene/gui/file_dialog.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/popup.h"
#include "scene/gui/popup_menu.h"
#include "scene/gui/web_input_picker.h"
#include "scene/resources/font.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/style_box.h"
#include "scene/resources/style_box_flat.h"
#include "scene/resources/texture.h"
#include "scene/theme/theme_db.h"
#include "servers/display/display_server.h"
#include "servers/rendering/rendering_server.h"

// ---------------------------------------------------------------------------
// Chromium UA-default constants (captured by tools/webinput_bridge into
// chromium_defaults.json). All sizes are border-box (match getBoundingClientRect).
// ---------------------------------------------------------------------------
static const Color CHROME_TEXT_BG = Color(1, 1, 1, 1); // rgb(255,255,255)
static const Color CHROME_TEXT_FG = Color(0, 0, 0, 1); // rgb(0,0,0)
static const Color CHROME_FIELD_BORDER = Color(118.0 / 255, 118.0 / 255, 118.0 / 255, 1); // #767676
static const Color CHROME_BUTTON_BG = Color(239.0 / 255, 239.0 / 255, 239.0 / 255, 1); // #efefef
// Native control face/border per interaction state, sampled from Chrome 148 on
// Windows. Buttons, the colour swatch, the file button and the unfilled part of
// a range track all share this palette.
static const Color CHROME_FACE_HOVER = Color(229.0 / 255, 229.0 / 255, 229.0 / 255, 1); // #e5e5e5
static const Color CHROME_FACE_ACTIVE = Color(245.0 / 255, 245.0 / 255, 245.0 / 255, 1); // #f5f5f5
static const Color CHROME_BORDER_HOVER = Color(79.0 / 255, 79.0 / 255, 79.0 / 255, 1); // #4f4f4f
static const Color CHROME_BORDER_ACTIVE = Color(141.0 / 255, 141.0 / 255, 141.0 / 255, 1); // #8d8d8d
// type=number spinner.
static const Color CHROME_SPIN_BG = Color(252.0 / 255, 252.0 / 255, 252.0 / 255, 1); // #fcfcfc
static const Color CHROME_SPIN_ARROW = Color(139.0 / 255, 139.0 / 255, 139.0 / 255, 1); // #8b8b8b
static const Color CHROME_SPIN_ARROW_HOVER = Color(99.0 / 255, 99.0 / 255, 99.0 / 255, 1); // #636363
static const Color CHROME_BUTTON_BORDER = Color(0, 0, 0, 1);
// ::-webkit-color-swatch's own border (Chrome 154).
static const Color CHROME_SWATCH_BORDER = Color(119.0 / 255, 119.0 / 255, 119.0 / 255, 1); // #777777
static const Color CHROME_PLACEHOLDER = Color(117.0 / 255, 117.0 / 255, 117.0 / 255, 1);
static const Color CHROME_DISABLED_TEXT = Color(84.0 / 255, 84.0 / 255, 84.0 / 255, 1); // rgb(84,84,84)
static const Color CHROME_DISABLED_BG = Color(248.0 / 255, 248.0 / 255, 248.0 / 255, 1); // rgb(248,248,248)
static const Color CHROME_DISABLED_BORDER = Color(212.0 / 255, 212.0 / 255, 212.0 / 255, 1); // rgb(212,212,212)
// Disabled checkbox/radio paint (Chrome 154), fitted as colour + alpha against
// white and black backdrops: the border, the interior, the checked fill (the
// disabled accent, painted over the interior) and the check mark.
static const Color CHROME_DISABLED_CONTROL_BORDER = Color(189.0 / 255, 189.0 / 255, 189.0 / 255, 0.70);
static const Color CHROME_DISABLED_CONTROL_BG = Color(245.0 / 255, 245.0 / 255, 245.0 / 255, 0.68);
static const Color CHROME_DISABLED_ACCENT = Color(118.0 / 255, 118.0 / 255, 118.0 / 255, 0.3);
static const Color CHROME_DISABLED_CHECK = Color(1, 1, 1, 0.6);
// Windows system highlight, painted behind the focused date/time field.
static const Color CHROME_FIELD_HIGHLIGHT = Color(0.0, 120.0 / 255, 215.0 / 255, 1);
static const real_t CHROME_FONT_SIZE = 13.3333;

// Checked checkbox/radio and the range thumb are painted by the browser in the
// OS accent colour. Read the same accent so they match; fall back to a neutral
// blue when the platform does not expose one.
Color WebInput::_accent_color() const {
	// The theme `accent_color` (CSS accent-color), default blue, used to fill
	// checked checkbox/radio and the range thumb.
	return theme_cache.accent_color;
}

Color WebInput::_state_face_color() const {
	if (disabled) {
		return CHROME_DISABLED_BG;
	}
	if (pressed) {
		return CHROME_FACE_ACTIVE;
	}
	return hovered ? CHROME_FACE_HOVER : CHROME_BUTTON_BG;
}

Color WebInput::_state_border_color() const {
	if (disabled) {
		return CHROME_DISABLED_BORDER;
	}
	if (pressed) {
		return CHROME_BORDER_ACTIVE;
	}
	return hovered ? CHROME_BORDER_HOVER : CHROME_FIELD_BORDER;
}

bool WebInput::_is_text_family() const {
	switch (input_type) {
		case TYPE_TEXT:
		case TYPE_PASSWORD:
		case TYPE_EMAIL:
		case TYPE_URL:
		case TYPE_TEL:
		case TYPE_SEARCH:
		case TYPE_NUMBER:
			return true;
		default:
			return false;
	}
}

Ref<Font> WebInput::_get_font() const {
	// A theme `font` override wins; otherwise the bundled Arial (matching
	// Chromium's default form-control font on Windows). The theme entry is left
	// empty by the default theme, but Godot hands back ThemeDB's fallback font
	// in that case, and that must not shadow the user-agent font.
	if (theme_cache.font.is_valid() && theme_cache.font != ThemeDB::get_singleton()->get_fallback_font()) {
		return theme_cache.font;
	}
	if (ua_font.is_valid()) {
		return ua_font;
	}
	return get_theme_default_font();
}

// Chromium sizes text fields from the primary font's metrics (Arial, in
// units of the em): the rounded average and maximum advances for the width,
// and the rounded `normal` line height (ascent + descent + line gap) for the
// height. At the 13.3333px UA size that is avg 7, max 36, line 15.
static const real_t ARIAL_AVG_EM = 1033.0 / 2048.0;
static const real_t ARIAL_MAX_EM = 5457.0 / 2048.0;
static const real_t ARIAL_LINE_EM = (1854.0 + 434.0 + 67.0) / 2048.0;
static const real_t NUMBER_SPIN_WIDTH = 15;


// The `head` table's glyph bounding box width in em (xMax - xMin) / unitsPerEm
// of the file behind a Font -- Blink's max char width on Windows -- or 0 when
// the font has no readable SFNT data (a WOFF, a missing file).
static real_t _wi_head_bbox_em(const PackedByteArray &p_data, int p_face) {
	const int64_t n = p_data.size();
	if (n < 12) {
		return 0;
	}
	const uint8_t *d = p_data.ptr();
	auto be16 = [](const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); };
	auto be32 = [](const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3]; };
	uint32_t base = 0;
	if (d[0] == 't' && d[1] == 't' && d[2] == 'c' && d[3] == 'f') {
		uint32_t count = be32(d + 8);
		if (p_face < 0 || (uint32_t)p_face >= count || 12 + 4 * (int64_t)(p_face + 1) > n) {
			return 0;
		}
		base = be32(d + 12 + 4 * p_face);
	}
	if ((int64_t)base + 12 > n) {
		return 0;
	}
	uint16_t tables = be16(d + base + 4);
	for (uint16_t i = 0; i < tables; i++) {
		int64_t rec = base + 12 + 16 * (int64_t)i;
		if (rec + 16 > n) {
			return 0;
		}
		const uint8_t *t = d + rec;
		uint32_t off = be32(t + 8);
		if (t[0] != 'h' || t[1] != 'e' || t[2] != 'a' || t[3] != 'd' || (int64_t)off + 44 > n) {
			continue;
		}
		const uint8_t *head = d + off;
		int upm = be16(head + 18);
		int x_min = (int16_t)be16(head + 36);
		int x_max = (int16_t)be16(head + 40);
		return upm > 0 ? (real_t)(x_max - x_min) / upm : 0;
	}
	return 0;
}

// The bbox width of a Font's primary face, through its variations: a
// FontFile's own data, else the installed file of a SystemFont.
static real_t _wi_font_max_char_em(const Ref<Font> &p_font) {
	static HashMap<ObjectID, real_t> cache;
	if (p_font.is_null()) {
		return 0;
	}
	if (const real_t *found = cache.getptr(p_font->get_instance_id())) {
		return *found;
	}
	Ref<Font> source = p_font;
	for (int depth = 0; depth < 4; depth++) {
		Ref<FontVariation> variation = source;
		if (variation.is_null() || variation->get_base_font().is_null()) {
			break;
		}
		source = variation->get_base_font();
	}
	real_t em = 0;
	Ref<FontFile> file = source;
	Ref<SystemFont> system = source;
	if (file.is_valid()) {
		em = _wi_head_bbox_em(file->get_data(), (int)file->get_face_index(0));
	} else if (system.is_valid() && !system->get_font_names().is_empty()) {
		String path = OS::get_singleton()->get_system_font_path(system->get_font_names()[0], system->get_font_weight(), system->get_font_stretch(), system->get_font_italic());
		if (!path.is_empty()) {
			em = _wi_head_bbox_em(FileAccess::get_file_as_bytes(path), 0);
		}
	}
	cache[p_font->get_instance_id()] = em;
	return em;
}

Size2 WebInput::_text_field_content_size(real_t p_font_size) const {
	// A number field ignores `size` and, when both bounds are finite, sizes
	// itself to the widest min/max/step text plus its spin button.
	int chars = input_type == TYPE_NUMBER ? 20 : MAX(1, size_chars);
	int number_chars = 0;
	bool number_sized = input_type == TYPE_NUMBER && _number_preferred_size(number_chars);
	if (number_sized) {
		chars = number_chars;
	}
	// Blink sizes the field from the primary face: chars x the average
	// character plus (widest - average), ceiled to a whole pixel. The average
	// is the advance of 'x' (DirectWrite reports no xAvgCharWidth), taken up to
	// the whole pixel when it rounds up and kept fractional when it rounds
	// down; the widest is the rounded `head` bounding box. Arial 13.3333px: 7
	// and 36 -> 169; Arial Bold: 7.415 and 35 -> 176 (fitted on 15 faces x 8
	// sizes, 119/120; Courier New Bold 13.33px hints 'x' to 8.0).
	Ref<Font> face = _resolved_font();
	real_t avg_em = ARIAL_AVG_EM;
	real_t max_em = ARIAL_MAX_EM;
	if (face.is_valid()) {
		const int ref = 1024;
		real_t x = face->get_char_size('x', ref).x;
		Ref<FontVariation> variation = face;
		if (variation.is_valid()) {
			x -= variation->get_spacing(TextServer::SPACING_GLYPH); // letter-spacing is not the face's.
		}
		if (x > 0) {
			avg_em = x / ref;
		}
		real_t bbox = _wi_font_max_char_em(face);
		if (bbox > 0) {
			max_em = bbox;
		}
	}
	real_t x = avg_em * p_font_size;
	real_t avg = MAX(x, (real_t)Math::round(x));
	real_t max = Math::round(max_em * p_font_size);
	real_t width = Math::ceil(avg * chars + (max - avg) - 1e-3) + (number_sized ? NUMBER_SPIN_WIDTH : 0);
	// The inner editor is one `normal` line of the field's own face (Arial
	// Black for weight 800+, an author family), not always Arial's.
	return Size2(width, _line_box(_resolved_font(), String(), p_font_size).height());
}

// Digits before the decimal point, counting a minus sign (Chromium's
// LengthBeforeDecimalPoint).
static int _number_length_before_point(double p_value) {
	int length = 1;
	for (double v = Math::floor(Math::abs(p_value)); v >= 10; v = Math::floor(v / 10)) {
		length++;
	}
	return length + (p_value < 0 ? 1 : 0);
}

// Digits after the decimal point in the attribute's canonical form.
static int _number_decimal_places(const String &p_text) {
	String text = p_text.strip_edges();
	int dot = text.find(".");
	if (dot < 0 || text.contains("e") || text.contains("E")) {
		return 0;
	}
	String fraction = text.substr(dot + 1);
	while (fraction.ends_with("0")) {
		fraction = fraction.substr(0, fraction.length() - 1);
	}
	return fraction.length();
}

bool WebInput::_number_preferred_size(int &r_chars) const {
	// Chromium NumberInputType::SizeShouldIncludeDecoration: only a field with
	// finite min and max (and a step other than "any") sizes to its range.
	if (step_attr.strip_edges().to_lower() == "any") {
		return false;
	}
	if (!min_attr.strip_edges().is_valid_float() || !max_attr.strip_edges().is_valid_float()) {
		return false;
	}
	double minimum = min_attr.to_float();
	double maximum = max_attr.to_float();
	int before = MAX(_number_length_before_point(minimum), _number_length_before_point(maximum));
	int after = MAX(MAX(_number_decimal_places(min_attr), _number_decimal_places(max_attr)),
			step_attr.strip_edges().is_valid_float() ? _number_decimal_places(step_attr) : 0);
	r_chars = before + (after > 0 ? after + 1 : 0);
	return true;
}

Ref<Font> WebInput::_label_font() const {
	// Same fallback-substitution guard as _get_font().
	if (theme_cache.label_font.is_valid() && theme_cache.label_font != ThemeDB::get_singleton()->get_fallback_font()) {
		return theme_cache.label_font;
	}
	return _get_font();
}

Ref<Font> WebInput::_resolved_font() const {
	if (css_font.is_valid()) {
		return css_font;
	}
	return _get_font();
}

Ref<Font> WebInput::_make_ua_font(const String &p_path, const String &p_system_name) const {
	// Prefer the real font file (exact glyphs + browser-matching metrics).
	Ref<FontFile> ff;
	ff.instantiate();
	if (ff->load_dynamic_font(p_path) == OK) {
		// Chromium lays text out with unhinted, sub-pixel positioned advances;
		// Godot's default hinting rounds each advance to a whole pixel, which
		// makes every string a few percent wider than the browser's.
		ff->set_hinting(TextServer::HINTING_NONE);
		ff->set_subpixel_positioning(TextServer::SUBPIXEL_POSITIONING_ONE_QUARTER);
		return ff;
	}
	Ref<SystemFont> sf;
	sf.instantiate();
	PackedStringArray names;
	names.push_back(p_system_name);
	sf->set_font_names(names);
	return sf;
}

void WebInput::_rebuild_css_font() {
	// Effective weight/spacing: a CSS author value wins, else the theme constant.
	date_edit_width_cache = -1;
	date_line_height_cache = -1; // typography change: re-measure the date editor.
	int eff_weight = css.has_font_weight ? css.font_weight : theme_cache.font_weight;
	real_t eff_spacing = css.has_letter_spacing ? css.letter_spacing : (real_t)theme_cache.letter_spacing;
	bool need = !css.font_family.is_empty() || eff_weight != 400 || eff_spacing != 0.0;
	if (!need) {
		css_font = Ref<Font>();
		return;
	}
	Ref<FontVariation> fv;
	fv.instantiate();
	// A host that resolves CSS fonts itself hands the face over as the theme
	// `font` -- already at the right weight, with the fallback faces the
	// browser would use for scripts it lacks. That face wins; the family list
	// is only resolved here when no such face was given.
	bool themed = theme_cache.font.is_valid() && theme_cache.font != ThemeDB::get_singleton()->get_fallback_font();
	Ref<Font> base = themed ? theme_cache.font : ua_font;
	bool own_face = false;
	if (!themed && !css.font_family.is_empty()) {
		Ref<SystemFont> sf;
		sf.instantiate();
		PackedStringArray names;
		Vector<String> raw = css.font_family.split(",");
		for (const String &n : raw) {
			String name = n.strip_edges().trim_prefix("\"").trim_suffix("\"").trim_prefix("'").trim_suffix("'");
			if (!name.is_empty()) {
				names.push_back(name);
			}
		}
		sf->set_font_names(names);
		sf->set_font_weight(eff_weight);
		base = sf;
		own_face = true;
	}
	fv->set_base_font(base);
	if (eff_weight != 400 && !themed) {
		Dictionary coords; // works on variable fonts.
		coords["wght"] = eff_weight;
		fv->set_variation_opentype(coords);
		if (!own_face) {
			// Faux bold/thin for the bundled UA face, so weight shows.
			fv->set_variation_embolden(CLAMP((eff_weight - 400) / 400.0, -0.5, 1.0));
		}
	}
	if (eff_spacing != 0.0) {
		fv->set_spacing(TextServer::SPACING_GLYPH, (int)Math::round(eff_spacing));
	}
	css_font = fv;
}

String WebInput::_default_value_for_type() const {
	switch (input_type) {
		case TYPE_SUBMIT:
			return _ua_text("submit");
		case TYPE_RESET:
			return _ua_text("reset");
		case TYPE_BUTTON:
			return "";
		default:
			return "";
	}
}

String WebInput::_display_text() const {
	if (input_type == TYPE_BUTTON || input_type == TYPE_SUBMIT || input_type == TYPE_RESET) {
		return value.is_empty() ? _default_value_for_type() : value;
	}
	return value;
}

WebInput::BoxModel WebInput::_compute_box_model() const {
	BoxModel b;
	// The theme items are the user-agent base (CSS author values override them).
	b.font_size = theme_cache.font_size > 0 ? (real_t)theme_cache.font_size : CHROME_FONT_SIZE;
	b.text_color = theme_cache.font_color;
	b.font_weight = theme_cache.font_weight;
	b.letter_spacing = theme_cache.letter_spacing;
	b.line_height = theme_cache.line_height;
	b.text_align = theme_cache.text_align;

	switch (input_type) {
		case TYPE_TEXT:
		case TYPE_PASSWORD:
		case TYPE_EMAIL:
		case TYPE_URL:
		case TYPE_TEL:
		case TYPE_NUMBER:
		case TYPE_SEARCH: {
			// Chromium: ceil(avg_char * size) + (max_char - avg_char), with Arial
			// 13.3333px's rounded metrics avg 7 / max 36 -> 169x15 for size=20;
			// border 2, padding v1/h2 -> 177x21. A number field ignores `size`
			// and, when both bounds are finite, sizes itself to the widest
			// min/max/step text plus its 15px spin button.
			real_t content_w = _text_field_content_size(CHROME_FONT_SIZE).x;
			b.border[0] = b.border[1] = b.border[2] = b.border[3] = 2;
			// Native paints a thin ~1px flat edge, not the 2px CSS bevel.
			b.draw_border[0] = b.draw_border[1] = b.draw_border[2] = b.draw_border[3] = 1;
			b.padding[0] = b.padding[2] = 1;
			b.padding[1] = b.padding[3] = 2;
			b.border_box = Size2(content_w + 4 + 4, _text_field_content_size(CHROME_FONT_SIZE).y + 4 + 2);
			b.background = CHROME_TEXT_BG;
			b.text_color = theme_cache.font_color;
			// The field border darkens under the pointer, like the native one.
			b.border_color = disabled ? CHROME_DISABLED_BORDER
									  : (hovered ? CHROME_BORDER_HOVER : CHROME_FIELD_BORDER);
			b.border_inset = false;
			if (disabled) {
				b.background = CHROME_DISABLED_BG;
				b.text_color = CHROME_DISABLED_TEXT;
			}
		} break;

		case TYPE_BUTTON:
		case TYPE_SUBMIT:
		case TYPE_RESET: {
			b.border[0] = b.border[1] = b.border[2] = b.border[3] = 2;
			// Native button paints a thin flat gray edge over the #f0f0f0 face.
			b.draw_border[0] = b.draw_border[1] = b.draw_border[2] = b.draw_border[3] = 1;
			b.padding[0] = b.padding[2] = 1;
			b.padding[1] = b.padding[3] = 6;
			// The caption and its `normal` line box follow the author font-size
			// (Chromium lays a push button out like any inline-block of text).
			Ref<Font> f = _resolved_font();
			real_t fs = _date_font_size(); // author size, else the exact 13.3333px UA size.
			real_t tw = _date_text_width(f, _display_text(), fs);
			b.border_box = Size2(tw + 12 + 4, _line_box(f, _display_text(), fs).height() + 2 + 4);
			b.background = _state_face_color();
			b.text_color = disabled ? CHROME_DISABLED_TEXT : theme_cache.font_color;
			b.border_color = CHROME_BUTTON_BORDER; // CSS computed: black.
			b.draw_border_color = _state_border_color(); // native paints flat gray.
			b.draw_border_color_set = true;
			b.border_outset = false;
			b.text_align = (int)HORIZONTAL_ALIGNMENT_CENTER; // UA default for buttons.
		} break;

		case TYPE_CHECKBOX:
		case TYPE_RADIO: {
			// Native appearance: structural border/padding are 0, bg transparent.
			b.border_box = Size2(13, 13);
			b.background = Color(0, 0, 0, 0);
			b.text_color = theme_cache.font_color;
			b.border_color = CHROME_FIELD_BORDER;
		} break;

		case TYPE_RANGE: {
			b.border_box = Size2(129, 16);
			b.background = CHROME_TEXT_BG;
			b.text_color = Color(16.0 / 255, 16.0 / 255, 16.0 / 255, 1);
			b.border_color = b.text_color;
		} break;

		case TYPE_COLOR: {
			b.border[0] = b.border[1] = b.border[2] = b.border[3] = 1;
			b.padding[0] = b.padding[2] = 1;
			b.padding[1] = b.padding[3] = 2;
			b.border_box = Size2(50, 27);
			b.background = _state_face_color();
			b.text_color = theme_cache.font_color;
			b.border_color = CHROME_BUTTON_BORDER; // CSS computed: black.
			b.draw_border_color = _state_border_color(); // native paints gray.
			b.draw_border_color_set = true;
		} break;

		case TYPE_FILE: {
			// A fixed 253px row; its height is the button's (caption line box
			// + 1px padding + 2px border, top and bottom).
			b.border_box = Size2(253, _line_box(_resolved_font(), _ua_text("choose_file"), _date_font_size()).height() + 2 + 4);
			b.background = Color(0, 0, 0, 0);
			b.text_color = theme_cache.font_color;
			b.border_color = CHROME_BUTTON_BORDER;
		} break;

		case TYPE_IMAGE: {
			// Without a loaded image the box is the broken-image placeholder:
			// a 16px icon followed by the alt text, like the browser.
			real_t alt_w = 0;
			if (!src_texture.is_valid()) {
				alt_w = 16 + _date_text_width(_resolved_font(), _image_fallback_text(), _date_font_size());
			}
			real_t w = image_width >= 0 ? (real_t)image_width
									    : (src_texture.is_valid() ? (real_t)src_texture->get_width() : alt_w);
			// The alt text's line box: its `line-height` when set, never shorter than the icon.
			real_t alt_line = css.has_line_height ? css.line_height : _line_box(_resolved_font(), _image_fallback_text(), _date_font_size()).height();
			real_t alt_h = alt_w > 0 ? MAX((real_t)16.0, alt_line) : 0.0;
			real_t h = image_height >= 0 ? (real_t)image_height
										 : (src_texture.is_valid() ? (real_t)src_texture->get_height() : alt_h);
			b.border_box = Size2(w, h);
			b.background = Color(0, 0, 0, 0);
		} break;

		case TYPE_HIDDEN: {
			b.border_box = Size2(0, 0);
			b.background = Color(0, 0, 0, 0);
		} break;

		case TYPE_DATE:
		case TYPE_TIME:
		case TYPE_MONTH:
		case TYPE_WEEK:
		case TYPE_DATETIME_LOCAL: {
			b.border[0] = b.border[1] = b.border[2] = b.border[3] = 2;
			b.draw_border[0] = b.draw_border[1] = b.draw_border[2] = b.draw_border[3] = 1;
			b.padding[3] = 1; // padding-left 1px.
			// Chromium sizes these editors from their content: the widest text
			// every field can hold, plus the separators and the picker
			// indicator (whose square box also sets the control's height).
			real_t ind = _date_indicator_size();
			// The clock indicator keeps an 8px gap from the fields; the calendar
			// one sits right after them (Chrome 154).
			real_t ind_gap = _is_calendar_family() ? 0.0 : 8.0;
			real_t content_w = _date_edit_width() + ind_gap + ind;
			real_t line_h = _date_line_height();
			b.border_box = Size2(5 + content_w, 4 + MAX(ind, line_h));
			b.background = CHROME_TEXT_BG;
			b.text_color = theme_cache.font_color;
			b.border_color = CHROME_FIELD_BORDER;
			b.border_inset = false;
			if (disabled) {
				b.background = CHROME_DISABLED_BG;
				b.text_color = CHROME_DISABLED_TEXT;
				b.border_color = CHROME_DISABLED_BORDER;
				b.draw_border_color = CHROME_DISABLED_BORDER;
				b.draw_border_color_set = true;
			}
		} break;

		default:
			break;
	}
	b.intrinsic_box = b.border_box;
	_apply_css_overrides(b);
	return b;
}

void WebInput::_apply_css_overrides(BoxModel &b) const {
	// Native widgets (checkbox/radio): in Chromium, background-color/color still
	// compute through (so getComputedStyle reports them) but border/padding/
	// radius are UA-reset to 0, the box stays fixed, and the native glyph paints
	// over the background. Mirror that: record bg/color for the metrics, keep
	// the geometry native, and let _draw_checkbox ignore the fill.
	if (input_type == TYPE_CHECKBOX || input_type == TYPE_RADIO) {
		if (css.has_bg) {
			b.background = css.bg;
		}
		if (css.has_color) {
			b.text_color = css.color;
		}
		// Border/padding/radius stay user-agent, but an explicit size applies:
		// Chromium stretches the box and centres a square glyph inside it.
		if (css.has_width || input_width >= 0) {
			b.border_box.x = css.has_width ? css.width : (real_t)input_width;
		}
		if (css.has_height || input_height >= 0) {
			b.border_box.y = css.has_height ? css.height : (real_t)input_height;
		}
		return;
	}

	// Content size implied by the UA box (before applying overrides).
	real_t content_w = b.border_box.x - (b.border[1] + b.border[3]) - (b.padding[1] + b.padding[3]);
	real_t content_h = b.border_box.y - (b.border[0] + b.border[2]) - (b.padding[0] + b.padding[2]);

	// A text input's intrinsic size scales with font-size (its width is `size`
	// average glyph advances), so author font-size grows the box like Chromium.
	bool font_scaled = false;
	if (css.has_font_size && _is_text_family() && css.font_size > 0) {
		Size2 content = _text_field_content_size(css.font_size);
		content_w = content.x;
		content_h = content.y;
		font_scaled = true;
	}

	for (int i = 0; i < 4; i++) {
		if (css.has_bw[i]) {
			b.border[i] = css.bw[i];
			b.draw_border[i] = css.bw[i];
		}
		if (css.has_pad[i]) {
			b.padding[i] = css.pad[i];
		}
		if (css.has_radius[i]) {
			b.radius[i] = css.radius[i];
		}
	}
	if (css.border_none) {
		for (int i = 0; i < 4; i++) {
			b.border[i] = 0;
			b.draw_border[i] = 0;
		}
	}
	if (css.has_bg) {
		b.background = css.bg;
	}
	if (css.has_bcol) {
		b.border_color = css.bcol;
		b.draw_border_color = css.bcol;
		b.draw_border_color_set = true;
		b.border_inset = false;
		b.border_outset = false;
	}
	if (css.has_shadow) {
		b.has_shadow = true;
		b.shadow_color = css.shadow_color;
		b.shadow_offset = css.shadow_offset;
		b.shadow_blur = css.shadow_blur;
		b.shadow_spread = css.shadow_spread;
	}
	if (css.has_color) {
		b.text_color = css.color;
	}
	if (css.has_font_size) {
		b.font_size = css.font_size;
	}
	if (css.has_font_weight) {
		b.font_weight = css.font_weight;
	}
	if (css.has_line_height) {
		b.line_height = css.line_height;
		if (_is_text_family()) {
			// Chromium's inner editor is never shorter than one normal line.
			content_h = MAX(content_h, css.line_height);
		} else if (_is_datetime_family()) {
			// The date/time fields' line box is the line-height plus their 1px
			// padding above and below; only the picker indicator sets a floor
			// (not a normal line): 13.33px Arial with line-height:10px is 17.33px
			// tall inside, the calendar indicator (Chrome 154).
			content_h = MAX(css.line_height + 2, _date_indicator_size());
		} else if (input_type != TYPE_SUBMIT && input_type != TYPE_RESET && input_type != TYPE_BUTTON
				&& input_type != TYPE_CHECKBOX && input_type != TYPE_RADIO && input_type != TYPE_RANGE && input_type != TYPE_COLOR) {
			content_h = css.line_height; // line box drives the content height.
		}
		// Push buttons, check boxes, radios, sliders and colour swatches keep
		// their UA size whatever the line-height; the label's line box still
		// grows around them.
	}
	if (css.has_text_align) {
		b.text_align = css.text_align;
	}
	if (css.has_letter_spacing) {
		b.letter_spacing = css.letter_spacing;
	}

	// Recompute the outer border-box. content-box (CSS default for text inputs)
	// grows with border/padding/line-height; border-box keeps the outer size.
	bool touched = css.border_none || css.has_line_height || font_scaled;
	for (int i = 0; i < 4; i++) {
		touched = touched || css.has_bw[i] || css.has_pad[i];
	}
	// box-sizing: a CSS override wins, then the theme constant when it asks for
	// border-box, otherwise the per-type user-agent default.
	bool border_box_sizing = css.has_box_sizing ? css.border_box
												: (theme_cache.box_sizing == 1 || _ua_border_box());
	real_t extra_w = b.border[1] + b.border[3] + b.padding[1] + b.padding[3];
	real_t extra_h = b.border[0] + b.border[2] + b.padding[0] + b.padding[2];

	// Explicit size: CSS width/height wins, else the input_width/input_height
	// node properties. It sets the box per box-sizing (border-box => the value
	// already includes padding+border; content-box => add them). Without an
	// explicit size, padding/border grow the box (auto width), which is what the
	// browser does in BOTH box-sizing modes.
	bool has_w = css.has_width || input_width >= 0;
	real_t w_val = css.has_width ? css.width : (real_t)input_width;
	bool has_h = css.has_height || input_height >= 0;
	real_t h_val = css.has_height ? css.height : (real_t)input_height;
	if (has_w) {
		b.border_box.x = border_box_sizing ? w_val : w_val + extra_w;
	} else if (touched) {
		b.border_box.x = content_w + extra_w;
	}
	if (has_h) {
		b.border_box.y = border_box_sizing ? h_val : h_val + extra_h;
	} else if (touched) {
		b.border_box.y = content_h + extra_h;
	}
}

Ref<StyleBoxFlat> WebInput::_make_style_box(const BoxModel &b) const {
	Ref<StyleBoxFlat> sb;
	sb.instantiate();
	sb->set_bg_color(b.background);
	real_t dbw[4];
	for (int i = 0; i < 4; i++) {
		dbw[i] = b.draw_border[i] >= 0 ? b.draw_border[i] : b.border[i];
	}
	sb->set_border_width(SIDE_TOP, (int)Math::round(dbw[0]));
	sb->set_border_width(SIDE_RIGHT, (int)Math::round(dbw[1]));
	sb->set_border_width(SIDE_BOTTOM, (int)Math::round(dbw[2]));
	sb->set_border_width(SIDE_LEFT, (int)Math::round(dbw[3]));
	sb->set_border_color(b.draw_border_color_set ? b.draw_border_color : b.border_color);
	sb->set_corner_radius(CORNER_TOP_LEFT, (int)Math::round(b.radius[0]));
	sb->set_corner_radius(CORNER_TOP_RIGHT, (int)Math::round(b.radius[1]));
	sb->set_corner_radius(CORNER_BOTTOM_RIGHT, (int)Math::round(b.radius[2]));
	sb->set_corner_radius(CORNER_BOTTOM_LEFT, (int)Math::round(b.radius[3]));
	if (b.has_shadow) {
		sb->set_shadow_color(b.shadow_color);
		sb->set_shadow_size((int)Math::round(b.shadow_blur + b.shadow_spread));
		sb->set_shadow_offset(b.shadow_offset);
	}
	// Antialias only when rounded; crisp axis-aligned edges otherwise (keeps the
	// pixel-perfect default field/border parity).
	bool rounded = b.radius[0] > 0 || b.radius[1] > 0 || b.radius[2] > 0 || b.radius[3] > 0;
	sb->set_anti_aliased(rounded);
	return sb;
}

Size2 WebInput::get_minimum_size() const {
	return _compute_layout(_compute_box_model()).total;
}

Control::CursorShape WebInput::_cursor_for_type() const {
	// HTML UA cursors: text-entry types -> text (I-beam); image -> pointer (hand);
	// everything else -> default (arrow).
	if (_is_text_family()) {
		return CURSOR_IBEAM;
	}
	if (input_type == TYPE_IMAGE) {
		return CURSOR_POINTING_HAND;
	}
	return CURSOR_ARROW;
}

// Chromium's `normal` line box on Windows is built from the font's usWin
// metrics; for the user-agent font (Arial) that is 0.905em above the baseline
// and 0.212em below it, giving the 15px label line the browser lays out.
static const real_t UA_LINE_ASCENT = 0.905;
static const real_t UA_LINE_DESCENT = 0.212;
static const real_t UA_LINE_GAP = 67.0 / 2048.0;

real_t WebInput::_label_font_size() const {
	// As with _date_font_size(), the theme carries the user-agent default as a
	// whole 13, which stands for Chromium's fractional 13.3333px.
	if (theme_cache.label_font_size > 0 && theme_cache.label_font_size != (int)Math::round(CHROME_FONT_SIZE)) {
		return (real_t)theme_cache.label_font_size;
	}
	return CHROME_FONT_SIZE;
}

WebInput::Layout WebInput::_compute_layout(const BoxModel &b) const {
	Layout L;
	L.input_origin = Point2();
	L.label_pos = Point2();
	L.total = b.border_box;
	L.has_label = !label_text.is_empty();
	if (!L.has_label) {
		return L;
	}
	// A wrapping <label> is laid out as one inline line box: the input sits on
	// the label text's baseline. Types that carry no text of their own put that
	// baseline at their bottom edge, so the label's descender hangs below them;
	// the rest have an internal baseline one descent above their bottom.
	Ref<Font> f = _label_draw_font();
	real_t lfs = _label_font_size();
	int fs = (int)Math::round(lfs);
	Size2 ls = f.is_valid() ? f->get_string_size(label_text, HORIZONTAL_ALIGNMENT_LEFT, -1, fs) : Size2();
	// Fractional advances like the browser, measured on the unspaced face:
	// letter-spacing is a CSS px length after every character, not a glyph
	// spacing that scales with the measuring size.
	ls.x = _date_text_width(_label_font(), label_text, lfs) + label_letter_spacing * label_text.length();
	// Blink rounds the primary font's ascent and descent to whole pixels
	// before building the line box (12 + 3 for the 13.3333px UA font).
	// `line-height: normal` adds the rounded line gap, split as half-leading.
	// A Han label grows its line box with the fallback face, like any text;
	// a set line-height replaces `normal` (half the leading above, floored).
	LineBox lb = _line_box(f, label_text, lfs, !css.has_line_height);
	if (css.has_line_height) {
		real_t leading = css.line_height - lb.height();
		real_t above = Math::floor(leading * 0.5);
		lb.ascent += above;
		lb.descent += leading - above;
	}
	real_t ascent = lb.ascent;
	real_t descent = lb.descent;
	real_t line_h = ascent + descent;
	real_t glyph_ascent = f.is_valid() ? f->get_ascent(fs) : ascent;
	real_t iw = b.border_box.x;
	real_t ih = b.border_box.y;
	// The UA margins of the native widgets (html.css) sit inside the label.
	// Without a label the node box is the control's border box, and the
	// renderer pins margin:0 on it to keep that true in the browser.
	real_t m[4] = { 0, 0, 0, 0 }; // top, right, bottom, left.
	if (input_type == TYPE_CHECKBOX) {
		m[0] = 3, m[1] = 3, m[2] = 3, m[3] = 4;
	} else if (input_type == TYPE_RADIO) {
		m[0] = 3, m[1] = 3, m[2] = 0, m[3] = 5;
	} else if (input_type == TYPE_RANGE) {
		m[0] = m[1] = m[2] = m[3] = 2;
	}
	real_t ow = iw + m[1] + m[3];
	real_t oh = ih + m[0] + m[2];

	// Where the shared baseline sits inside the input. Controls that show text
	// put it on their first text line -- which is centred in the content box, so
	// it follows an explicit height; controls that show none align their bottom
	// edge to it instead.
	real_t input_baseline;
	if (input_type == TYPE_CHECKBOX || input_type == TYPE_RADIO || input_type == TYPE_RANGE) {
		input_baseline = ih;
	} else if (input_type == TYPE_COLOR) {
		// The swatch wrapper's own padding puts the colour input's baseline six
		// pixels above its bottom edge.
		input_baseline = MAX((real_t)0.0, ih - 6 * (b.font_size / CHROME_FONT_SIZE));
	} else {
		input_baseline = _input_text_baseline(b);
	}
	// Measured from the top margin edge from here on.
	input_baseline += m[0];
	real_t above = MAX(ascent, input_baseline);
	real_t inline_h = above + MAX(descent, oh - input_baseline);
	real_t input_y = above - input_baseline + m[0];

	LabelPosition position = label_position;
	if (label_wrap_width >= 0 && ls.x + ow > label_wrap_width + 0.01) {
		// No room for both on one line: the browser breaks between the text
		// and the control, so the pair stacks.
		if (position == LABEL_LEFT) {
			position = LABEL_TOP;
		} else if (position == LABEL_RIGHT) {
			position = LABEL_BOTTOM;
		}
	}
	bool wrapped = position != label_position;
	switch (position) {
		case LABEL_LEFT: {
			L.label_pos = Point2(0, above - glyph_ascent);
			L.input_origin = Point2(ls.x + m[3], input_y);
			L.total = Size2(ls.x + ow, inline_h);
		} break;
		case LABEL_RIGHT: {
			L.input_origin = Point2(m[3], input_y);
			L.label_pos = Point2(ow, above - glyph_ascent);
			L.total = Size2(ow + ls.x, inline_h);
		} break;
		case LABEL_TOP: {
			// The input gets a line box of its own, which still carries the
			// block's strut -- so a control with no text baseline keeps the
			// descender space under it here too.
			L.label_pos = Point2(0, ascent - glyph_ascent);
			L.input_origin = Point2(m[3], line_h + input_y);
			L.total = Size2(MAX(ls.x, ow), line_h + inline_h);
		} break;
		case LABEL_BOTTOM: {
			L.input_origin = Point2(m[3], input_y);
			L.label_pos = Point2(0, inline_h + ascent - glyph_ascent);
			L.total = Size2(MAX(ls.x, ow), inline_h + line_h);
		} break;
	}
	if (wrapped) {
		// The label box is as wide as the space it was given (its widest
		// unbreakable piece when that is wider).
		L.total.x = MAX(L.total.x, label_wrap_width);
	}
	if (position == LABEL_TOP || position == LABEL_BOTTOM) {
		// Stacked, the text and the control are lines of their own, so
		// text-align places each in the label box -- the clamped one under a
		// max-width -- and a line wider than the box starts at its start edge
		// and overflows the end (CSS Text 3).
		int align = label_alignment >= 0 ? label_alignment : (b.text_align >= 0 ? b.text_align : (int)HORIZONTAL_ALIGNMENT_LEFT);
		real_t f_align = align == (int)HORIZONTAL_ALIGNMENT_CENTER ? 0.5 : align == (int)HORIZONTAL_ALIGNMENT_RIGHT ? 1.0 : 0.0;
		real_t box = label_max_width >= 0 ? MIN(L.total.x, label_max_width) : L.total.x;
		if (f_align > 0.0) {
			L.label_pos.x = MAX((real_t)0.0, (box - ls.x) * f_align);
			L.input_origin.x = MAX((real_t)0.0, (box - ow) * f_align) + m[3];
		}
	}
	return L;
}

// Blink's `line-height: normal` line box: every face the text draws with
// contributes its rounded ascent and descent plus its rounded line gap (split
// floor-half above), and the line spans the union of them. A Han caption or
// placeholder drawn with the locale's fallback face (the canvas fonts carry it
// as an explicit fallback) is therefore taller than Arial alone.
Ref<Font> WebInput::_label_draw_font() const {
	Ref<Font> base = _label_font();
	int spacing = (int)Math::round(label_letter_spacing);
	if (base.is_null() || spacing == 0) {
		return base;
	}
	if (label_spacing_font.is_null() || label_spacing_base != base || label_spacing_px != spacing) {
		label_spacing_font.instantiate();
		label_spacing_font->set_base_font(base);
		label_spacing_font->set_spacing(TextServer::SPACING_GLYPH, spacing);
		label_spacing_base = base;
		label_spacing_px = spacing;
	}
	return label_spacing_font;
}

WebInput::LineBox WebInput::_line_box(const Ref<Font> &p_font, const String &p_text, real_t p_font_size, bool p_with_gap) const {
	LineBox box;
	if (p_font.is_null()) {
		box.ascent = Math::round(UA_LINE_ASCENT * p_font_size);
		box.descent = Math::round(UA_LINE_DESCENT * p_font_size);
		return box;
	}
	TypedArray<RID> rids = p_font->get_rids();
	if (rids.is_empty()) {
		return box;
	}
	const int ref = 1024; // FreeType rounds metrics to whole pixels; negligible here.
	auto add_face = [&](const RID &p_rid, real_t p_gap_em) {
		real_t a = Math::round(TS->font_get_ascent(p_rid, ref) / ref * p_font_size);
		real_t d = Math::round(TS->font_get_descent(p_rid, ref) / ref * p_font_size);
		real_t g = p_with_gap ? Math::round(p_gap_em * p_font_size) : 0.0;
		real_t above = Math::floor(g * 0.5);
		box.ascent = MAX(box.ascent, a + above);
		box.descent = MAX(box.descent, d + g - above);
	};
	RID primary = rids[0];
	// TextServer does not report a face's line gap; the UA face's is known.
	add_face(primary, TS->font_get_name(primary) == "Arial" ? UA_LINE_GAP : 0.0);
	HashSet<RID> used;
	used.insert(primary);
	for (int i = 0; i < p_text.length(); i++) {
		char32_t c = p_text[i];
		if (c < 0x80 || TS->font_has_char(primary, c)) {
			continue;
		}
		for (int j = 1; j < rids.size(); j++) {
			RID fallback = rids[j];
			if (TS->font_has_char(fallback, c)) {
				if (!used.has(fallback)) {
					used.insert(fallback);
					add_face(fallback, 0.0);
				}
				break;
			}
		}
	}
	return box;
}

real_t WebInput::_input_text_baseline(const BoxModel &b) const {
	real_t ih = b.border_box.y;
	real_t ct = b.border[0] + b.padding[0];
	real_t ch = ih - b.border[0] - b.border[2] - b.padding[0] - b.padding[2];
	real_t fs = _date_font_size();
	if (_is_datetime_family()) {
		// The fields wrapper is centred in the content box and pads its line
		// box by a pixel.
		if (css.has_line_height) {
			// A set line-height is the line box: the primary face's ascent and
			// descent sit in it with the half-leading floored above, and a
			// fallback face (the Han placeholder) no longer grows it.
			LineBox primary = _line_box(_date_font(), String(), fs, false);
			real_t top = ct + (ch - (css.line_height + 2)) * 0.5 + 1;
			return top + Math::floor((css.line_height - primary.height()) * 0.5) + primary.ascent;
		}
		LineBox lb = _date_line_box();
		return ct + (ch - (lb.height() + 2)) * 0.5 + 1 + lb.ascent;
	}
	if (input_type == TYPE_FILE) {
		LineBox lb = _line_box(_resolved_font(), _ua_text("choose_file"), fs);
		return 2 + 1 + lb.ascent;
	}
	if (input_type == TYPE_IMAGE) {
		return _line_box(_resolved_font(), _image_fallback_text(), fs).ascent;
	}
	String text = (input_type == TYPE_BUTTON || input_type == TYPE_SUBMIT || input_type == TYPE_RESET) ? _display_text() : String();
	LineBox lb = _line_box(_resolved_font(), text, b.font_size);
	return ct + MAX((real_t)0.0, (ch - lb.height()) * 0.5) + lb.ascent;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
struct _DecorationGapLess {
	_FORCE_INLINE_ bool operator()(const Vector2 &a, const Vector2 &b) const { return a.x < b.x; }
};

// The <label>'s text-decoration, after Chromium (Blink TextDecorationInfo /
// StrokeData, as in TextEdit): `auto` thickness is max(1px, font-size / 10);
// the underline's top sits max(1, ceil(t / 2))px under the baseline,
// line-through is centred a third of the whole-pixel ascent above it and an
// overline rests on the ascent. Solid and double strokes are whole pixels;
// dotted/dashed round the width and snap the centre; wavy keeps it fractional.
// Underlines and overlines skip ink (text-decoration-skip-ink: auto): they
// stop where a glyph's outline crosses them, dilated by the thickness.
void WebInput::_draw_label_decorations(const Ref<Font> &p_font, const Point2 &p_baseline) const {
	if (label_decoration_lines == 0 || p_font.is_null() || label_text.is_empty()) {
		return;
	}
	const real_t css_size = _label_font_size();
	const int fs = MAX(1, (int)Math::round(css_size));
	const real_t scale = css_size / fs;
	Ref<TextLine> line = _shape_label();
	const RID rid = line->get_rid();
	const real_t width = line->get_size().x * scale;
	if (width <= 0) {
		return;
	}
	const Color color = label_decoration_color.a > 0 ? label_decoration_color : theme_cache.label_font_color;
	const real_t thickness = label_decoration_thickness > 0 ? label_decoration_thickness : MAX((real_t)1.0, css_size / 10.0);
	const real_t whole = MAX((real_t)1.0, Math::floor(thickness));
	// The primary font's whole-pixel ascent (Blink's SimpleFontData), not the
	// Font's, which also covers its fallback faces.
	const real_t ascent = _line_box(p_font, String(), css_size, false).ascent;
	const real_t x0 = p_baseline.x;
	RID ci = get_canvas_item();

	// Glyph ink crossing the band [top, top + thickness] (baseline-relative,
	// y down), as x intervals in node px, dilated like Blink's clip-out.
	auto ink_gaps = [&](real_t p_top) {
		Vector<Vector2> gaps;
		const real_t band0 = p_top - p_baseline.y;
		const real_t band1 = band0 + thickness;
		const real_t dilation = MIN(thickness, (real_t)13.0);
		const int count = TS->shaped_text_get_glyph_count(rid);
		const Glyph *glyphs = TS->shaped_text_get_glyphs(rid);
		real_t pen = 0;
		for (int i = 0; i < count; i++) {
			const Glyph &g = glyphs[i];
			const real_t gx = (pen + g.x_off) * scale;
			pen += g.advance * g.repeat;
			if (g.font_rid == RID() || (g.flags & TextServer::GRAPHEME_IS_SPACE) == TextServer::GRAPHEME_IS_SPACE) {
				continue;
			}
			Dictionary outline = TS->font_get_glyph_contours(g.font_rid, g.font_size, g.index);
			PackedVector3Array points = outline.get("points", PackedVector3Array());
			PackedInt32Array ends = outline.get("contours", PackedInt32Array());
			real_t lo = 1e20, hi = -1e20;
			int start = 0;
			for (int c = 0; c < ends.size(); c++) {
				const int end = ends[c];
				for (int k = start; k <= end; k++) {
					const Vector3 a3 = points[k];
					const Vector3 b3 = points[k == end ? start : k + 1];
					const real_t ay = (a3.y + g.y_off) * scale, by = (b3.y + g.y_off) * scale;
					const real_t ax = a3.x * scale, bx = b3.x * scale;
					if (ay >= band0 && ay <= band1) {
						lo = MIN(lo, ax);
						hi = MAX(hi, ax);
					}
					const real_t edges[2] = { band0, band1 };
					for (real_t edge : edges) {
						if ((ay - edge) * (by - edge) < 0) {
							const real_t x = ax + (bx - ax) * (edge - ay) / (by - ay);
							lo = MIN(lo, x);
							hi = MAX(hi, x);
						}
					}
				}
				start = end + 1;
			}
			if (hi >= lo) {
				gaps.push_back(Vector2(x0 + gx + lo - dilation, x0 + gx + hi + dilation));
			}
		}
		return gaps;
	};

	// Paints [p_from, p_to) of a stroke segment minus the ink gaps.
	auto paint_rect = [&](real_t p_from, real_t p_to, real_t p_y, real_t p_h, const Vector<Vector2> &p_gaps) {
		real_t from = p_from;
		for (const Vector2 &gap : p_gaps) {
			if (gap.y <= from || gap.x >= p_to) {
				continue;
			}
			if (gap.x > from) {
				RenderingServer::get_singleton()->canvas_item_add_rect(ci, Rect2(from, p_y, gap.x - from, p_h), color);
			}
			from = MAX(from, gap.y);
		}
		if (p_to > from) {
			RenderingServer::get_singleton()->canvas_item_add_rect(ci, Rect2(from, p_y, p_to - from, p_h), color);
		}
	};

	auto stroke = [&](real_t p_top, bool p_skip_ink) {
		Vector<Vector2> gaps;
		if (p_skip_ink) {
			gaps = ink_gaps(p_top);
			gaps.sort_custom<_DecorationGapLess>();
		}
		const real_t from = x0, to = x0 + width;
		switch (label_decoration_style) {
			case 1: { // double
				const real_t top = Math::round(p_top);
				paint_rect(from, to, top, whole, gaps);
				paint_rect(from, to, top + Math::round(thickness + 1), whole, gaps);
			} break;
			case 2:
			case 3: { // dotted, dashed
				const real_t w = MAX((real_t)1.0, Math::round(thickness));
				real_t center = p_top + thickness * 0.5;
				center = ((int)w % 2 == 0) ? Math::floor(center) : Math::floor(center) + 0.5;
				const real_t top = center - w * 0.5;
				const bool dotted = label_decoration_style == 2;
				const real_t dash = dotted ? w : w * (w >= 3 ? 2 : 3);
				const real_t gap = dotted ? w : w * (w >= 3 ? 1 : 2);
				for (real_t x = from; x < to - 0.5; x += dash + gap) {
					const real_t end = MIN(x + dash, to);
					if (dotted && w > 3) {
						bool hidden = false;
						for (const Vector2 &g : gaps) {
							hidden = hidden || (x + w * 0.5 > g.x && x + w * 0.5 < g.y);
						}
						if (!hidden) {
							RenderingServer::get_singleton()->canvas_item_add_circle(ci, Vector2(x + w * 0.5, top + w * 0.5), w * 0.5, color, true);
						}
					} else {
						paint_rect(x, end, top, w, gaps);
					}
				}
			} break;
			case 4: { // wavy
				const real_t wavelength = 4.0 * thickness + 3.0;
				const real_t amplitude = (thickness + 1.0) * 0.5;
				const real_t center = p_top + thickness + 1.0; // the line top plus the double-line offset.
				const real_t step = MAX((real_t)0.5, wavelength / 16);
				Vector<Vector2> points;
				auto flush = [&]() {
					if (points.size() > 1) {
						Vector<Color> colors;
						colors.push_back(color);
						RenderingServer::get_singleton()->canvas_item_add_polyline(ci, points, colors, thickness, true);
					}
					points.clear();
				};
				for (real_t x = from;; x = MIN(x + step, to)) {
					bool hidden = false;
					for (const Vector2 &g : gaps) {
						hidden = hidden || (x > g.x && x < g.y);
					}
					if (hidden) {
						flush();
					} else {
						points.push_back(Vector2(x, center + amplitude * Math::sin((x - from) * Math::TAU / wavelength)));
					}
					if (x >= to) {
						break;
					}
				}
				flush();
			} break;
			default: { // solid
				paint_rect(from, to, Math::round(p_top), whole, gaps);
			} break;
		}
	};

	if (label_decoration_lines & 1) {
		stroke(p_baseline.y + MAX((real_t)1.0, Math::ceil(thickness * 0.5)), true);
	}
	if (label_decoration_lines & 2) {
		stroke(p_baseline.y - ascent - whole, true);
	}
	if (label_decoration_lines & 4) {
		stroke(p_baseline.y - ascent / 3.0 - whole * 0.5, false);
	}
}

// Godot sizes fonts in whole pixels while the browser's UA size is
// 13.3333px. Text is shaped at the rounded size, drawn scaled to the CSS size
// around its baseline origin and rasterized at that scaled size (oversampling),
// so glyphs land at the browser's size and positions and stay crisp.
void WebInput::_draw_text_css(const Ref<Font> &p_font, const Point2 &p_baseline, const String &p_text, real_t p_css_size,
		const Color &p_color, real_t p_width) {
	if (p_font.is_null() || p_text.is_empty()) {
		return;
	}
	int fs = MAX(1, (int)Math::round(p_css_size));
	real_t scale = p_css_size / fs;
	if (Math::abs(scale - 1.0) < 0.002) {
		p_font->draw_string(get_canvas_item(), p_baseline, p_text, HORIZONTAL_ALIGNMENT_LEFT, p_width, fs, p_color);
		return;
	}
	real_t viewing = get_global_transform_with_canvas().get_scale().x;
	draw_set_transform(draw_base + p_baseline, 0.0, Size2(scale, scale));
	p_font->draw_string(get_canvas_item(), Point2(), p_text, HORIZONTAL_ALIGNMENT_LEFT, p_width < 0 ? -1 : p_width / scale, fs,
			p_color, TextServer::JUSTIFICATION_KASHIDA | TextServer::JUSTIFICATION_WORD_BOUND, TextServer::DIRECTION_AUTO,
			TextServer::ORIENTATION_HORIZONTAL, MAX((real_t)0.01, viewing * scale));
	draw_set_transform(draw_base, 0.0, Size2(1, 1));
}

void WebInput::_draw_box(const BoxModel &p_box) {
	if (!draw_box) {
		return;
	}
	// Background + border + border-radius + box-shadow are all expressed as a
	// StyleBoxFlat, which gives CSS-faithful rounded corners, shadow and AA
	// while still rendering the native thin-border default crisply.
	Ref<StyleBoxFlat> sb = _make_style_box(p_box);
	draw_style_box(sb, Rect2(Point2(), p_box.border_box));
}

void WebInput::_draw_button(const BoxModel &p_box) {
	_draw_box(p_box);
	Ref<Font> f = _resolved_font();
	if (f.is_null()) {
		return;
	}
	String txt = _display_text();
	real_t tw = _date_text_width(f, txt, p_box.font_size);
	Point2 pos((p_box.border_box.x - tw) * 0.5, Math::round(_input_text_baseline(p_box)));
	_draw_text_css(f, pos, txt, p_box.font_size, p_box.text_color);
}

void WebInput::_draw_checkbox(const BoxModel &p_box, bool p_radio) {
	// Chromium stretches the box but keeps the control square, centring a
	// glyph of min(width, height) inside it, and paints it on the
	// pixel-snapped rect (a label in front leaves the box at a fractional x).
	Size2 box = p_box.border_box;
	real_t side = MIN(box.x, box.y);
	Point2 at((box.x - side) * 0.5, (box.y - side) * 0.5);
	Size2 sz(side, side);
	{
		Transform2D to_canvas = get_global_transform_with_canvas().translated_local(draw_base);
		Transform2D from_canvas = to_canvas.affine_inverse();
		Point2 start = from_canvas.xform(to_canvas.xform(at).round());
		Point2 end = from_canvas.xform(to_canvas.xform(at + sz).round());
		at = start;
		sz = end - start;
	}
	Rect2 r(at, sz);
	Color accent = _accent_color();
	// A checked box darkens under the pointer and lightens while pressed,
	// like the range's filled track.
	if (checked && !disabled && pressed) {
		accent = accent.lerp(Color(1, 1, 1), 0.216);
	} else if (checked && !disabled && hovered) {
		accent = accent.lerp(Color(0, 0, 0), 0.215);
	}
	if (p_radio) {
		// A 1px antialiased ring, the interior and a dot of 0.3 x the side.
		// Translucent disabled colours must not overlap, so the ring is a stroke.
		Vector2 c = at + sz * 0.5;
		real_t rad = MIN(sz.x, sz.y) * 0.5;
		Color ring = disabled ? CHROME_DISABLED_CONTROL_BORDER : (checked ? accent : CHROME_FIELD_BORDER);
		draw_circle(c, rad - 1.0, disabled ? CHROME_DISABLED_CONTROL_BG : Color(1, 1, 1), true, -1.0, true);
		draw_arc(c, rad - 0.5, 0.0, Math::TAU, 64, ring, 1.0, true);
		if (checked) {
			draw_circle(c, MIN(sz.x, sz.y) * 0.3, disabled ? CHROME_DISABLED_ACCENT : accent, true, -1.0, true);
		}
	} else {
		// 1px border, ~2px rounded corners (StyleBoxFlat gives the rounded
		// antialiased edge Chromium draws). Checked fills with the OS accent;
		// a disabled box keeps its border and lays the disabled accent over
		// the interior.
		Ref<StyleBoxFlat> sb;
		sb.instantiate();
		sb->set_border_width_all(1);
		sb->set_corner_radius_all(2);
		if (disabled) {
			sb->set_bg_color(CHROME_DISABLED_CONTROL_BG);
			sb->set_border_color(CHROME_DISABLED_CONTROL_BORDER);
		} else {
			sb->set_bg_color(checked ? accent : Color(1, 1, 1));
			sb->set_border_color(checked ? accent : CHROME_FIELD_BORDER);
		}
		draw_style_box(sb, r);
		if (checked) {
			if (disabled) {
				draw_rect(r.grow(-1.0), CHROME_DISABLED_ACCENT);
			}
			Vector<Point2> pts;
			pts.push_back(at + Point2(sz.x * 0.22, sz.y * 0.52));
			pts.push_back(at + Point2(sz.x * 0.42, sz.y * 0.72));
			pts.push_back(at + Point2(sz.x * 0.80, sz.y * 0.28));
			draw_polyline(pts, disabled ? CHROME_DISABLED_CHECK : Color(1, 1, 1), MAX((real_t)1.5, side * 0.115), true);
		}
	}
}

double WebInput::_parse_num(const String &p_s, double p_def) const {
	if (p_s.is_empty() || !p_s.is_valid_float()) {
		return p_def;
	}
	return p_s.to_float();
}

double WebInput::_range_min() const {
	return (double)min_value;
}

double WebInput::_range_max() const {
	return (double)max_value;
}

double WebInput::_range_step() const {
	return step_value > 0 ? (double)step_value : 1.0;
}

double WebInput::_range_value() const {
	double lo = _range_min();
	double hi = _range_max();
	if (hi < lo) {
		hi = lo;
	}
	double v = value.is_empty() ? (lo + (hi - lo) * 0.5) : _parse_num(value, lo);
	return CLAMP(v, lo, hi);
}

void WebInput::_set_range_from_pos(real_t p_x) {
	BoxModel b = _compute_box_model();
	Size2 sz = b.border_box;
	p_x -= _compute_layout(b).input_origin.x; // account for a left/top label.
	real_t thumb_r = sz.y * 0.5;
	real_t usable = MAX(1.0, sz.x - thumb_r * 2);
	double frac = CLAMP((p_x - thumb_r) / usable, 0.0, 1.0);
	double lo = _range_min(), hi = _range_max(), step = _range_step();
	double v = lo + frac * (hi - lo);
	v = lo + Math::round((v - lo) / step) * step; // snap to step.
	v = CLAMP(v, lo, hi);
	String new_val = String::num(v, 4).trim_suffix("0").trim_suffix(".");
	if (new_val != value) {
		value = new_val;
		queue_redraw();
		emit_signal(SNAME("value_changed"), value);
	}
}

void WebInput::_draw_range(const BoxModel &p_box) {
	// Author CSS border/radius/shadow paint first; the native track paints over
	// the fill, so the background is intentionally NOT drawn here (Chromium's
	// range ignores background-color visually).
	BoxModel box_no_fill = p_box;
	box_no_fill.background.a = 0.0;
	_draw_box(box_no_fill);

	// Chromium paints a rounded track whose left part is filled in the accent
	// colour up to the thumb, with the remainder in the shared control face.
	// Hovering darkens the accent and pressing lightens it; there is no outline
	// in any state, which is why the focus ring is suppressed for this type.
	// The slider itself does not stretch: Chromium keeps it at its intrinsic
	// height and centres it in a taller box.
	Size2 sz = p_box.border_box;
	real_t band_h = MIN(p_box.intrinsic_box.y > 0 ? p_box.intrinsic_box.y : sz.y, sz.y);
	real_t cy = sz.y * 0.5;
	real_t track_h = band_h * 0.5;
	real_t thumb_r = band_h * 0.5;
	Color accent = _accent_color();
	// Hover darkens the filled track only; the thumb keeps the accent
	// (Chrome 154). Pressing lightens both.
	Color thumb_accent = accent;
	Color face = CHROME_BUTTON_BG;
	if (disabled) {
		accent = CHROME_DISABLED_BORDER;
		thumb_accent = accent;
		face = CHROME_DISABLED_BG;
	} else if (pressed) {
		accent = accent.lerp(Color(1, 1, 1), 0.216); // #0075ff -> #3793ff
		thumb_accent = accent;
		face = CHROME_FACE_ACTIVE;
	} else if (hovered) {
		accent = accent.lerp(Color(0, 0, 0), 0.215); // #0075ff -> #005cc8
		face = CHROME_FACE_HOVER;
	}

	double lo = _range_min(), hi = _range_max();
	double frac = (hi > lo) ? CLAMP((_range_value() - lo) / (hi - lo), 0.0, 1.0) : 0.5;
	real_t thumb_cx = thumb_r + frac * (sz.x - thumb_r * 2);

	// Rounded rectangles keep the anti-aliased edges inside the control's box;
	// a plain antialiased circle bleeds a pixel past it.
	struct Local {
		static void pill(CanvasItem *ci, const Rect2 &r, const Color &c, int radius) {
			Ref<StyleBoxFlat> sb;
			sb.instantiate();
			sb->set_bg_color(c);
			for (int i = 0; i < 4; i++) {
				sb->set_corner_radius((Corner)i, radius);
			}
			sb->set_anti_aliased(true);
			ci->draw_style_box(sb, r);
		}
	};
	int track_r = (int)Math::round(track_h * 0.5);
	Local::pill(this, Rect2(1, cy - track_h * 0.5, sz.x - 2, track_h), face, track_r);
	real_t fill_w = CLAMP(thumb_cx - 1, (real_t)0.0, sz.x - 2);
	if (fill_w > 0) {
		Local::pill(this, Rect2(1, cy - track_h * 0.5, fill_w, track_h), accent, track_r);
	}
	Local::pill(this, Rect2(thumb_cx - thumb_r, cy - thumb_r, thumb_r * 2, thumb_r * 2), thumb_accent,
			(int)Math::round(thumb_r));
}

void WebInput::_enforce_radio_group() {
	// HTML radio semantics: only one radio per `name` group (siblings) checked.
	if (input_type != TYPE_RADIO || !checked) {
		return;
	}
	Node *parent = get_parent();
	if (!parent) {
		return;
	}
	for (int i = 0; i < parent->get_child_count(); i++) {
		WebInput *other = Object::cast_to<WebInput>(parent->get_child(i));
		if (other && other != this && other->input_type == TYPE_RADIO && other->attr_name == attr_name && other->checked) {
			other->set_checked(false);
		}
	}
}

// Keyboard activation, as Chromium does it: Space presses on keydown and
// clicks on keyup (checkbox, radio and the button types); Enter clicks a
// button on keydown and submits implicitly from a checkbox or radio; the
// arrow keys move a radio group's selection. Returns true when consumed.
bool WebInput::_activation_key_input(const Ref<InputEventKey> &p_key) {
	bool is_button = input_type == TYPE_BUTTON || input_type == TYPE_SUBMIT || input_type == TYPE_RESET || input_type == TYPE_IMAGE;
	bool is_toggle = input_type == TYPE_CHECKBOX || input_type == TYPE_RADIO;
	if ((!is_button && !is_toggle) || p_key->is_ctrl_pressed() || p_key->is_alt_pressed() || p_key->is_meta_pressed()) {
		return false;
	}
	Key code = p_key->get_keycode();
	if (code == Key::SPACE) {
		if (p_key->is_pressed()) {
			if (!p_key->is_echo() && !pressed) {
				pressed = true;
				queue_redraw();
			}
		} else if (pressed) {
			pressed = false;
			queue_redraw();
			_activate_from_key();
		}
		return true;
	}
	if (code == Key::ENTER || code == Key::KP_ENTER) {
		if (!p_key->is_pressed()) {
			return true;
		}
		if (is_button) {
			_activate_from_key();
		} else {
			// The form decides what implicit submission does; the control only
			// reports it, as a text field does through its LineEdit.
			emit_signal(SNAME("submitted"), get_value());
		}
		return true;
	}
	if (input_type == TYPE_RADIO && p_key->is_pressed() &&
			(code == Key::UP || code == Key::DOWN || code == Key::LEFT || code == Key::RIGHT)) {
		int direction = (code == Key::UP || code == Key::LEFT) ? -1 : 1;
		if (is_layout_rtl() && (code == Key::LEFT || code == Key::RIGHT)) {
			direction = -direction;
		}
		// The embedder owns the group when it listens; otherwise the group is
		// this control's same-named siblings (as `_enforce_radio_group`).
		if (has_connections(SNAME("radio_navigation_requested"))) {
			emit_signal(SNAME("radio_navigation_requested"), direction);
		} else {
			_navigate_radio_siblings(direction);
		}
		return true;
	}
	return false;
}

// The click a keyboard activation simulates (HTMLInputElement's activation
// behavior). A pointer still toggles on press; see gui_input.
void WebInput::_activate_from_key() {
	switch (input_type) {
		case TYPE_CHECKBOX:
			set_checked(!checked);
			emit_signal(SNAME("toggled"), checked);
			break;
		case TYPE_RADIO:
			if (!checked) {
				set_checked(true);
				_enforce_radio_group();
				emit_signal(SNAME("toggled"), checked);
			}
			break;
		case TYPE_SUBMIT:
			submit_form();
			emit_signal(SNAME("pressed"));
			break;
		case TYPE_RESET:
			reset_form();
			emit_signal(SNAME("pressed"));
			break;
		case TYPE_BUTTON:
		case TYPE_IMAGE:
			emit_signal(SNAME("pressed"));
			break;
		default:
			break;
	}
}

// Chromium's radio arrow navigation within same-named siblings: the next
// enabled, visible radio in the direction (wrapping) gets focus and is checked.
void WebInput::_navigate_radio_siblings(int p_direction) {
	Node *parent = get_parent();
	if (!parent) {
		return;
	}
	Vector<WebInput *> group;
	for (int i = 0; i < parent->get_child_count(); i++) {
		WebInput *w = Object::cast_to<WebInput>(parent->get_child(i));
		if (w && w->input_type == TYPE_RADIO && w->attr_name == attr_name) {
			group.push_back(w);
		}
	}
	int at = group.find(this);
	int n = group.size();
	for (int i = 1; at >= 0 && i < n; i++) {
		WebInput *next = group[((at + p_direction * i) % n + n) % n];
		if (next->disabled || !next->is_visible_in_tree()) {
			continue;
		}
		next->grab_focus();
		next->_activate_from_key();
		return;
	}
}

Rect2 WebInput::_spin_rect(const BoxModel &p_box) const {
	// Right-aligned inside the content box, one pixel short of its height --
	// the geometry Chromium's ::-webkit-inner-spin-button ends up with.
	Size2 sz = p_box.border_box;
	real_t scale = p_box.font_size / CHROME_FONT_SIZE;
	real_t w = 15 * scale;
	real_t x1 = sz.x - p_box.border[1] - p_box.padding[1];
	real_t y0 = p_box.border[0] + p_box.padding[0];
	real_t y1 = sz.y - p_box.border[2] - p_box.padding[2] - 1 * scale;
	return Rect2(x1 - w, y0, w, y1 - y0);
}

real_t WebInput::_search_clear_width(const BoxModel &p_box) const {
	if (input_type != TYPE_SEARCH) {
		return 0;
	}
	return 14 * (p_box.font_size / CHROME_FONT_SIZE);
}

void WebInput::_draw_search_clear(const BoxModel &p_box) {
	// Chromium only paints the clear button while the pointer is over a search
	// field that holds a value; the space it needs is reserved either way.
	if (input_type != TYPE_SEARCH || value.is_empty() || !hovered || disabled || readonly) {
		return;
	}
	Size2 sz = p_box.border_box;
	real_t scale = p_box.font_size / CHROME_FONT_SIZE;
	real_t w = _search_clear_width(p_box);
	real_t cx = sz.x - p_box.border[1] - p_box.padding[1] - w * 0.5;
	real_t cy = sz.y * 0.5;
	real_t d = 3.0 * scale;
	Color col = CHROME_SPIN_ARROW;
	draw_line(Point2(cx - d, cy - d), Point2(cx + d, cy + d), col, scale * 1.5, true);
	draw_line(Point2(cx + d, cy - d), Point2(cx - d, cy + d), col, scale * 1.5, true);
}

void WebInput::_draw_number_spinner(const BoxModel &p_box) {
	// Chromium keeps the spinner at opacity 0 until the pointer is over the
	// field; the space it takes is reserved either way (see _sync_line_edit).
	if (input_type != TYPE_NUMBER || !hovered || disabled || readonly) {
		return;
	}
	Rect2 r = _spin_rect(p_box);
	real_t scale = p_box.font_size / CHROME_FONT_SIZE;
	draw_rect(r, CHROME_SPIN_BG, true);

	real_t cx = r.position.x + r.size.x * 0.5;
	real_t half_w = 4.5 * scale;
	real_t h = 5 * scale;
	for (int dir = 1; dir >= -1; dir -= 2) {
		Color col = (hovered_spin == dir) ? CHROME_SPIN_ARROW_HOVER : CHROME_SPIN_ARROW;
		// Chromium's arrows are blunt-tipped rather than sharp triangles, so the
		// tip is a short flat edge.
		real_t tip_w = 2.0 * scale;
		Vector<Point2> tri;
		if (dir > 0) {
			real_t top = r.position.y;
			tri.push_back(Point2(cx - tip_w, top));
			tri.push_back(Point2(cx + tip_w, top));
			tri.push_back(Point2(cx + half_w, top + h));
			tri.push_back(Point2(cx - half_w, top + h));
		} else {
			real_t bottom = r.position.y + r.size.y;
			tri.push_back(Point2(cx + tip_w, bottom));
			tri.push_back(Point2(cx - tip_w, bottom));
			tri.push_back(Point2(cx - half_w, bottom - h));
			tri.push_back(Point2(cx + half_w, bottom - h));
		}
		draw_colored_polygon(tri, col);
	}
}

void WebInput::_step_number(int p_dir) {
	double cur = _parse_num(value, _range_min());
	cur = CLAMP(cur + _range_step() * p_dir, _range_min(), _range_max());
	set_value(String::num(cur, 4).trim_suffix("0").trim_suffix("."));
	emit_signal(SNAME("value_changed"), value);
}

void WebInput::_open_color_picker() {
	if (disabled || readonly) {
		return;
	}
	if (external_color_picker_enabled) {
		Color current = value.is_valid_html_color() ? Color::html(value) : Color(0, 0, 0, 1);
		emit_signal(SNAME("color_picker_requested"), current);
		return;
	}
	if (!color_popup) {
		color_popup = memnew(PopupPanel);
		color_picker = memnew(ColorPicker);
		color_popup->add_child(color_picker);
		color_picker->connect("color_changed", callable_mp(this, &WebInput::_on_color_picked));
		add_child(color_popup, false, INTERNAL_MODE_FRONT);
	}
	Color cur = Color(0, 0, 0, 1);
	if (!value.is_empty() && value.is_valid_html_color()) {
		cur = Color::html(value);
	}
	color_picker->set_pick_color(cur);
	BoxModel b = _compute_box_model();
	Point2 origin = _compute_layout(b).input_origin;
	Point2 screen = get_screen_position() + origin + Point2(0, b.border_box.y + 1);
	color_popup->reset_size();
	color_popup->set_position(Point2i(screen));
	color_popup->popup();
}

void WebInput::_on_color_picked(const Color &p_color) {
	// HTML exposes the colour as a lower-case #rrggbb string.
	String hex = "#" + p_color.to_html(false);
	if (hex == value) {
		return;
	}
	value = hex;
	queue_redraw();
	emit_signal(SNAME("value_changed"), value);
}

void WebInput::_coerce_value_for_type() {
	String before = value;
	switch (input_type) {
		case TYPE_NUMBER: {
			if (!value.is_empty() && !value.is_valid_float()) {
				value = "0";
			}
		} break;
		case TYPE_RANGE: {
			if (!value.is_valid_float()) {
				// A range always reports a value; with none set that is the
				// midpoint of its min/max, like the browser. Clear first so the
				// unparseable text cannot be read back as a zero.
				value = String();
				value = String::num(_range_value(), 4).trim_suffix("0").trim_suffix(".");
			}
		} break;
		case TYPE_COLOR: {
			// type=color has no empty state; anything unparseable becomes black.
			value = (!value.is_empty() && value.is_valid_html_color())
					? "#" + Color::html(value).to_html(false)
					: "#000000";
		} break;
		case TYPE_FILE: {
			// A file input's value cannot be carried over from another type.
			value = String();
			selected_files.clear();
		} break;
		default:
			break;
	}
	if (value != before) {
		emit_signal(SNAME("value_changed"), value);
	}
}

void WebInput::_draw_color(const BoxModel &p_box) {
	_draw_box(p_box);
	// ::-webkit-color-swatch-wrapper pads 4px 2px inside the border + padding;
	// the swatch it holds has a 1px #777777 border around the colour.
	const real_t inset_x = 2.0, inset_y = 4.0;
	Rect2 wrapper(Point2(p_box.border[3] + p_box.padding[3] + inset_x, p_box.border[0] + p_box.padding[0] + inset_y),
			Size2(p_box.border_box.x - p_box.border[1] - p_box.border[3] - p_box.padding[1] - p_box.padding[3] - inset_x * 2,
					p_box.border_box.y - p_box.border[0] - p_box.border[2] - p_box.padding[0] - p_box.padding[2] - inset_y * 2));
	Color swatch = Color(0, 0, 0, 1);
	if (!value.is_empty() && value.is_valid_html_color()) {
		swatch = Color::html(value);
	}
	if (wrapper.size.x <= 0 || wrapper.size.y <= 0) {
		return;
	}
	draw_rect(wrapper, CHROME_SWATCH_BORDER, true);
	Rect2 fill = wrapper.grow(-1);
	if (fill.size.x > 0 && fill.size.y > 0) {
		draw_rect(fill, swatch, true);
	}
}

void WebInput::_draw_text_content(const BoxModel &p_box) {
	_draw_box(p_box);
	// Text rendering is handled by the embedded LineEdit child.
	_draw_number_spinner(p_box);
	_draw_search_clear(p_box);
}

bool WebInput::_is_datetime_family() const {
	switch (input_type) {
		case TYPE_DATE:
		case TYPE_TIME:
		case TYPE_DATETIME_LOCAL:
		case TYPE_MONTH:
		case TYPE_WEEK:
			return true;
		default:
			return false;
	}
}

bool WebInput::_ua_border_box() const {
	// Chromium's html.css gives these types box-sizing: border-box, so an
	// author width/height is the whole box; the rest are content-box and grow
	// by their border and padding.
	switch (input_type) {
		case TYPE_SEARCH:
		case TYPE_BUTTON:
		case TYPE_SUBMIT:
		case TYPE_RESET:
		case TYPE_IMAGE:
		case TYPE_CHECKBOX:
		case TYPE_RADIO:
		case TYPE_RANGE:
		case TYPE_COLOR:
		case TYPE_FILE:
			return true;
		default:
			return false;
	}
}

bool WebInput::_is_calendar_family() const {
	// Everything but type=time opens a calendar, and so paints the calendar
	// indicator instead of the clock one.
	return _is_datetime_family() && input_type != TYPE_TIME;
}

// ---------------------------------------------------------------------------
// Locale-driven date/time field layout
//
// Chromium formats these editors with the browser's UI language, so the field
// order, the separators and the empty placeholders all change with the locale
// (en-US shows "mm/dd/yyyy", zh-TW shows "年/月/日"). The table below mirrors
// the CLDR patterns Chromium resolves; adding a language is one more row.
//
// Pattern syntax: "{<code>:<placeholder>}" introduces an editable field, and
// everything outside the braces is a literal separator. Field codes:
//   y year   M numeric month   N month name   d day   w ISO week
//   h 12-hour   H 24-hour   m minute   s second   p AM/PM
// ---------------------------------------------------------------------------
namespace {

struct DateLocaleEntry {
	const char *locale; // "en_US"; matched exactly, then by language.
	const char *date;
	const char *time;
	const char *datetime;
	const char *month;
	const char *week;
	const char *am;
	const char *pm;
	const char *months[12];
	const char *weekdays[7]; // Sunday first.
	int first_weekday; // 0 = Sunday, 1 = Monday (calendar drop-down).
};

// clang-format off
static const DateLocaleEntry DATE_LOCALES[] = {
	{ "en_US",
		"{M:mm}/{d:dd}/{y:yyyy}",
		"{h:--}:{m:--} {p:--}",
		"{M:mm}/{d:dd}/{y:yyyy} {h:--}:{m:--} {p:--}",
		"{N:---------} {y:----}",
		"Week {w:--}, {y:----}",
		"AM", "PM",
		{ "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" },
		{ "Su", "Mo", "Tu", "We", "Th", "Fr", "Sa" }, 0 },
	{ "en_GB",
		"{d:dd}/{M:mm}/{y:yyyy}",
		"{H:--}:{m:--}",
		"{d:dd}/{M:mm}/{y:yyyy} {H:--}:{m:--}",
		"{N:---------} {y:----}",
		"Week {w:--}, {y:----}",
		"am", "pm",
		{ "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" },
		{ "Su", "Mo", "Tu", "We", "Th", "Fr", "Sa" }, 1 },
	{ "de",
		"{d:TT}.{M:MM}.{y:JJJJ}",
		"{H:--}:{m:--}",
		"{d:TT}.{M:MM}.{y:JJJJ} {H:--}:{m:--}",
		"{N:---------} {y:----}",
		"Woche {w:--}, {y:----}",
		"AM", "PM",
		{ "Januar", "Februar", "März", "April", "Mai", "Juni", "Juli", "August", "September", "Oktober", "November", "Dezember" },
		{ "So", "Mo", "Di", "Mi", "Do", "Fr", "Sa" }, 1 },
	{ "fr",
		"{d:jj}/{M:mm}/{y:aaaa}",
		"{H:--}:{m:--}",
		"{d:jj}/{M:mm}/{y:aaaa} {H:--}:{m:--}",
		"{N:---------} {y:----}",
		"Semaine {w:--}, {y:----}",
		"AM", "PM",
		{ "janvier", "février", "mars", "avril", "mai", "juin", "juillet", "août", "septembre", "octobre", "novembre", "décembre" },
		{ "di", "lu", "ma", "me", "je", "ve", "sa" }, 1 },
	{ "es",
		"{d:dd}/{M:mm}/{y:aaaa}",
		"{H:--}:{m:--}",
		"{d:dd}/{M:mm}/{y:aaaa} {H:--}:{m:--}",
		"{N:---------} {y:----}",
		"Semana {w:--}, {y:----}",
		"a. m.", "p. m.",
		{ "enero", "febrero", "marzo", "abril", "mayo", "junio", "julio", "agosto", "septiembre", "octubre", "noviembre", "diciembre" },
		{ "do", "lu", "ma", "mi", "ju", "vi", "sa" }, 1 },
	{ "ja",
		"{y:年}/{M:月}/{d:日}",
		"{H:--}:{m:--}",
		"{y:年}/{M:月}/{d:日} {H:--}:{m:--}",
		"{y:----}年{M:--}月",
		"{y:----}年第{w:--}週",
		"午前", "午後",
		{ "1月", "2月", "3月", "4月", "5月", "6月", "7月", "8月", "9月", "10月", "11月", "12月" },
		{ "日", "月", "火", "水", "木", "金", "土" }, 0 },
	{ "ko",
		"{y:년}. {M:월}. {d:일}.",
		"{p:--} {h:--}:{m:--}",
		"{y:년}. {M:월}. {d:일}. {p:--} {h:--}:{m:--}",
		"{y:----}년 {M:--}월",
		"{y:----}년 {w:--}번째 주",
		"오전", "오후",
		{ "1월", "2월", "3월", "4월", "5월", "6월", "7월", "8월", "9월", "10월", "11월", "12월" },
		{ "일", "월", "화", "수", "목", "금", "토" }, 0 },
	{ "zh_TW",
		"{y: 年 }/{M:月}/{d:日}",
		"{p:--} {h:--}:{m:--}",
		"{y: 年 }/{M:月}/{d:日} {p:--} {h:--}:{m:--}",
		"{y:----}年{M:--}月",
		"{y:----} 年，第 {w:--} 週",
		"上午", "下午",
		{ "1月", "2月", "3月", "4月", "5月", "6月", "7月", "8月", "9月", "10月", "11月", "12月" },
		{ "日", "一", "二", "三", "四", "五", "六" }, 0 },
	{ "zh_CN",
		"{y:年}/{M:月}/{d:日}",
		"{p:--}{h:--}:{m:--}",
		"{y:年}/{M:月}/{d:日} {p:--}{h:--}:{m:--}",
		"{y:----}年{M:--}月",
		"{y:----}年第{w:--}周",
		"上午", "下午",
		{ "1月", "2月", "3月", "4月", "5月", "6月", "7月", "8月", "9月", "10月", "11月", "12月" },
		{ "日", "一", "二", "三", "四", "五", "六" }, 0 },
};
// clang-format on

const DateLocaleEntry &date_locale_entry(const String &p_locale) {
	String want = p_locale.replace("-", "_");
	for (const DateLocaleEntry &e : DATE_LOCALES) {
		if (want == e.locale) {
			return e;
		}
	}
	String lang = want.get_slicec('_', 0);
	for (const DateLocaleEntry &e : DATE_LOCALES) {
		if (lang == String(e.locale).get_slicec('_', 0)) {
			return e;
		}
	}
	return DATE_LOCALES[0]; // en_US.
}

// Captions the browser supplies itself, in its UI language.
struct UaStrings {
	const char *locale;
	const char *submit;
	const char *reset;
	const char *choose_file;
	const char *no_file;
};

// clang-format off
static const UaStrings UA_STRINGS[] = {
	{ "en", "Submit", "Reset", "Choose File", "No file chosen" },
	{ "zh_TW", "提交", "重設", "選擇檔案", "未選擇任何檔案" },
	{ "zh_HK", "提交", "重設", "選擇檔案", "未選擇任何檔案" },
	{ "zh", "提交", "重置", "选择文件", "未选择任何文件" },
	{ "ja", "送信", "リセット", "ファイルを選択", "ファイルが選択されていません" },
	{ "ko", "제출", "재설정", "파일 선택", "선택된 파일 없음" },
	{ "de", "Senden", "Zurücksetzen", "Datei auswählen", "Keine ausgewählt" },
	{ "fr", "Envoyer", "Réinitialiser", "Choisir un fichier", "Aucun fichier choisi" },
	{ "es", "Enviar", "Restablecer", "Seleccionar archivo", "No se ha seleccionado ningún archivo" },
};
// clang-format on

const UaStrings &ua_strings_for(const String &p_locale) {
	String want = p_locale.replace("-", "_");
	for (const UaStrings &e : UA_STRINGS) {
		if (want == e.locale) {
			return e;
		}
	}
	String lang = want.get_slicec('_', 0);
	for (const UaStrings &e : UA_STRINGS) {
		if (lang == String(e.locale)) {
			return e;
		}
	}
	return UA_STRINGS[0];
}

} // namespace

String WebInput::_ua_text(const char *p_key) const {
	const UaStrings &s = ua_strings_for(_resolved_date_locale());
	String key = p_key;
	const char *text = key == "submit" ? s.submit : key == "reset" ? s.reset : key == "choose_file" ? s.choose_file : s.no_file;
	return String::utf8(text);
}

String WebInput::_resolved_date_locale() const {
	if (!date_locale.is_empty()) {
		return date_locale;
	}
	// No author override: follow the OS UI language, which is what a browser
	// formats its date/time editors with.
	return OS::get_singleton()->get_locale();
}

Ref<Font> WebInput::_date_font() const {
	// The UA font-family for these editors is `monospace` (Consolas on
	// Windows); an author font-family override still wins.
	if (css_font.is_valid()) {
		return css_font;
	}
	// A theme font is an author font-family too (the fallback font is the
	// "unset" sentinel, as in _get_font()).
	if (theme_cache.font.is_valid() && theme_cache.font != ThemeDB::get_singleton()->get_fallback_font()) {
		return theme_cache.font;
	}
	if (ua_mono_font.is_valid()) {
		return ua_mono_font;
	}
	return _get_font();
}

// Reference size the date/time metrics are measured at before being scaled
// down to the (fractional) CSS font size.
static const int DATE_METRIC_REF_SIZE = 256;

real_t WebInput::_date_text_width(const Ref<Font> &p_font, const String &p_text, real_t p_font_size) const {
	if (p_font.is_null() || p_text.is_empty()) {
		return 0;
	}
	real_t width = p_font->get_string_size(p_text, HORIZONTAL_ALIGNMENT_LEFT, -1, DATE_METRIC_REF_SIZE).x;
	// Glyph spacing is whole px at every size, so it is taken out of the
	// reference measurement and the CSS letter-spacing it stands for is added
	// at full size after every character (the face that carries it).
	Ref<FontVariation> variation = p_font;
	if (variation.is_valid()) {
		width -= variation->get_spacing(TextServer::SPACING_GLYPH) * p_text.length();
	}
	width *= p_font_size / (real_t)DATE_METRIC_REF_SIZE;
	if (p_font == css_font) {
		real_t spacing = css.has_letter_spacing ? css.letter_spacing : (real_t)theme_cache.letter_spacing;
		width += spacing * p_text.length();
	}
	return width;
}

WebInput::LineBox WebInput::_date_line_box() const {
	// The line box of the text the fields show now: a Han placeholder ("年")
	// or month name grows it with the fallback face, like the browser.
	String all;
	for (const DatePart &p : date_parts) {
		all += _date_part_text(p);
	}
	return _line_box(_date_font(), all, _date_font_size());
}

real_t WebInput::_date_line_height() const {
	if (date_line_height_cache >= 0) {
		return date_line_height_cache;
	}
	// ::-webkit-datetime-edit-fields-wrapper pads its line box by 1px top and
	// bottom (Chrome 154).
	date_line_height_cache = _date_line_box().height() + 2;
	return date_line_height_cache;
}

real_t WebInput::_date_font_size() const {
	if (css.has_font_size) {
		return css.font_size;
	}
	// Unlike the other types, the date/time editors derive their box size from
	// the font, so the exact fractional UA size matters: the theme stores the
	// user-agent default as a whole 13 (Godot themes only carry integer font
	// sizes), and that value means "Chromium's 13.3333px". A theme size that
	// differs from the default is a real author override and wins as-is.
	if (theme_cache.font_size > 0 && theme_cache.font_size != (int)Math::round(CHROME_FONT_SIZE)) {
		return (real_t)theme_cache.font_size;
	}
	return CHROME_FONT_SIZE;
}

void WebInput::_build_date_parts() {
	date_parts.clear();
	focused_part = -1;
	part_typed = 0;
	if (!_is_datetime_family()) {
		return;
	}
	const DateLocaleEntry &loc = date_locale_entry(_resolved_date_locale());
	const char *fmt = nullptr;
	switch (input_type) {
		case TYPE_DATE:
			fmt = loc.date;
			break;
		case TYPE_TIME:
			fmt = loc.time;
			break;
		case TYPE_DATETIME_LOCAL:
			fmt = loc.datetime;
			break;
		case TYPE_MONTH:
			fmt = loc.month;
			break;
		case TYPE_WEEK:
			fmt = loc.week;
			break;
		default:
			return;
	}

	String pat = String::utf8(fmt);
	String literal;
	for (int i = 0; i < pat.length(); i++) {
		if (pat[i] != '{') {
			literal += String::chr(pat[i]);
			continue;
		}
		int close = pat.find("}", i);
		if (close < 0) {
			literal += String::chr(pat[i]);
			continue;
		}
		if (!literal.is_empty()) {
			DatePart t;
			t.field = FIELD_TEXT;
			t.text = literal;
			date_parts.push_back(t);
			literal = String();
		}
		String spec = pat.substr(i + 1, close - i - 1);
		int colon = spec.find(":");
		String code = colon >= 0 ? spec.substr(0, colon) : spec;
		DatePart f;
		f.placeholder = colon >= 0 ? spec.substr(colon + 1) : String("--");
		switch (code.is_empty() ? (char32_t)' ' : code[0]) {
			case 'y':
				f.field = FIELD_YEAR;
				break;
			case 'M':
				f.field = FIELD_MONTH;
				break;
			case 'N':
				f.field = FIELD_MONTH_NAME;
				break;
			case 'd':
				f.field = FIELD_DAY;
				break;
			case 'w':
				f.field = FIELD_WEEK;
				break;
			case 'h':
				f.field = FIELD_HOUR12;
				break;
			case 'H':
				f.field = FIELD_HOUR24;
				break;
			case 'm':
				f.field = FIELD_MINUTE;
				break;
			case 's':
				f.field = FIELD_SECOND;
				break;
			case 'p':
				f.field = FIELD_AMPM;
				break;
			default:
				f.field = FIELD_TEXT;
				f.text = f.placeholder;
				break;
		}
		date_parts.push_back(f);
		i = close;
	}
	if (!literal.is_empty()) {
		DatePart t;
		t.field = FIELD_TEXT;
		t.text = literal;
		date_parts.push_back(t);
	}
	_date_parts_from_value();
	_layout_date_parts();
}

String WebInput::_date_part_text(const DatePart &p_part) const {
	if (p_part.field == FIELD_TEXT) {
		return p_part.text;
	}
	if (p_part.value < 0) {
		return p_part.placeholder;
	}
	const DateLocaleEntry &loc = date_locale_entry(_resolved_date_locale());
	switch (p_part.field) {
		case FIELD_YEAR:
			return String::num_int64(p_part.value).pad_zeros(4);
		case FIELD_MONTH_NAME:
			return String::utf8(loc.months[CLAMP(p_part.value, 1, 12) - 1]);
		case FIELD_AMPM:
			return String::utf8(p_part.value == 1 ? loc.pm : loc.am);
		default:
			return String::num_int64(p_part.value).pad_zeros(2);
	}
}

real_t WebInput::_date_part_max_width(const DatePart &p_part, const Ref<Font> &p_font, real_t p_font_size) const {
	if (p_font.is_null()) {
		return 0;
	}
	// Chromium sizes the control from the widest text each field can ever show
	// (its placeholder included), then lays the fields out at their current
	// text width, which is why a filled month field is narrower than an empty
	// one but the control keeps its size.
	real_t w = _date_text_width(p_font, p_part.placeholder, p_font_size);
	const DateLocaleEntry &loc = date_locale_entry(_resolved_date_locale());
	Vector<String> candidates;
	// Blink (DateTimeNumericFieldElement::MaximumWidth) measures each numeric
	// field's largest value as text -- year 275760, month 12, day 31, ... --
	// which only matches "00…" in a font with tabular figures.
	switch (p_part.field) {
		case FIELD_YEAR:
			candidates.push_back("275760");
			break;
		case FIELD_MONTH:
			candidates.push_back("12");
			break;
		case FIELD_DAY:
			candidates.push_back("31");
			break;
		case FIELD_WEEK:
			candidates.push_back("53");
			break;
		case FIELD_HOUR12:
			candidates.push_back("12");
			break;
		case FIELD_HOUR24:
			candidates.push_back("23");
			break;
		case FIELD_MINUTE:
		case FIELD_SECOND:
			candidates.push_back("59");
			break;
		case FIELD_MONTH_NAME:
			for (int i = 0; i < 12; i++) {
				candidates.push_back(String::utf8(loc.months[i]));
			}
			break;
		case FIELD_AMPM:
			candidates.push_back(String::utf8(loc.am));
			candidates.push_back(String::utf8(loc.pm));
			break;
		default:
			candidates.push_back("00");
			break;
	}
	for (const String &c : candidates) {
		w = MAX(w, _date_text_width(p_font, c, p_font_size));
	}
	return w;
}

void WebInput::_layout_date_parts() {
	date_edit_width_cache = -1;
	date_line_height_cache = -1;
	Ref<Font> f = _date_font();
	if (f.is_null()) {
		return;
	}
	real_t fs = _date_font_size();
	real_t x = 0;
	for (DatePart &p : date_parts) {
		real_t w;
		if (p.field == FIELD_TEXT) {
			w = _date_text_width(f, p.text, fs);
		} else {
			// Every field box hugs the text it shows now -- its value, else its
			// placeholder -- plus 1px padding each side (Chrome 154); only the
			// editor as a whole is sized for the widest values.
			w = _date_text_width(f, _date_part_text(p), fs) + 2;
		}
		p.x = x;
		p.width = w;
		x += w;
	}
	// text-align places the fields inside the editor, which is sized for their
	// widest values.
	int align = css.has_text_align ? css.text_align : theme_cache.text_align;
	if (align == (int)HORIZONTAL_ALIGNMENT_CENTER || align == (int)HORIZONTAL_ALIGNMENT_RIGHT) {
		real_t free = _date_edit_width() - x;
		if (free > 0) {
			real_t shift = align == (int)HORIZONTAL_ALIGNMENT_CENTER ? free * 0.5 : free;
			for (DatePart &p : date_parts) {
				p.x += shift;
			}
		}
	}
}

real_t WebInput::_date_edit_width() const {
	if (date_edit_width_cache >= 0) {
		return date_edit_width_cache;
	}
	Ref<Font> f = _date_font();
	if (f.is_null()) {
		return 0;
	}
	real_t fs = _date_font_size();
	real_t w = 0;
	for (const DatePart &p : date_parts) {
		if (p.field == FIELD_TEXT) {
			w += _date_text_width(f, p.text, fs);
		} else {
			w += _date_part_max_width(p, f, fs) + 2;
		}
	}
	// The year's six reserved digits are part of its maximum text above. The
	// clock editor's indicator keeps its own 8px gap (_compute_box_model). The
	// editor's width is rounded up to whole pixels.
	date_edit_width_cache = Math::ceil(w - 0.001);
	return date_edit_width_cache;
}

real_t WebInput::_date_indicator_size() const {
	// Square indicator box; it also sets the control's height whenever it is
	// taller than the text line. Both curves are fitted to Chrome 148 on
	// Windows across font sizes (the clock indicator is the larger of the two).
	real_t fs = _date_font_size();
	return _is_calendar_family() ? fs + 4.0 : 1.05 * fs + 6.0;
}

// Inclusive value range a field accepts (defined below, next to the key
// handling that also uses it).
static void date_field_range(WebInput::DateField p_field, int &r_lo, int &r_hi);

String WebInput::_date_iso_value() const {
	int year = -1, month = -1, day = -1, week = -1;
	int hour12 = -1, hour24 = -1, minute = -1, second = -1, ampm = -1;
	for (const DatePart &p : date_parts) {
		if (p.field != FIELD_TEXT && p.value >= 0) {
			// A half-typed field (a lone "0" in a 1-based box) is displayed but
			// is not yet a value, exactly like the browser.
			int lo = 0, hi = 0;
			date_field_range(p.field, lo, hi);
			if (p.value < lo || p.value > hi) {
				return String();
			}
		}
		switch (p.field) {
			case FIELD_YEAR:
				year = p.value;
				break;
			case FIELD_MONTH:
			case FIELD_MONTH_NAME:
				month = p.value;
				break;
			case FIELD_DAY:
				day = p.value;
				break;
			case FIELD_WEEK:
				week = p.value;
				break;
			case FIELD_HOUR12:
				hour12 = p.value;
				break;
			case FIELD_HOUR24:
				hour24 = p.value;
				break;
			case FIELD_MINUTE:
				minute = p.value;
				break;
			case FIELD_SECOND:
				second = p.value;
				break;
			case FIELD_AMPM:
				ampm = p.value;
				break;
			default:
				break;
		}
	}
	// A partly filled editor has no value at all, exactly like the browser.
	int h = hour24;
	if (h < 0 && hour12 >= 0 && ampm >= 0) {
		h = (hour12 % 12) + (ampm == 1 ? 12 : 0);
	}
	auto p4 = [](int v) { return String::num_int64(v).pad_zeros(4); };
	auto p2 = [](int v) { return String::num_int64(v).pad_zeros(2); };
	String date_str, time_str;
	bool need_date = (input_type != TYPE_TIME);
	bool need_time = (input_type == TYPE_TIME || input_type == TYPE_DATETIME_LOCAL);
	if (need_date) {
		if (input_type == TYPE_MONTH) {
			if (year < 0 || month < 0) {
				return String();
			}
			date_str = p4(year) + "-" + p2(month);
		} else if (input_type == TYPE_WEEK) {
			if (year < 0 || week < 0) {
				return String();
			}
			date_str = p4(year) + "-W" + p2(week);
		} else {
			if (year < 0 || month < 0 || day < 0) {
				return String();
			}
			date_str = p4(year) + "-" + p2(month) + "-" + p2(day);
		}
	}
	if (need_time) {
		if (h < 0 || minute < 0) {
			return String();
		}
		time_str = p2(h) + ":" + p2(minute);
		if (second >= 0) {
			time_str += ":" + p2(second);
		}
	}
	if (need_date && need_time) {
		return date_str + "T" + time_str;
	}
	return need_time ? time_str : date_str;
}

void WebInput::_date_parts_from_value() {
	for (DatePart &p : date_parts) {
		p.value = -1;
	}
	String v = value.strip_edges();
	if (v.is_empty()) {
		return;
	}
	String date_str = v;
	String time_str;
	int t_at = v.find("T");
	if (t_at >= 0) {
		date_str = v.substr(0, t_at);
		time_str = v.substr(t_at + 1);
	} else if (input_type == TYPE_TIME) {
		date_str = String();
		time_str = v;
	}
	int year = -1, month = -1, day = -1, week = -1, hour = -1, minute = -1, second = -1;
	if (!date_str.is_empty()) {
		Vector<String> bits = date_str.split("-");
		if (bits.size() >= 1 && bits[0].is_valid_int()) {
			year = (int)bits[0].to_int();
		}
		if (bits.size() >= 2) {
			if (bits[1].begins_with("W")) {
				week = (int)bits[1].substr(1).to_int();
			} else if (bits[1].is_valid_int()) {
				month = (int)bits[1].to_int();
			}
		}
		if (bits.size() >= 3 && bits[2].is_valid_int()) {
			day = (int)bits[2].to_int();
		}
	}
	if (!time_str.is_empty()) {
		Vector<String> hm = time_str.split(":");
		if (hm.size() >= 1 && hm[0].is_valid_int()) {
			hour = (int)hm[0].to_int();
		}
		if (hm.size() >= 2 && hm[1].is_valid_int()) {
			minute = (int)hm[1].to_int();
		}
		if (hm.size() >= 3 && hm[2].is_valid_int()) {
			second = (int)hm[2].to_int();
		}
	}
	for (DatePart &p : date_parts) {
		switch (p.field) {
			case FIELD_YEAR:
				p.value = year;
				break;
			case FIELD_MONTH:
			case FIELD_MONTH_NAME:
				p.value = month;
				break;
			case FIELD_DAY:
				p.value = day;
				break;
			case FIELD_WEEK:
				p.value = week;
				break;
			case FIELD_HOUR24:
				p.value = hour;
				break;
			case FIELD_HOUR12: {
				if (hour >= 0) {
					int h12 = hour % 12;
					p.value = (h12 == 0) ? 12 : h12;
				}
			} break;
			case FIELD_MINUTE:
				p.value = minute;
				break;
			case FIELD_SECOND:
				p.value = second;
				break;
			case FIELD_AMPM:
				if (hour >= 0) {
					p.value = hour >= 12 ? 1 : 0;
				}
				break;
			default:
				break;
		}
	}
}

void WebInput::_date_commit() {
	value = _date_iso_value();
	_layout_date_parts();
	update_minimum_size();
	queue_redraw();
	emit_signal(SNAME("value_changed"), value);
}

void WebInput::simulate_type(const String &p_text) {
	if (!line_edit || !_is_text_family()) {
		return;
	}
	line_edit->grab_focus();
	for (int i = 0; i < p_text.length(); i++) {
		line_edit->insert_text_at_caret(String::chr(p_text[i]));
		// insert_text_at_caret is the programmatic path and stays silent; the
		// keyboard path reports each change, so report it here too (this also
		// runs the type=number filter, as typing does).
		_on_line_edit_changed(line_edit->get_text());
	}
}

void WebInput::send_date_key(int p_keycode) {
	if (!_is_datetime_family()) {
		return;
	}
	Ref<InputEventKey> k;
	k.instantiate();
	k->set_pressed(true);
	k->set_keycode((Key)p_keycode);
	_date_key_input(k);
}

// Inclusive value range a field accepts, used by typing and by the up/down
// arrows (which wrap, like the browser).
static void date_field_range(WebInput::DateField p_field, int &r_lo, int &r_hi) {
	switch (p_field) {
		case WebInput::FIELD_YEAR:
			r_lo = 1;
			r_hi = 275760;
			break;
		case WebInput::FIELD_MONTH:
		case WebInput::FIELD_MONTH_NAME:
			r_lo = 1;
			r_hi = 12;
			break;
		case WebInput::FIELD_DAY:
			r_lo = 1;
			r_hi = 31;
			break;
		case WebInput::FIELD_WEEK:
			r_lo = 1;
			r_hi = 53;
			break;
		case WebInput::FIELD_HOUR12:
			r_lo = 1;
			r_hi = 12;
			break;
		case WebInput::FIELD_HOUR24:
			r_lo = 0;
			r_hi = 23;
			break;
		case WebInput::FIELD_MINUTE:
		case WebInput::FIELD_SECOND:
			r_lo = 0;
			r_hi = 59;
			break;
		case WebInput::FIELD_AMPM:
			r_lo = 0;
			r_hi = 1;
			break;
		default:
			r_lo = 0;
			r_hi = 0;
			break;
	}
}

void WebInput::_focus_date_part(int p_index, int p_dir) {
	// Snap onto the nearest editable part in the given direction (separators
	// are skipped, as they are in the browser's tab order).
	int n = date_parts.size();
	if (n == 0) {
		focused_part = -1;
		return;
	}
	int i = CLAMP(p_index, 0, n - 1);
	int step = p_dir >= 0 ? 1 : -1;
	for (int guard = 0; guard < n; guard++) {
		if (date_parts[i].field != FIELD_TEXT) {
			focused_part = i;
			part_typed = 0;
			queue_redraw();
			return;
		}
		i += step;
		if (i < 0 || i >= n) {
			// Nothing that way; sweep back from the other end.
			i = step > 0 ? n - 1 : 0;
			step = -step;
		}
	}
	focused_part = -1;
}

void WebInput::_date_step_focused(int p_delta) {
	if (focused_part < 0 || focused_part >= date_parts.size()) {
		return;
	}
	DatePart &p = date_parts.write[focused_part];
	int lo = 0, hi = 0;
	date_field_range(p.field, lo, hi);
	if (p.value < 0) {
		p.value = p_delta > 0 ? lo : hi;
	} else {
		p.value += p_delta;
		if (p.value > hi) {
			p.value = lo;
		} else if (p.value < lo) {
			p.value = hi;
		}
	}
	part_typed = 0;
	_date_commit();
}

bool WebInput::_date_key_input(const Ref<InputEventKey> &p_key) {
	if (!p_key->is_pressed() || date_parts.is_empty()) {
		return false;
	}
	if (focused_part < 0 || focused_part >= date_parts.size() || date_parts[focused_part].field == FIELD_TEXT) {
		_focus_date_part(0, 1);
		if (focused_part < 0) {
			return false;
		}
	}
	Key code = p_key->get_keycode();

	// Alt+Down opens the drop-down, matching the browser shortcut.
	if (code == Key::DOWN && p_key->is_alt_pressed()) {
		_open_picker();
		return true;
	}

	DatePart &s = date_parts.write[focused_part];
	int lo = 0, hi = 0;
	date_field_range(s.field, lo, hi);

	int ci = (int)code;
	int digit = -1;
	if (ci >= (int)Key::KEY_0 && ci <= (int)Key::KEY_9) {
		digit = ci - (int)Key::KEY_0;
	} else if (ci >= (int)Key::KP_0 && ci <= (int)Key::KP_9) {
		digit = ci - (int)Key::KP_0;
	}

	if (digit >= 0 && s.field != FIELD_AMPM) {
		int width = (s.field == FIELD_YEAR) ? 4 : 2;
		int cur = (part_typed == 0 || s.value < 0) ? digit : s.value * 10 + digit;
		part_typed++;
		if (cur > hi) {
			cur = digit;
			part_typed = 1;
		}
		// Keep the digits exactly as typed -- clamping a leading "0" up to the
		// field minimum here would turn a following "7" into 17 instead of 07.
		// _date_iso_value() treats an out-of-range field as not yet filled.
		s.value = cur;
		if (part_typed >= width || cur * 10 > hi) {
			_focus_date_part(focused_part + 1 < date_parts.size() ? focused_part + 1 : focused_part, 1);
		}
		_date_commit();
		return true;
	}
	if (s.field == FIELD_AMPM && (code == Key::A || code == Key::P)) {
		s.value = (code == Key::A) ? 0 : 1;
		_date_commit();
		return true;
	}

	switch (code) {
		case Key::UP:
			_date_step_focused(1);
			return true;
		case Key::DOWN:
			_date_step_focused(-1);
			return true;
		case Key::LEFT:
			if (focused_part > 0) {
				_focus_date_part(focused_part - 1, -1);
			}
			return true;
		case Key::RIGHT:
			if (focused_part + 1 < date_parts.size()) {
				_focus_date_part(focused_part + 1, 1);
			}
			return true;
		case Key::BACKSPACE:
		case Key::KEY_DELETE:
			s.value = -1;
			part_typed = 0;
			_date_commit();
			return true;
		default:
			return false;
	}
}

int WebInput::_date_part_at(const Point2 &p_pos) const {
	// p_pos is relative to the input box's top-left corner.
	BoxModel b = _compute_box_model();
	real_t content_x = b.border[3] + b.padding[3];
	for (int i = 0; i < date_parts.size(); i++) {
		const DatePart &p = date_parts[i];
		if (p.field == FIELD_TEXT) {
			continue;
		}
		if (p_pos.x >= content_x + p.x && p_pos.x < content_x + p.x + p.width) {
			return i;
		}
	}
	// Clicking past the last field lands on the nearest one, like the browser.
	if (!date_parts.is_empty() && p_pos.x >= content_x) {
		for (int i = date_parts.size() - 1; i >= 0; i--) {
			if (date_parts[i].field != FIELD_TEXT && p_pos.x >= content_x + date_parts[i].x) {
				return i;
			}
		}
	}
	return -1;
}

// ---------------------------------------------------------------------------
// Date/time painting
// ---------------------------------------------------------------------------
void WebInput::_draw_picker_indicator(const BoxModel &p_box) {
	Size2 sz = p_box.border_box;
	real_t fs = p_box.font_size;
	real_t scale = fs / CHROME_FONT_SIZE;
	real_t ind = _date_indicator_size();
	// The indicator sits flush against the content box's right edge and is
	// vertically centred; its size also sets the control's height.
	real_t bx = MAX(p_box.border[3], sz.x - p_box.border[1] - ind);
	real_t by = Math::round((sz.y - ind) * 0.5);
	// The UA indicator is an image in the field's own text colour: an author
	// `color` does not tint it (Chrome 154).
	Color col = disabled ? CHROME_DISABLED_TEXT : CHROME_TEXT_FG;

	if (_is_calendar_family()) {
		// 11x11.5 calendar: two rings, a filled title band and an open body.
		real_t left = bx + 3 * scale;
		real_t top = by + 2 * scale;
		draw_line(Point2(left + 2.5 * scale, top + 0.5 * scale), Point2(left + 2.5 * scale, top + 1.5 * scale), col, scale, true);
		draw_line(Point2(left + 8.5 * scale, top + 0.5 * scale), Point2(left + 8.5 * scale, top + 1.5 * scale), col, scale, true);
		Rect2 body(left + 1.0 * scale, top + 1.5 * scale, 9 * scale, 9.5 * scale);
		draw_rect(body, col, false, scale, true);
		draw_rect(Rect2(left + 0.5 * scale, top + 2.0 * scale, 10 * scale, 2.0 * scale), col, true);
	} else {
		// 12x12 clock: a ring with hands at 12 and roughly 4.
		real_t left = bx + 4 * scale;
		real_t top = by + 4 * scale;
		Point2 c(left + 5.75 * scale, top + 5.75 * scale);
		draw_arc(c, 5.25 * scale, 0, Math::TAU, 24, col, scale, true);
		draw_line(c, c - Point2(0, 3.4 * scale), col, scale, true);
		draw_line(c, c + Point2(2.6 * scale, 2.6 * scale), col, scale, true);
	}
}

void WebInput::_draw_datetime(const BoxModel &p_box) {
	_draw_box(p_box);
	Ref<Font> f = _date_font();
	if (f.is_null()) {
		return;
	}
	Size2 sz = p_box.border_box;
	int fs = (int)Math::round(p_box.font_size);
	real_t content_x = p_box.border[3] + p_box.padding[3];
	real_t content_y = p_box.border[0] + p_box.padding[0];
	real_t content_h = sz.y - p_box.border[0] - p_box.border[2] - p_box.padding[0] - p_box.padding[2];
	(void)content_h;
	(void)content_y;
	// The fields line box sits centred in the content box with 1px padding
	// above (see _input_text_baseline); a focused field's highlight covers its
	// inline box: the primary face's content area plus the 1px field padding.
	real_t baseline = Math::round(_input_text_baseline(p_box));
	LineBox primary = _line_box(f, String(), _date_font_size());
	real_t highlight_top = baseline - primary.ascent - 1;
	real_t highlight_h = primary.height() + 2;
	bool focused = has_focus() && !disabled;
	Color fg = disabled ? CHROME_DISABLED_TEXT : p_box.text_color;

	for (int i = 0; i < date_parts.size(); i++) {
		const DatePart &p = date_parts[i];
		String txt = _date_part_text(p);
		real_t px = content_x + p.x;
		Color col = fg;
		if (focused && i == focused_part && p.field != FIELD_TEXT) {
			// Chromium fills the focused field with the system highlight for
			// the full height of the editor and inverts the text.
			draw_rect(Rect2(px, highlight_top, p.width, highlight_h), CHROME_FIELD_HIGHLIGHT, true);
			col = Color(1, 1, 1);
		}
		real_t tw = _date_text_width(f, txt, p_box.font_size);
		_draw_text_css(f, Point2(px + (p.width - tw) * 0.5, baseline), txt, p_box.font_size, col);
	}

	_draw_picker_indicator(p_box);
}

// ---------------------------------------------------------------------------
// Picker drop-down
// ---------------------------------------------------------------------------
void WebInput::_open_picker() {
	if (disabled || readonly || !_is_datetime_family()) {
		return;
	}
	if (!picker_popup) {
		picker_popup = memnew(Popup);
		picker_panel = memnew(WebInputPickerPanel);
		picker_popup->add_child(picker_panel);
		picker_panel->connect("value_picked", callable_mp(this, &WebInput::_on_picker_value));
		add_child(picker_popup, false, INTERNAL_MODE_FRONT);
	}

	const DateLocaleEntry &loc = date_locale_entry(_resolved_date_locale());
	Vector<String> months, weekdays, ampm;
	for (int i = 0; i < 12; i++) {
		months.push_back(String::utf8(loc.months[i]));
	}
	for (int i = 0; i < 7; i++) {
		weekdays.push_back(String::utf8(loc.weekdays[i]));
	}
	ampm.push_back(String::utf8(loc.am));
	ampm.push_back(String::utf8(loc.pm));

	bool hour24 = true;
	for (const DatePart &p : date_parts) {
		if (p.field == FIELD_HOUR12) {
			hour24 = false;
		}
	}
	// `step` is in seconds for the time-bearing types; the default (1 minute)
	// would make an unusable list, so fall back to half-hour rows like the
	// browser's drop-down.
	int step_minutes = 30;
	if (step_value >= 60 && (24 * 60 * 60) / step_value <= 288) {
		step_minutes = step_value / 60;
	}

	WebInputPickerPanel::Mode mode = WebInputPickerPanel::MODE_DATE;
	switch (input_type) {
		case TYPE_MONTH:
			mode = WebInputPickerPanel::MODE_MONTH;
			break;
		case TYPE_WEEK:
			mode = WebInputPickerPanel::MODE_WEEK;
			break;
		case TYPE_TIME:
			mode = WebInputPickerPanel::MODE_TIME;
			break;
		case TYPE_DATETIME_LOCAL:
			mode = WebInputPickerPanel::MODE_DATETIME;
			break;
		default:
			break;
	}

	picker_panel->set_picker_font(_get_font(), (int)Math::round(CHROME_FONT_SIZE));
	picker_panel->setup(mode, value, hour24, step_minutes, months, weekdays, ampm, loc.first_weekday);

	BoxModel b = _compute_box_model();
	Point2 origin = _compute_layout(b).input_origin;
	Size2 ps = picker_panel->get_panel_size();
	picker_panel->set_position(Point2());
	picker_panel->set_size(ps);
	Point2 screen = get_screen_position() + origin + Point2(0, b.border_box.y + 1);
	picker_popup->popup(Rect2i(Point2i(screen), Size2i(ps)));
}

void WebInput::show_picker() {
	_open_picker();
}

void WebInput::_on_picker_value(const String &p_value) {
	value = p_value;
	_date_parts_from_value();
	_layout_date_parts();
	update_minimum_size();
	queue_redraw();
	emit_signal(SNAME("value_changed"), value);
	// A calendar-only pick closes the drop-down; datetime-local stays open so
	// the time can be chosen from the same popup.
	if (picker_popup && input_type != TYPE_DATETIME_LOCAL) {
		picker_popup->hide();
	}
}

void WebInput::_draw_file(const BoxModel &p_box) {
	// Author CSS background/border/radius paints first (Chromium applies it to
	// the file input), then the "Choose File" button + label on top.
	_draw_box(p_box);
	// type=file has no outer border; it hosts a "Choose File" button on the left
	// and a status label to the right.
	Size2 sz = p_box.border_box;
	Ref<Font> f = _resolved_font();
	int fs = (int)Math::round(p_box.font_size);
	String btn_label = _ua_text("choose_file");
	// The button does not stretch with an explicit height: Chromium keeps it at
	// its intrinsic height, anchored to the top, and clips it to the box width.
	real_t btn_w = MIN(_file_button_width(p_box), sz.x);
	real_t btn_h = MIN(p_box.intrinsic_box.y > 0 ? p_box.intrinsic_box.y : sz.y, sz.y);

	// Button face + thin gray border (native look). Only the button itself
	// reacts to the pointer -- hovering the status label leaves it alone, as in
	// the browser.
	Color face = CHROME_BUTTON_BG;
	Color border = CHROME_FIELD_BORDER;
	if (disabled) {
		face = CHROME_DISABLED_BG;
		border = CHROME_DISABLED_BORDER;
	} else if (hovered_file_btn) {
		face = pressed ? CHROME_FACE_ACTIVE : CHROME_FACE_HOVER;
		border = pressed ? CHROME_BORDER_ACTIVE : CHROME_BORDER_HOVER;
	}
	Rect2 btn(0, 0, btn_w, btn_h);
	// The native face is a 1px edge on whole pixels around the fill.
	draw_rect(btn, border, true);
	draw_rect(btn.grow(-1), face, true);
	(void)fs;
	if (f.is_valid()) {
		real_t css = _date_font_size();
		real_t tw = _date_text_width(f, btn_label, css);
		real_t baseline = Math::round(_input_text_baseline(p_box));
		_draw_text_css(f, Point2((btn_w - tw) * 0.5, baseline), btn_label, css, CHROME_TEXT_FG);
		// Status label: on the button's line, after its 4px margin-right. An
		// explicit height leaves the row at the top, like the browser.
		Point2 lp(btn_w + 4, baseline);
		_draw_text_css(f, lp, _files_label(), css, CHROME_TEXT_FG, MAX((real_t)0.0, sz.x - lp.x));
	}
}

void WebInput::_load_src_texture() {
	src_texture = Ref<Texture2D>();
	if (src.is_empty()) {
		return;
	}
	if (src.begins_with("res://") || src.begins_with("user://")) {
		src_texture = ResourceLoader::load(src);
	} else {
		Ref<Image> img;
		img.instantiate();
		if (img->load(src) == OK) {
			src_texture = ImageTexture::create_from_image(img);
		}
	}
}

void WebInput::_draw_image(const BoxModel &p_box) {
	Size2 sz = p_box.border_box;
	if (src_texture.is_valid()) {
		draw_texture_rect(src_texture, Rect2(Point2(), sz), false);
		return;
	}
	// Broken-image fallback: Chromium draws no box border, just a small
	// placeholder icon and the alt text, at their natural size in the top-left
	// corner even when the element has been given a larger box.
	// The icon is a fixed 16px bitmap; it does not scale with the font.
	real_t icon = 16;
	if (theme_cache.broken_image.is_valid()) {
		draw_texture_rect(theme_cache.broken_image, Rect2(0, 0, icon, icon), false);
	} else {
		Rect2 ic(1, 1, icon - 2, icon - 2);
		draw_rect(ic, CHROME_FIELD_BORDER, false, 1.0);
		draw_line(ic.position, ic.position + ic.size, CHROME_FIELD_BORDER, 1.0, true);
	}
	Ref<Font> f = _resolved_font();
	if (f.is_valid()) {
		// The icon floats left; the alt text's line box starts at the top.
		_draw_text_css(f, Point2(icon, Math::round(_input_text_baseline(p_box))), _image_fallback_text(),
				_date_font_size(), p_box.text_color, MAX((real_t)0.0, sz.x - icon));
	}
}

String WebInput::_image_fallback_text() const {
	// Chromium's alternative text for an image button: alt, else value, else
	// the localized "Submit".
	if (!alt.is_empty()) {
		return alt;
	}
	return value.is_empty() ? _ua_text("submit") : value;
}

// ---------------------------------------------------------------------------
// File input: a "Choose File" button that opens a FileDialog, plus a label.
// ---------------------------------------------------------------------------
String WebInput::_files_label() const {
	if (selected_files.is_empty()) {
		return _ua_text("no_file");
	}
	if (selected_files.size() == 1) {
		return selected_files[0].get_file();
	}
	return itos(selected_files.size()) + " files";
}

real_t WebInput::_file_button_width(const BoxModel &p_box) const {
	Ref<Font> f = _resolved_font();
	int fs = (int)Math::round(p_box.font_size);
	real_t tw = f.is_valid() ? _date_text_width(f, _ua_text("choose_file"), _date_font_size()) : 60;
	(void)fs;
	return tw + 12 + 4; // padding 6px and the 2px (natively 1px-painted) border each side.
}

void WebInput::_open_file_dialog(FileTarget p_target) {
	if (disabled) {
		return;
	}
	file_target = p_target;
	if (!file_dialog) {
		file_dialog = memnew(FileDialog);
		file_dialog->set_access(FileDialog::ACCESS_FILESYSTEM);
		file_dialog->connect("files_selected", callable_mp(this, &WebInput::_on_files_chosen));
		file_dialog->connect("file_selected", callable_mp(this, &WebInput::_on_file_chosen));
		// Browsers hand type=file over to the OS file picker, so ask for the
		// native one. FileDialog falls back to its own window on platforms
		// without DisplayServer.FEATURE_NATIVE_DIALOG_FILE.
		file_dialog->set_use_native_dialog(true);
		add_child(file_dialog, false, INTERNAL_MODE_BACK);
	}
	bool many = multiple && p_target == FILE_TARGET_FILES;
	file_dialog->set_file_mode(many ? FileDialog::FILE_MODE_OPEN_FILES : FileDialog::FILE_MODE_OPEN_FILE);

	// Translate the HTML `accept` list into FileDialog filters.
	file_dialog->clear_filters();
	if (p_target == FILE_TARGET_SRC) {
		file_dialog->add_filter("*.png, *.jpg, *.jpeg, *.webp, *.bmp, *.svg", "Images");
	} else if (!accept.is_empty()) {
		Vector<String> toks = accept.split(",");
		String exts;
		for (const String &raw : toks) {
			String tk = raw.strip_edges();
			if (tk.begins_with(".")) {
				exts += (exts.is_empty() ? "" : ", ") + String("*") + tk;
			} else if (tk == "image/*") {
				exts += (exts.is_empty() ? "" : ", ") + String("*.png, *.jpg, *.jpeg, *.gif, *.webp, *.bmp");
			} else if (tk == "video/*") {
				exts += (exts.is_empty() ? "" : ", ") + String("*.mp4, *.webm, *.ogv, *.mov");
			} else if (tk == "audio/*") {
				exts += (exts.is_empty() ? "" : ", ") + String("*.mp3, *.wav, *.ogg");
			}
		}
		if (!exts.is_empty()) {
			file_dialog->add_filter(exts, "Accepted");
		}
	}
	file_dialog->popup_centered_ratio(0.5);
}

void WebInput::_on_files_chosen(const PackedStringArray &p_files) {
	if (file_target == FILE_TARGET_SRC) {
		if (!p_files.is_empty()) {
			set_src(p_files[0]);
			// Unlike type=file (which mirrors the browser's fake path), an image
			// button reports the real path, since that is what the picker is
			// there to produce.
			value = p_files[0];
			queue_redraw();
			emit_signal(SNAME("value_changed"), value);
		}
		return;
	}
	set_selected_files(p_files);
}

void WebInput::_on_file_chosen(const String &p_file) {
	// The single-selection signal carries the path directly; reading it back off
	// the dialog would not work for the native one.
	PackedStringArray files;
	files.push_back(p_file);
	_on_files_chosen(files);
}

void WebInput::set_selected_files(const PackedStringArray &p_files) {
	selected_files = p_files;
	// HTML exposes only the first file name (with the fakepath prefix) as value.
	value = selected_files.is_empty() ? String() : String("C:\\fakepath\\") + selected_files[0].get_file();
	queue_redraw();
	emit_signal(SNAME("value_changed"), value);
}

PackedStringArray WebInput::get_selected_files() const {
	return selected_files;
}

void WebInput::set_accept(const String &p_accept) {
	accept = p_accept;
}

String WebInput::get_accept() const {
	return accept;
}

// ---------------------------------------------------------------------------
// Form model: submit/reset act on sibling WebInputs (same parent). Full `form`
// attribute association across the tree is a later refinement.
// ---------------------------------------------------------------------------
void WebInput::reset_form() {
	Node *parent = get_parent();
	if (!parent) {
		return;
	}
	for (int i = 0; i < parent->get_child_count(); i++) {
		WebInput *w = Object::cast_to<WebInput>(parent->get_child(i));
		if (!w) {
			continue;
		}
		w->set_value(w->default_value);
		w->set_checked(w->default_checked);
	}
}

Dictionary WebInput::submit_form() {
	Dictionary out;
	Node *parent = get_parent();
	if (parent) {
		for (int i = 0; i < parent->get_child_count(); i++) {
			WebInput *w = Object::cast_to<WebInput>(parent->get_child(i));
			if (!w || w->attr_name.is_empty() || w->disabled) {
				continue;
			}
			switch (w->input_type) {
				case TYPE_SUBMIT:
				case TYPE_RESET:
				case TYPE_BUTTON:
				case TYPE_IMAGE:
					continue; // buttons are not successful controls here.
				case TYPE_CHECKBOX:
				case TYPE_RADIO:
					if (!w->checked) {
						continue;
					}
					out[w->attr_name] = w->value.is_empty() ? String("on") : w->value;
					break;
				default:
					out[w->attr_name] = w->value;
					break;
			}
		}
	}
	emit_signal(SNAME("form_submitted"), out);
	return out;
}

// ---------------------------------------------------------------------------
// Type / child management
// ---------------------------------------------------------------------------
void WebInput::_update_type() {
	// The editor child is created once in the constructor; here we just toggle
	// its visibility to match the current type.
	if (line_edit) {
		line_edit->set_visible(_is_text_family());
	}
	if (_is_datetime_family()) {
		// Parsing into the fields and reading them back drops anything the new
		// type cannot represent (e.g. "hello" or a time in a date field).
		String before = value;
		_build_date_parts();
		value = _date_iso_value();
		if (value != before) {
			emit_signal(SNAME("value_changed"), value);
		}
	} else {
		_coerce_value_for_type();
	}
	set_default_cursor_shape(_cursor_for_type()); // HTML UA cursor per type.
	_sync_line_edit();
	update_minimum_size();
	queue_redraw();
}

void WebInput::_sync_line_edit() {
	if (!line_edit) {
		return;
	}
	BoxModel b = _compute_box_model();
	Size2 sz = b.border_box; // the input box, not the labelled composite.
	Point2 origin = _compute_layout(b).input_origin;
	// Let the LineEdit fill the whole input box and inset its text via the
	// stylebox content margins (= border + padding). This is the LineEdit's
	// normal layout path, so its vertical text centering is correct.
	// type=number always reserves room for the spinner, which only becomes
	// visible on hover but never reflows the text. The editor is made narrower
	// rather than just inset, so that clicks on the spinner reach this control
	// instead of being swallowed by the LineEdit.
	real_t spin_w = ((input_type == TYPE_NUMBER) ? _spin_rect(b).size.x : 0.0) + _search_clear_width(b);
	if (le_box.is_valid()) {
		// The clipping wrapper is placed over the content box, so the editor
		// itself needs no insets; keeping them here would let the text run over
		// the padding and border instead of being clipped at the content edge.
		le_box->set_content_margin(SIDE_LEFT, 0);
		le_box->set_content_margin(SIDE_RIGHT, 0);
		// Vertically the editor is placed over the content box instead of being
		// inset, so that its own centring lands where the browser puts the text
		// at every box height; leaving margins here would raise the LineEdit's
		// minimum height and clamp short boxes.
		le_box->set_content_margin(SIDE_TOP, 0);
		le_box->set_content_margin(SIDE_BOTTOM, 0);
	}
	// Chromium centres the text in the content box, half a pixel above its
	// middle. Matching that means giving the editor exactly the content box:
	// when the box is shorter than the editor can be, the difference is taken
	// out of its position instead.
	real_t content_top = b.border[0] + b.padding[0];
	real_t content_h = sz.y - b.border[0] - b.border[2] - b.padding[0] - b.padding[2];
	real_t le_min_h = line_edit->get_combined_minimum_size().y;
	real_t vcorr = MIN((real_t)0.0, (content_h - le_min_h) * 0.5) - 0.5;
	real_t content_left = b.border[3] + b.padding[3];
	real_t content_w = sz.x - b.border[1] - b.border[3] - b.padding[1] - b.padding[3];
	Size2 edit_size(MAX((real_t)0.0, content_w - spin_w), MAX(content_h, le_min_h));
	if (editor_clip) {
		editor_clip->set_position(origin + Point2(content_left, content_top + vcorr));
		editor_clip->set_size(edit_size);
		line_edit->set_position(Point2());
	} else {
		line_edit->set_position(origin + Point2(0, content_top + vcorr));
	}
	line_edit->set_size(edit_size);
	line_edit->set_text(value);
	line_edit->set_placeholder(placeholder);
	line_edit->set_editable(!disabled && !readonly);
	line_edit->set_secret(input_type == TYPE_PASSWORD);
	if (maxlength >= 0) {
		line_edit->set_max_length(maxlength);
	}
	// Typography overrides (resolved from CSS, else UA defaults).
	line_edit->add_theme_font_override("font", _resolved_font());
	line_edit->add_theme_font_size_override("font_size", (int)Math::round(b.font_size));
	line_edit->add_theme_color_override("font_color", b.text_color);
	// Not editable (disabled or readonly), LineEdit draws with this colour:
	// a readonly field keeps its text colour, a disabled one has its own.
	line_edit->add_theme_color_override("font_uneditable_color", b.text_color);
	line_edit->add_theme_color_override("font_placeholder_color", theme_cache.font_placeholder_color);
	line_edit->add_theme_color_override("selection_color", theme_cache.selection_color);
	line_edit->add_theme_color_override("caret_color", theme_cache.caret_color);
	line_edit->set_horizontal_alignment(b.text_align >= 0 ? (HorizontalAlignment)b.text_align : HORIZONTAL_ALIGNMENT_LEFT);
}

String WebInput::filter_number_text(const String &p_text) {
	// Keep only characters valid in an HTML type=number field: digits, a leading
	// sign (or after an exponent), a single decimal point, and one exponent.
	String filtered;
	bool seen_dot = false;
	bool seen_e = false;
	for (int i = 0; i < p_text.length(); i++) {
		char32_t c = p_text[i];
		bool ok = false;
		if (c >= '0' && c <= '9') {
			ok = true;
		} else if (c == '-' || c == '+') {
			ok = (i == 0) || (i > 0 && (p_text[i - 1] == 'e' || p_text[i - 1] == 'E'));
		} else if (c == '.' && !seen_dot && !seen_e) {
			seen_dot = true;
			ok = true;
		} else if ((c == 'e' || c == 'E') && !seen_e && !filtered.is_empty()) {
			seen_e = true;
			ok = true;
		}
		if (ok) {
			filtered += String::chr(c);
		}
	}
	return filtered;
}

void WebInput::_on_line_edit_changed(const String &p_text) {
	String t = p_text;
	if (input_type == TYPE_NUMBER) {
		String filtered = filter_number_text(t);
		if (filtered != t && line_edit) {
			int caret = line_edit->get_caret_column() - (t.length() - filtered.length());
			line_edit->set_text(filtered);
			line_edit->set_caret_column(MAX(0, caret));
			t = filtered;
		}
	}
	value = t;
	emit_signal(SNAME("value_changed"), value);
}

void WebInput::_on_line_edit_submitted(const String &p_text) {
	value = p_text;
	emit_signal(SNAME("submitted"), value);
}

// Shift+Tab out of the embedded editor. Godot's backward traversal stops on
// this control (the editor's ancestor), whose FOCUS_ENTER hands focus straight
// back to the editor; step over the whole field instead, as the browser does.
void WebInput::_on_line_edit_gui_input(const Ref<InputEvent> &p_event) {
	if (!p_event->is_action_pressed(SNAME("ui_focus_prev"), true, true)) {
		return;
	}
	line_edit->accept_event();
	Control *prev = find_prev_valid_focus();
	if (prev && prev != this && !is_ancestor_of(prev)) {
		prev->grab_focus();
	}
}

// ---------------------------------------------------------------------------
// Notifications / input
// ---------------------------------------------------------------------------
void WebInput::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			_update_type();
		} break;

		case NOTIFICATION_RESIZED: {
			_sync_line_edit();
		} break;

		case NOTIFICATION_THEME_CHANGED: {
			_rebuild_css_font(); // theme weight/letter-spacing may have changed.
			if (_is_datetime_family()) {
				_layout_date_parts();
			}
			_sync_line_edit();
			update_minimum_size();
			queue_redraw();
		} break;

		case NOTIFICATION_MOUSE_ENTER: {
			hovered = true;
			queue_redraw();
		} break;

		case NOTIFICATION_MOUSE_EXIT: {
			hovered = false;
			hovered_spin = 0;
			hovered_file_btn = false;
			queue_redraw();
		} break;

		case NOTIFICATION_FOCUS_ENTER: {
			// Forward focus to the editor so the caret appears (text family).
			if (_is_text_family() && line_edit && line_edit->is_visible() && !line_edit->has_focus()) {
				line_edit->grab_focus();
			}
			if (_is_datetime_family() && focused_part < 0) {
				_focus_date_part(0, 1);
			}
			// Chromium's :focus-visible: text-entry and date/time fields always
			// show the ring, everything else only when focused from the keyboard.
			// The viewport grabs focus before gui_input runs, so the pointer is
			// detected from the live button state rather than from our own flag.
			bool from_pointer = focus_from_mouse ||
					Input::get_singleton()->is_mouse_button_pressed(MouseButton::LEFT);
			focus_visible = !from_pointer || _is_text_family() || _is_datetime_family();
			focus_from_mouse = false;
			queue_redraw();
		} break;

		case NOTIFICATION_FOCUS_EXIT: {
			focused_part = -1;
			part_typed = 0;
			queue_redraw();
		} break;

		case NOTIFICATION_DRAW: {
			if (input_type == TYPE_HIDDEN) {
				return;
			}
			BoxModel b = _compute_box_model();
			Layout L = _compute_layout(b);
			// Draw the wrapping <label> text (node-local coords).
			if (L.has_label) {
				// The label uses its own typography theme items (label_font /
				// label_font_size / label_font_color), independent of the input.
				Ref<Font> f = _label_draw_font();
				if (f.is_valid()) {
					int fs = (int)Math::round(_label_font_size());
					if (label_selection_to > label_selection_from) {
						Ref<TextLine> line = _shape_label();
						for (const Vector2 &range : TS->shaped_text_get_selection(line->get_rid(), label_selection_from, label_selection_to)) {
							draw_rect(Rect2(L.label_pos + Point2(range.x, 0), Size2(range.y - range.x, line->get_size().y)), theme_cache.selection_color);
						}
					}
					draw_base = Point2();
					_draw_text_css(f, L.label_pos + Point2(0, f->get_ascent(fs)), label_text, _label_font_size(),
							theme_cache.label_font_color);
					_draw_label_decorations(f, L.label_pos + Point2(0, f->get_ascent(fs)));
				}
			}
			// Shift the canvas so the input box draws at its origin within the node.
			draw_base = L.input_origin;
			if (L.input_origin != Point2()) {
				draw_set_transform(L.input_origin, 0.0, Size2(1, 1));
			}
			switch (input_type) {
				case TYPE_BUTTON:
				case TYPE_SUBMIT:
				case TYPE_RESET:
					_draw_button(b);
					break;
				case TYPE_CHECKBOX:
					_draw_checkbox(b, false);
					break;
				case TYPE_RADIO:
					_draw_checkbox(b, true);
					break;
				case TYPE_RANGE:
					_draw_range(b);
					break;
				case TYPE_COLOR:
					_draw_color(b);
					break;
				case TYPE_FILE:
					_draw_file(b);
					break;
				case TYPE_IMAGE:
					_draw_image(b);
					break;
				case TYPE_DATE:
				case TYPE_TIME:
				case TYPE_DATETIME_LOCAL:
				case TYPE_MONTH:
				case TYPE_WEEK:
					_draw_datetime(b);
					break;
				default:
					// text family: draw the field box (text via LineEdit child).
					_draw_text_content(b);
					break;
			}
			// Interaction-state overlays (theme styleboxes), like the browser's
			// :hover / :active / :focus. Drawn over the control's box.
			Rect2 box_rect(Point2(), b.border_box);
			if (input_type != TYPE_HIDDEN) {
				if (hovered && theme_cache.hover.is_valid()) {
					draw_style_box(theme_cache.hover, box_rect);
				}
				if (pressed && theme_cache.pressed.is_valid()) {
					draw_style_box(theme_cache.pressed, box_rect);
				}
				bool focused = has_focus() || (line_edit && line_edit->has_focus());
				// Text-entry and date/time fields always match :focus-visible;
				// the rest only when the focus did not come from a click. (The
				// text family focuses its LineEdit child directly, so its
				// visibility is decided here rather than in FOCUS_ENTER.)
				bool ring = focused && (focus_visible || _is_text_family() || _is_datetime_family());
				if (ring && theme_cache.focus.is_valid()) {
					draw_style_box(theme_cache.focus, box_rect);
				}
			}
			if (L.input_origin != Point2()) {
				draw_set_transform(Point2(), 0.0, Size2(1, 1)); // reset.
			}
		} break;
	}
}

void WebInput::gui_input(const Ref<InputEvent> &p_event) {
	if (disabled) {
		return;
	}

	// Date/time segment editing (keyboard).
	Ref<InputEventKey> k = p_event;
	if (k.is_valid() && _is_datetime_family()) {
		if (_date_key_input(k)) {
			accept_event();
		}
		return;
	}
	if (k.is_valid() && _activation_key_input(k)) {
		accept_event();
		return;
	}

	// Pointer bookkeeping for the sub-parts that light up on their own: the
	// type=number spinner halves and type=file's "Choose File" button.
	Ref<InputEventMouseMotion> hover_mm = p_event;
	if (hover_mm.is_valid()) {
		BoxModel hb = _compute_box_model();
		Point2 local = hover_mm->get_position() - _compute_layout(hb).input_origin;
		int spin = 0;
		bool on_file_btn = false;
		if (input_type == TYPE_NUMBER) {
			Rect2 sr = _spin_rect(hb);
			if (sr.has_point(local)) {
				spin = (local.y < sr.position.y + sr.size.y * 0.5) ? 1 : -1;
			}
		} else if (input_type == TYPE_FILE) {
			on_file_btn = Rect2(Point2(), Size2(_file_button_width(hb), hb.border_box.y)).has_point(local);
		}
		if (spin != hovered_spin || on_file_btn != hovered_file_btn) {
			hovered_spin = spin;
			hovered_file_btn = on_file_btn;
			queue_redraw();
		}
	}

	Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_valid() && mb->get_button_index() == MouseButton::LEFT) {
		if (mb->is_pressed()) {
			pressed = true;
			focus_from_mouse = true;
			queue_redraw();
			BoxModel pb = _compute_box_model();
			Point2 plocal = mb->get_position() - _compute_layout(pb).input_origin;
			if (input_type == TYPE_NUMBER) {
				Rect2 sr = _spin_rect(pb);
				if (sr.has_point(plocal)) {
					_step_number(plocal.y < sr.position.y + sr.size.y * 0.5 ? 1 : -1);
					accept_event();
					return;
				}
			}
			if (input_type == TYPE_CHECKBOX) {
				set_checked(!checked);
				emit_signal(SNAME("toggled"), checked);
			} else if (input_type == TYPE_RADIO) {
				if (!checked) {
					set_checked(true);
					_enforce_radio_group();
					emit_signal(SNAME("toggled"), checked);
				}
			} else if (input_type == TYPE_RANGE) {
				_set_range_from_pos(mb->get_position().x);
			} else if (input_type == TYPE_COLOR) {
				_open_color_picker();
			} else if (input_type == TYPE_FILE) {
				// Only the button opens the dialog; the status label is inert.
				if (plocal.x <= _file_button_width(pb)) {
					_open_file_dialog(FILE_TARGET_FILES);
				}
			} else if (_is_datetime_family()) {
				grab_focus();
				BoxModel db = _compute_box_model();
				Point2 local = mb->get_position() - _compute_layout(db).input_origin;
				real_t ind = _date_indicator_size();
				if (local.x >= db.border_box.x - db.border[1] - ind) {
					_open_picker();
				} else {
					int part = _date_part_at(local);
					if (part >= 0) {
						_focus_date_part(part, 1);
					} else if (focused_part < 0) {
						_focus_date_part(0, 1);
					}
				}
				queue_redraw();
			}
			accept_event();
		} else if (pressed) {
			pressed = false;
			queue_redraw();
			if (input_type == TYPE_IMAGE) {
				// Lets the image the button shows be chosen from the OS picker.
				_open_file_dialog(FILE_TARGET_SRC);
			}
			if (input_type == TYPE_SUBMIT) {
				submit_form();
				emit_signal(SNAME("pressed"));
			} else if (input_type == TYPE_RESET) {
				reset_form();
				emit_signal(SNAME("pressed"));
			} else if (input_type == TYPE_BUTTON || input_type == TYPE_IMAGE) {
				emit_signal(SNAME("pressed"));
			}
			accept_event();
		}
	}

	// Range: drag the thumb.
	Ref<InputEventMouseMotion> mm = p_event;
	if (mm.is_valid() && input_type == TYPE_RANGE && pressed) {
		_set_range_from_pos(mm->get_position().x);
		accept_event();
	}

	// Number: scroll wheel steps the value.
	if (mb.is_valid() && input_type == TYPE_NUMBER && mb->is_pressed()) {
		if (mb->get_button_index() == MouseButton::WHEEL_UP) {
			_step_number(1);
			accept_event();
		} else if (mb->get_button_index() == MouseButton::WHEEL_DOWN) {
			_step_number(-1);
			accept_event();
		}
	}
}

// ---------------------------------------------------------------------------
// Bridge contract
// ---------------------------------------------------------------------------
static const char *TYPE_STRINGS[WebInput::TYPE_MAX] = {
	"text", "password", "email", "url", "tel", "search", "number",
	"button", "submit", "reset", "image", "checkbox", "radio", "range",
	"color", "file", "hidden", "date", "time", "datetime-local", "month", "week"
};

void WebInput::set_input_type_string(const String &p_type) {
	for (int i = 0; i < TYPE_MAX; i++) {
		if (p_type == TYPE_STRINGS[i]) {
			set_input_type((Type)i);
			return;
		}
	}
	ERR_PRINT(vformat("WebInput: unknown input type '%s'", p_type));
}

String WebInput::get_input_type_string() const {
	return TYPE_STRINGS[input_type];
}

void WebInput::set_html_attribute(const String &p_name, const Variant &p_value) {
	String n = p_name;
	if (n == "type") {
		set_input_type_string(p_value);
	} else if (n == "value") {
		set_value(p_value);
		default_value = p_value; // markup value == defaultValue for form reset.
	} else if (n == "placeholder") {
		set_placeholder(p_value);
	} else if (n == "name") {
		set_input_name(p_value);
	} else if (n == "disabled") {
		set_disabled(p_value);
	} else if (n == "readonly") {
		set_readonly(p_value);
	} else if (n == "required") {
		set_required(p_value);
	} else if (n == "checked") {
		set_checked(p_value);
		default_checked = p_value;
	} else if (n == "width") {
		set_image_width(p_value);
	} else if (n == "height") {
		set_image_height(p_value);
	} else if (n == "accept") {
		set_accept(p_value);
	} else if (n == "size") {
		set_size_chars(p_value);
	} else if (n == "maxlength") {
		set_maxlength(p_value);
	} else if (n == "min") {
		set_min_value((int)String(p_value).to_int());
		min_attr = p_value;
		update_minimum_size();
	} else if (n == "max") {
		set_max_value((int)String(p_value).to_int());
		max_attr = p_value;
		update_minimum_size();
	} else if (n == "step") {
		set_step_value((int)String(p_value).to_int());
		step_attr = p_value;
		update_minimum_size();
	} else if (n == "lang") {
		// A `lang` attribute picks the date/time editor's locale, the way the
		// browser's UI language does.
		set_date_locale(p_value);
	} else if (n == "pattern") {
		set_pattern(p_value);
	} else if (n == "multiple") {
		set_multiple(p_value);
	} else if (n == "src") {
		set_src(p_value);
	} else if (n == "alt") {
		set_alt(p_value);
	} else {
		extra_attributes[n] = p_value;
	}
}

Variant WebInput::get_html_attribute(const String &p_name) const {
	if (extra_attributes.has(p_name)) {
		return extra_attributes[p_name];
	}
	return Variant();
}

// ---------------------------------------------------------------------------
// CSS author styling
// ---------------------------------------------------------------------------
bool WebInput::_parse_css_length(const String &p_s, real_t &r_out) {
	String s = p_s.strip_edges().to_lower();
	if (s.is_empty()) {
		return false;
	}
	if (s.ends_with("px")) {
		s = s.substr(0, s.length() - 2);
	} else if (s.ends_with("pt")) {
		String n = s.substr(0, s.length() - 2);
		if (n.is_valid_float()) {
			r_out = n.to_float() * 1.3333;
			return true;
		}
		return false;
	}
	if (s.is_valid_float()) {
		r_out = s.to_float();
		return true;
	}
	return false;
}

bool WebInput::_parse_css_color(const String &p_s, Color &r_out) {
	String s = p_s.strip_edges();
	if (s.is_empty()) {
		return false;
	}
	String low = s.to_lower();
	if (low == "transparent") {
		r_out = Color(0, 0, 0, 0);
		return true;
	}
	if (low.begins_with("rgb")) {
		int o = low.find("(");
		int c = low.find(")");
		if (o < 0 || c < 0) {
			return false;
		}
		String inner = low.substr(o + 1, c - o - 1).replace("/", ",");
		Vector<String> parts = inner.split(",", false);
		if (parts.size() < 3) {
			return false;
		}
		float v[4] = { 0, 0, 0, 1 };
		for (int i = 0; i < parts.size() && i < 4; i++) {
			String p = parts[i].strip_edges();
			if (p.ends_with("%")) {
				v[i] = p.substr(0, p.length() - 1).to_float() / 100.0 * (i < 3 ? 255.0 : 1.0);
			} else {
				v[i] = p.to_float();
			}
		}
		r_out = Color(v[0] / 255.0, v[1] / 255.0, v[2] / 255.0, parts.size() > 3 ? v[3] : 1.0);
		return true;
	}
	if (s.begins_with("#") && s.is_valid_html_color()) {
		r_out = Color::html(s);
		return true;
	}
	Color named = Color::named(low, Color(-1, -1, -1, -1));
	if (named.r >= 0.0) {
		r_out = named;
		return true;
	}
	return false;
}

int WebInput::_parse_css_font_weight(const String &p_s) {
	String s = p_s.strip_edges().to_lower();
	if (s == "normal") {
		return 400;
	}
	if (s == "bold" || s == "bolder") {
		return 700;
	}
	if (s == "lighter") {
		return 300;
	}
	if (s.is_valid_int()) {
		return s.to_int();
	}
	return 400;
}

// Expand a 1-4 value CSS box shorthand into T,R,B,L (or TL,TR,BR,BL).
static void _css_expand_box(const String &p_val, bool (&r_has)[4], real_t (&r_out)[4], bool p_radius) {
	Vector<String> parts = p_val.split(" ", false);
	real_t v[4] = { 0, 0, 0, 0 };
	int n = 0;
	for (const String &p : parts) {
		real_t len;
		if (WebInput::_parse_css_length(p, len)) {
			if (n < 4) {
				v[n++] = len;
			}
		}
	}
	if (n == 0) {
		return;
	}
	real_t t, rr, b, l;
	if (n == 1) {
		t = rr = b = l = v[0];
	} else if (n == 2) {
		t = b = v[0];
		rr = l = v[1];
	} else if (n == 3) {
		t = v[0];
		rr = l = v[1];
		b = v[2];
	} else {
		t = v[0];
		rr = v[1];
		b = v[2];
		l = v[3];
	}
	// For radius the order is TL,TR,BR,BL; the same 1-4 expansion semantics map
	// onto the same array slots we use (0=TL/T,1=TR/R,2=BR/B,3=BL/L).
	r_out[0] = t;
	r_out[1] = rr;
	r_out[2] = b;
	r_out[3] = l;
	for (int i = 0; i < 4; i++) {
		r_has[i] = true;
	}
}

static int _css_side_from_prop(const String &p_prop) {
	if (p_prop.contains("-top")) {
		return 0;
	}
	if (p_prop.contains("-right")) {
		return 1;
	}
	if (p_prop.contains("-bottom")) {
		return 2;
	}
	if (p_prop.contains("-left")) {
		return 3;
	}
	return -1;
}

static int _css_text_align(const String &p_val) {
	String v = p_val.strip_edges().to_lower();
	if (v == "center") {
		return (int)HORIZONTAL_ALIGNMENT_CENTER;
	}
	if (v == "right" || v == "end") {
		return (int)HORIZONTAL_ALIGNMENT_RIGHT;
	}
	if (v == "justify") {
		return (int)HORIZONTAL_ALIGNMENT_FILL;
	}
	return (int)HORIZONTAL_ALIGNMENT_LEFT;
}

void WebInput::set_css(const String &p_property, const String &p_value) {
	String prop = p_property.strip_edges().to_lower();
	String val = p_value.strip_edges();
	real_t len;
	Color col;

	if (prop == "background-color" || prop == "background") {
		if (_parse_css_color(val, col)) {
			css.has_bg = true;
			css.bg = col;
		}
	} else if (prop == "color") {
		if (_parse_css_color(val, col)) {
			css.has_color = true;
			css.color = col;
		}
	} else if (prop == "border") {
		Vector<String> toks = val.split(" ", false);
		for (const String &t : toks) {
			String low = t.to_lower();
			if (low == "none" || low == "hidden") {
				css.border_none = true;
			} else if (low == "solid" || low == "dashed" || low == "dotted" || low == "double" || low == "groove" || low == "ridge" || low == "inset" || low == "outset") {
				css.border_none = false;
			} else if (_parse_css_length(t, len)) {
				for (int i = 0; i < 4; i++) {
					css.has_bw[i] = true;
					css.bw[i] = len;
				}
			} else if (_parse_css_color(t, col)) {
				css.has_bcol = true;
				css.bcol = col;
			}
		}
	} else if (prop == "border-width") {
		_css_expand_box(val, css.has_bw, css.bw, false);
	} else if (prop == "border-color") {
		if (_parse_css_color(val, col)) {
			css.has_bcol = true;
			css.bcol = col;
		}
	} else if (prop == "border-style") {
		css.border_none = (val.to_lower() == "none" || val.to_lower() == "hidden");
	} else if (prop == "border-radius") {
		_css_expand_box(val, css.has_radius, css.radius, true);
	} else if (prop.begins_with("border-") && prop.ends_with("-width")) {
		int side = _css_side_from_prop(prop);
		if (side >= 0 && _parse_css_length(val, len)) {
			css.has_bw[side] = true;
			css.bw[side] = len;
		}
	} else if (prop == "padding") {
		_css_expand_box(val, css.has_pad, css.pad, false);
	} else if (prop.begins_with("padding-")) {
		int side = _css_side_from_prop(prop);
		if (side >= 0 && _parse_css_length(val, len)) {
			css.has_pad[side] = true;
			css.pad[side] = len;
		}
	} else if (prop == "box-sizing") {
		css.has_box_sizing = true;
		css.border_box = (val.to_lower() == "border-box");
	} else if (prop == "width") {
		if (_parse_css_length(val, len)) {
			css.has_width = true;
			css.width = len;
		}
	} else if (prop == "height") {
		if (_parse_css_length(val, len)) {
			css.has_height = true;
			css.height = len;
		}
	} else if (prop == "box-shadow") {
		String v = val;
		Color sc(0, 0, 0, 1);
		int rgbi = v.findn("rgb");
		if (rgbi >= 0) {
			int close = v.find(")", rgbi);
			if (close >= 0) {
				_parse_css_color(v.substr(rgbi, close - rgbi + 1), sc);
				v = v.substr(0, rgbi) + " " + v.substr(close + 1);
			}
		}
		Vector<real_t> nums;
		for (const String &t : v.split(" ", false)) {
			String tl = t.strip_edges();
			if (tl.is_empty() || tl.to_lower() == "inset") {
				continue;
			}
			real_t l2;
			if (_parse_css_length(tl, l2)) {
				nums.push_back(l2);
			} else {
				Color c2;
				if (_parse_css_color(tl, c2)) {
					sc = c2;
				}
			}
		}
		css.has_shadow = true;
		css.shadow_offset = Vector2(nums.size() > 0 ? nums[0] : 0, nums.size() > 1 ? nums[1] : 0);
		css.shadow_blur = nums.size() > 2 ? nums[2] : 0;
		css.shadow_spread = nums.size() > 3 ? nums[3] : 0;
		css.shadow_color = sc;
	} else if (prop == "font-size") {
		if (_parse_css_length(val, len)) {
			css.has_font_size = true;
			css.font_size = len;
		}
	} else if (prop == "font-family") {
		css.font_family = val;
	} else if (prop == "font-weight") {
		css.has_font_weight = true;
		css.font_weight = _parse_css_font_weight(val);
	} else if (prop == "line-height") {
		String low = val.to_lower();
		real_t base = css.has_font_size ? css.font_size : CHROME_FONT_SIZE;
		if (low == "normal") {
			css.has_line_height = false;
		} else if (low.ends_with("px") && _parse_css_length(val, len)) {
			css.has_line_height = true;
			css.line_height = len;
		} else if (low.is_valid_float()) {
			css.has_line_height = true;
			css.line_height = low.to_float() * base;
		}
	} else if (prop == "text-align") {
		css.has_text_align = true;
		css.text_align = _css_text_align(val);
	} else if (prop == "letter-spacing") {
		if (val.to_lower() == "normal") {
			css.has_letter_spacing = false;
		} else if (_parse_css_length(val, len)) {
			css.has_letter_spacing = true;
			css.letter_spacing = len;
		}
	} else {
		return; // unsupported property: ignore.
	}

	_rebuild_css_font();
	if (_is_datetime_family()) {
		_layout_date_parts();
	}
	update_minimum_size();
	_sync_line_edit();
	queue_redraw();
}

void WebInput::set_css_text(const String &p_css) {
	for (const String &decl : p_css.split(";", false)) {
		int colon = decl.find(":");
		if (colon < 0) {
			continue;
		}
		set_css(decl.substr(0, colon), decl.substr(colon + 1));
	}
}

void WebInput::clear_css() {
	css = CssStyle();
	css_font = Ref<Font>();
	if (_is_datetime_family()) {
		_layout_date_parts();
	}
	update_minimum_size();
	_sync_line_edit();
	queue_redraw();
}

Dictionary WebInput::get_test_metrics() const {
	BoxModel b = _compute_box_model();
	Dictionary d;
	Size2 sz = get_size();

	Dictionary size_d;
	size_d["w"] = sz.x;
	size_d["h"] = sz.y;
	d["size"] = size_d;

	Dictionary border_d;
	border_d["top"] = b.border[0];
	border_d["right"] = b.border[1];
	border_d["bottom"] = b.border[2];
	border_d["left"] = b.border[3];
	d["border"] = border_d;

	Dictionary pad_d;
	pad_d["top"] = b.padding[0];
	pad_d["right"] = b.padding[1];
	pad_d["bottom"] = b.padding[2];
	pad_d["left"] = b.padding[3];
	d["padding"] = pad_d;

	Array bg;
	bg.push_back(b.background.r);
	bg.push_back(b.background.g);
	bg.push_back(b.background.b);
	bg.push_back(b.background.a);
	d["background_color"] = bg;

	Array tc;
	tc.push_back(b.text_color.r);
	tc.push_back(b.text_color.g);
	tc.push_back(b.text_color.b);
	tc.push_back(b.text_color.a);
	d["text_color"] = tc;

	Array bc;
	bc.push_back(b.border_color.r);
	bc.push_back(b.border_color.g);
	bc.push_back(b.border_color.b);
	bc.push_back(b.border_color.a);
	d["border_color"] = bc;

	d["font_size"] = b.font_size;

	// CSS author-overridable extras (resolved values).
	Dictionary radius_d;
	radius_d["top_left"] = b.radius[0];
	radius_d["top_right"] = b.radius[1];
	radius_d["bottom_right"] = b.radius[2];
	radius_d["bottom_left"] = b.radius[3];
	d["border_radius"] = radius_d;

	Dictionary shadow_d;
	shadow_d["present"] = b.has_shadow;
	shadow_d["offset_x"] = b.shadow_offset.x;
	shadow_d["offset_y"] = b.shadow_offset.y;
	shadow_d["blur"] = b.shadow_blur;
	shadow_d["spread"] = b.shadow_spread;
	Array sc;
	sc.push_back(b.shadow_color.r);
	sc.push_back(b.shadow_color.g);
	sc.push_back(b.shadow_color.b);
	sc.push_back(b.shadow_color.a);
	shadow_d["color"] = sc;
	d["box_shadow"] = shadow_d;

	d["font_weight"] = b.font_weight;
	d["line_height"] = b.line_height; // 0 => normal.
	d["letter_spacing"] = b.letter_spacing;
	// text-align as CSS keyword (default left for unset).
	int ta = b.text_align;
	d["text_align"] = (ta == (int)HORIZONTAL_ALIGNMENT_CENTER) ? "center" : (ta == (int)HORIZONTAL_ALIGNMENT_RIGHT) ? "right"
			: (ta == (int)HORIZONTAL_ALIGNMENT_FILL)                                                              ? "justify"
																												  : "left";

	d["type"] = get_input_type_string();
	return d;
}

bool WebInput::check_validity() const {
	// HTML constraint validation (subset; `pattern` regex wiring is deferred to
	// avoid a hard dependency on the regex module from the scene layer).
	const String v = value;

	if (required) {
		if (input_type == TYPE_CHECKBOX || input_type == TYPE_RADIO) {
			if (!checked) {
				return false;
			}
		} else if (v.is_empty()) {
			return false;
		}
	}

	if (_is_text_family() && !v.is_empty()) {
		if (minlength >= 0 && v.length() < minlength) {
			return false;
		}
		if (maxlength >= 0 && v.length() > maxlength) {
			return false;
		}
		if (input_type == TYPE_EMAIL) {
			// Minimal email shape: local@domain.tld (or comma-separated if multiple).
			Vector<String> parts;
			if (multiple) {
				parts = v.split(",");
			} else {
				parts.push_back(v);
			}
			for (const String &raw : parts) {
				String e = raw.strip_edges();
				int at = e.find("@");
				if (at <= 0 || e.rfind("@") != at) {
					return false;
				}
				String domain = e.substr(at + 1);
				if (domain.is_empty() || !domain.contains(".") || domain.begins_with(".") || domain.ends_with(".")) {
					return false;
				}
			}
		}
		if (input_type == TYPE_URL) {
			if (!v.contains("://") || v.find("://") == 0) {
				return false;
			}
		}
		if (!pattern.is_empty()) {
			// HTML `pattern` is a full-string match. Instantiate RegEx via
			// ClassDB so the scene layer keeps no hard link to the regex module;
			// if the module is unavailable the constraint is simply skipped.
			Ref<RefCounted> re = Object::cast_to<RefCounted>(ClassDB::instantiate("RegEx"));
			if (re.is_valid()) {
				int err = (int)re->call("compile", String("^(?:") + pattern + ")$");
				if (err == OK) {
					Variant m = re->call("search", v);
					Object *match = m;
					if (match == nullptr) {
						return false;
					}
				}
			}
		}
	}

	if ((input_type == TYPE_NUMBER || input_type == TYPE_RANGE) && !v.is_empty()) {
		if (!v.is_valid_float()) {
			return false;
		}
		double d = v.to_float();
		if (d < (double)min_value) {
			return false;
		}
		if (d > (double)max_value) {
			return false;
		}
	}

	return true;
}

// ---------------------------------------------------------------------------
// Property accessors
// ---------------------------------------------------------------------------
void WebInput::set_input_type(Type p_type) {
	ERR_FAIL_INDEX(p_type, TYPE_MAX);
	if (input_type == p_type) {
		return;
	}
	input_type = p_type;
	_update_type();
}

WebInput::Type WebInput::get_input_type() const {
	return input_type;
}

void WebInput::set_value(const String &p_value) {
	if (value == p_value) {
		return;
	}
	value = p_value;
	if (line_edit) {
		line_edit->set_text(value);
	}
	if (_is_datetime_family()) {
		_date_parts_from_value();
		_layout_date_parts();
	}
	update_minimum_size();
	queue_redraw();
}

String WebInput::get_value() const {
	return value;
}

void WebInput::set_placeholder(const String &p_placeholder) {
	placeholder = p_placeholder;
	if (line_edit) {
		line_edit->set_placeholder(placeholder);
	}
	queue_redraw();
}

String WebInput::get_placeholder() const {
	return placeholder;
}

void WebInput::set_input_name(const String &p_name) {
	attr_name = p_name;
}

String WebInput::get_input_name() const {
	return attr_name;
}

void WebInput::set_draw_box(bool p_draw) {
	if (draw_box == p_draw) {
		return;
	}
	draw_box = p_draw;
	queue_redraw();
}

bool WebInput::is_drawing_box() const {
	return draw_box;
}

void WebInput::set_disabled(bool p_disabled) {
	disabled = p_disabled;
	if (line_edit) {
		line_edit->set_editable(!disabled && !readonly);
	}
	queue_redraw();
}

bool WebInput::is_disabled() const {
	return disabled;
}

void WebInput::set_readonly(bool p_readonly) {
	readonly = p_readonly;
	if (line_edit) {
		line_edit->set_editable(!disabled && !readonly);
	}
}

bool WebInput::is_readonly() const {
	return readonly;
}

void WebInput::set_required(bool p_required) {
	required = p_required;
}

bool WebInput::is_required() const {
	return required;
}

void WebInput::set_checked(bool p_checked) {
	if (checked == p_checked) {
		return;
	}
	checked = p_checked;
	if (checked && input_type == TYPE_RADIO) {
		_enforce_radio_group();
	}
	queue_redraw();
}

bool WebInput::is_checked() const {
	return checked;
}

void WebInput::set_size_chars(int p_size) {
	p_size = MAX(1, p_size);
	if (size_chars == p_size) {
		return;
	}
	size_chars = p_size;
	update_minimum_size();
	_sync_line_edit();
	queue_redraw();
}

int WebInput::get_size_chars() const {
	return size_chars;
}

void WebInput::set_input_width(int p_width) {
	if (input_width == p_width) {
		return;
	}
	input_width = p_width;
	update_minimum_size();
	_sync_line_edit();
	queue_redraw();
}
int WebInput::get_input_width() const {
	return input_width;
}
void WebInput::set_input_height(int p_height) {
	if (input_height == p_height) {
		return;
	}
	input_height = p_height;
	update_minimum_size();
	_sync_line_edit();
	queue_redraw();
}
int WebInput::get_input_height() const {
	return input_height;
}

void WebInput::set_maxlength(int p_maxlength) {
	maxlength = p_maxlength;
	if (line_edit && maxlength >= 0) {
		line_edit->set_max_length(maxlength);
	}
}

int WebInput::get_maxlength() const {
	return maxlength;
}

void WebInput::set_min_value(int p_min) {
	min_value = p_min;
}
int WebInput::get_min_value() const {
	return min_value;
}
void WebInput::set_max_value(int p_max) {
	max_value = p_max;
}
int WebInput::get_max_value() const {
	return max_value;
}
void WebInput::set_step_value(int p_step) {
	step_value = p_step;
}
int WebInput::get_step_value() const {
	return step_value;
}
void WebInput::set_pattern(const String &p_pattern) {
	pattern = p_pattern;
}
String WebInput::get_pattern() const {
	return pattern;
}
void WebInput::set_multiple(bool p_multiple) {
	multiple = p_multiple;
}
bool WebInput::is_multiple() const {
	return multiple;
}
void WebInput::set_src(const String &p_src) {
	src = p_src;
	_load_src_texture();
	update_minimum_size();
	queue_redraw();
}
String WebInput::get_src() const {
	return src;
}
void WebInput::set_alt(const String &p_alt) {
	alt = p_alt;
	queue_redraw();
}
String WebInput::get_alt() const {
	return alt;
}
void WebInput::set_label(const String &p_label) {
	if (label_text == p_label) {
		return;
	}
	label_text = p_label;
	label_selection_from = label_selection_to = 0;
	update_minimum_size();
	_sync_line_edit();
	queue_redraw();
}
void WebInput::set_date_locale(const String &p_locale) {
	if (date_locale == p_locale) {
		return;
	}
	date_locale = p_locale;
	if (_is_datetime_family()) {
		_build_date_parts();
		update_minimum_size();
		queue_redraw();
	}
}

String WebInput::get_date_locale() const {
	return date_locale;
}

String WebInput::get_label() const {
	return label_text;
}

Ref<TextLine> WebInput::_shape_label() const {
	return memnew(TextLine(label_text, _label_draw_font(), (int)Math::round(_label_font_size())));
}

Rect2 WebInput::get_label_text_rect() const {
	if (label_text.is_empty()) {
		return Rect2();
	}
	return Rect2(_compute_layout(_compute_box_model()).label_pos, _shape_label()->get_size());
}

int WebInput::get_label_column_at_pos(const Point2 &p_position) const {
	return _shape_label()->hit_test(p_position.x - get_label_text_rect().position.x);
}

Point2 WebInput::get_label_caret_position(int p_column) const {
	Ref<TextLine> line = _shape_label();
	CaretInfo caret = TS->shaped_text_get_carets(line->get_rid(), CLAMP(p_column, 0, label_text.length()));
	Rect2 rect = get_label_text_rect();
	return rect.position + Point2(caret.l_caret.position.x, rect.size.y * 0.5);
}

void WebInput::set_label_selection(int p_from, int p_to) {
	int from = CLAMP(MIN(p_from, p_to), 0, label_text.length());
	int to = CLAMP(MAX(p_from, p_to), 0, label_text.length());
	if (from == label_selection_from && to == label_selection_to) {
		return;
	}
	label_selection_from = from;
	label_selection_to = to;
	queue_redraw();
}

String WebInput::get_selected_label_text() const {
	return label_text.substr(label_selection_from, label_selection_to - label_selection_from);
}

void WebInput::activate_label() {
	if (disabled || !is_visible_in_tree()) {
		return;
	}
	if (line_edit && _is_text_family()) {
		line_edit->grab_focus();
		return;
	}
	grab_focus();
	if (input_type == TYPE_RANGE) { return; }
	if (input_type == TYPE_IMAGE) { emit_signal(SNAME("pressed")); return; }
	// Dispatch the associated control's activation only after an undragged
	// label release. A selection drag must never toggle/submit the control.
	Ref<InputEventMouseButton> button;
	button.instantiate();
	button->set_button_index(MouseButton::LEFT);
	Layout layout = _compute_layout(_compute_box_model());
	button->set_position(layout.input_origin + Point2(1, 1));
	button->set_pressed(true);
	gui_input(button);
	button->set_pressed(false);
	gui_input(button);
}

void WebInput::set_label_cursor_shape(CursorShape p_shape) {
	label_cursor_shape = p_shape;
}

Control::CursorShape WebInput::get_label_cursor_shape() const {
	return label_cursor_shape;
}

Control::CursorShape WebInput::get_cursor_shape(const Point2 &p_position) const {
	if (!label_text.is_empty() && get_label_text_rect().has_point(p_position)) { return label_cursor_shape; }
	return Control::get_cursor_shape(p_position);
}
void WebInput::set_label_position(LabelPosition p_position) {
	if (label_position == p_position) {
		return;
	}
	label_position = p_position;
	update_minimum_size();
	_sync_line_edit();
	queue_redraw();
}
void WebInput::set_label_wrap_width(real_t p_width) {
	if (Math::is_equal_approx(label_wrap_width, p_width)) {
		return;
	}
	label_wrap_width = p_width;
	update_minimum_size();
	_sync_line_edit();
	queue_redraw();
}
real_t WebInput::get_label_wrap_width() const {
	return label_wrap_width;
}

void WebInput::set_label_max_width(real_t p_width) {
	if (Math::is_equal_approx(label_max_width, p_width)) {
		return;
	}
	label_max_width = p_width;
	_sync_line_edit();
	queue_redraw();
}

real_t WebInput::get_label_max_width() const {
	return label_max_width;
}
void WebInput::set_label_letter_spacing(real_t p_spacing) {
	if (Math::is_equal_approx(label_letter_spacing, p_spacing)) {
		return;
	}
	label_letter_spacing = p_spacing;
	update_minimum_size();
	_sync_line_edit();
	queue_redraw();
}
real_t WebInput::get_label_letter_spacing() const {
	return label_letter_spacing;
}
void WebInput::set_label_decoration_lines(int p_lines) {
	label_decoration_lines = p_lines;
	queue_redraw();
}
int WebInput::get_label_decoration_lines() const {
	return label_decoration_lines;
}
void WebInput::set_label_decoration_color(const Color &p_color) {
	label_decoration_color = p_color;
	queue_redraw();
}
Color WebInput::get_label_decoration_color() const {
	return label_decoration_color;
}
void WebInput::set_label_decoration_style(int p_style) {
	label_decoration_style = p_style;
	queue_redraw();
}
int WebInput::get_label_decoration_style() const {
	return label_decoration_style;
}
void WebInput::set_label_decoration_thickness(real_t p_thickness) {
	label_decoration_thickness = p_thickness;
	queue_redraw();
}
real_t WebInput::get_label_decoration_thickness() const {
	return label_decoration_thickness;
}
void WebInput::set_label_alignment(int p_alignment) {
	if (label_alignment == p_alignment) {
		return;
	}
	label_alignment = p_alignment;
	update_minimum_size();
	_sync_line_edit();
	queue_redraw();
}
int WebInput::get_label_alignment() const {
	return label_alignment;
}
WebInput::LabelPosition WebInput::get_label_position() const {
	return label_position;
}
void WebInput::set_image_width(int p_w) {
	image_width = p_w;
	update_minimum_size();
	queue_redraw();
}
int WebInput::get_image_width() const {
	return image_width;
}
void WebInput::set_image_height(int p_h) {
	image_height = p_h;
	update_minimum_size();
	queue_redraw();
}
int WebInput::get_image_height() const {
	return image_height;
}

// ---------------------------------------------------------------------------
void WebInput::set_external_color_picker_enabled(bool p_enabled) {
	external_color_picker_enabled = p_enabled;
	if (p_enabled && color_popup != nullptr) {
		color_popup->hide();
	}
}

bool WebInput::is_external_color_picker_enabled() const {
	return external_color_picker_enabled;
}

void WebInput::apply_external_color(const Color &p_color) {
	if (input_type == TYPE_COLOR && !disabled && !readonly) {
		_on_color_picked(p_color);
	}
}

void WebInput::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_input_type", "type"), &WebInput::set_input_type);
	ClassDB::bind_method(D_METHOD("get_input_type"), &WebInput::get_input_type);
	ClassDB::bind_method(D_METHOD("set_input_type_string", "type"), &WebInput::set_input_type_string);
	ClassDB::bind_method(D_METHOD("get_input_type_string"), &WebInput::get_input_type_string);
	ClassDB::bind_method(D_METHOD("set_html_attribute", "name", "value"), &WebInput::set_html_attribute);
	ClassDB::bind_method(D_METHOD("get_html_attribute", "name"), &WebInput::get_html_attribute);
	ClassDB::bind_method(D_METHOD("get_test_metrics"), &WebInput::get_test_metrics);
	ClassDB::bind_method(D_METHOD("set_css", "property", "value"), &WebInput::set_css);
	ClassDB::bind_method(D_METHOD("set_css_text", "css"), &WebInput::set_css_text);
	ClassDB::bind_method(D_METHOD("clear_css"), &WebInput::clear_css);
	ClassDB::bind_method(D_METHOD("check_validity"), &WebInput::check_validity);
	ClassDB::bind_method(D_METHOD("send_date_key", "keycode"), &WebInput::send_date_key);
	ClassDB::bind_method(D_METHOD("show_picker"), &WebInput::show_picker);
	ClassDB::bind_method(D_METHOD("set_date_locale", "locale"), &WebInput::set_date_locale);
	ClassDB::bind_method(D_METHOD("get_date_locale"), &WebInput::get_date_locale);
	ClassDB::bind_method(D_METHOD("simulate_type", "text"), &WebInput::simulate_type);
	ClassDB::bind_static_method("WebInput", D_METHOD("filter_number_text", "text"), &WebInput::filter_number_text);
	ClassDB::bind_method(D_METHOD("reset_form"), &WebInput::reset_form);
	ClassDB::bind_method(D_METHOD("submit_form"), &WebInput::submit_form);
	ClassDB::bind_method(D_METHOD("set_accept", "accept"), &WebInput::set_accept);
	ClassDB::bind_method(D_METHOD("get_accept"), &WebInput::get_accept);
	ClassDB::bind_method(D_METHOD("set_selected_files", "files"), &WebInput::set_selected_files);
	ClassDB::bind_method(D_METHOD("get_selected_files"), &WebInput::get_selected_files);

	ClassDB::bind_method(D_METHOD("set_value", "value"), &WebInput::set_value);
	ClassDB::bind_method(D_METHOD("get_value"), &WebInput::get_value);
	ClassDB::bind_method(D_METHOD("set_placeholder", "placeholder"), &WebInput::set_placeholder);
	ClassDB::bind_method(D_METHOD("get_placeholder"), &WebInput::get_placeholder);
	ClassDB::bind_method(D_METHOD("set_input_name", "name"), &WebInput::set_input_name);
	ClassDB::bind_method(D_METHOD("get_input_name"), &WebInput::get_input_name);
	ClassDB::bind_method(D_METHOD("set_draw_box", "draw"), &WebInput::set_draw_box);
	ClassDB::bind_method(D_METHOD("is_drawing_box"), &WebInput::is_drawing_box);
	ClassDB::bind_method(D_METHOD("set_disabled", "disabled"), &WebInput::set_disabled);
	ClassDB::bind_method(D_METHOD("is_disabled"), &WebInput::is_disabled);
	ClassDB::bind_method(D_METHOD("set_readonly", "readonly"), &WebInput::set_readonly);
	ClassDB::bind_method(D_METHOD("is_readonly"), &WebInput::is_readonly);
	ClassDB::bind_method(D_METHOD("set_required", "required"), &WebInput::set_required);
	ClassDB::bind_method(D_METHOD("is_required"), &WebInput::is_required);
	ClassDB::bind_method(D_METHOD("set_checked", "checked"), &WebInput::set_checked);
	ClassDB::bind_method(D_METHOD("is_checked"), &WebInput::is_checked);
	ClassDB::bind_method(D_METHOD("set_size_chars", "size"), &WebInput::set_size_chars);
	ClassDB::bind_method(D_METHOD("get_size_chars"), &WebInput::get_size_chars);
	ClassDB::bind_method(D_METHOD("set_input_width", "width"), &WebInput::set_input_width);
	ClassDB::bind_method(D_METHOD("get_input_width"), &WebInput::get_input_width);
	ClassDB::bind_method(D_METHOD("set_input_height", "height"), &WebInput::set_input_height);
	ClassDB::bind_method(D_METHOD("get_input_height"), &WebInput::get_input_height);
	ClassDB::bind_method(D_METHOD("set_maxlength", "maxlength"), &WebInput::set_maxlength);
	ClassDB::bind_method(D_METHOD("get_maxlength"), &WebInput::get_maxlength);
	ClassDB::bind_method(D_METHOD("set_min_value", "min"), &WebInput::set_min_value);
	ClassDB::bind_method(D_METHOD("get_min_value"), &WebInput::get_min_value);
	ClassDB::bind_method(D_METHOD("set_max_value", "max"), &WebInput::set_max_value);
	ClassDB::bind_method(D_METHOD("get_max_value"), &WebInput::get_max_value);
	ClassDB::bind_method(D_METHOD("set_step_value", "step"), &WebInput::set_step_value);
	ClassDB::bind_method(D_METHOD("get_step_value"), &WebInput::get_step_value);
	ClassDB::bind_method(D_METHOD("set_pattern", "pattern"), &WebInput::set_pattern);
	ClassDB::bind_method(D_METHOD("get_pattern"), &WebInput::get_pattern);
	ClassDB::bind_method(D_METHOD("set_multiple", "multiple"), &WebInput::set_multiple);
	ClassDB::bind_method(D_METHOD("is_multiple"), &WebInput::is_multiple);
	ClassDB::bind_method(D_METHOD("set_src", "src"), &WebInput::set_src);
	ClassDB::bind_method(D_METHOD("get_src"), &WebInput::get_src);
	ClassDB::bind_method(D_METHOD("set_alt", "alt"), &WebInput::set_alt);
	ClassDB::bind_method(D_METHOD("get_alt"), &WebInput::get_alt);
	ClassDB::bind_method(D_METHOD("set_label", "label"), &WebInput::set_label);
	ClassDB::bind_method(D_METHOD("get_label"), &WebInput::get_label);
	ClassDB::bind_method(D_METHOD("get_label_text_rect"), &WebInput::get_label_text_rect);
	ClassDB::bind_method(D_METHOD("get_label_column_at_pos", "position"), &WebInput::get_label_column_at_pos);
	ClassDB::bind_method(D_METHOD("get_label_caret_position", "column"), &WebInput::get_label_caret_position);
	ClassDB::bind_method(D_METHOD("set_label_selection", "from", "to"), &WebInput::set_label_selection);
	ClassDB::bind_method(D_METHOD("get_selected_label_text"), &WebInput::get_selected_label_text);
	ClassDB::bind_method(D_METHOD("activate_label"), &WebInput::activate_label);
	ClassDB::bind_method(D_METHOD("set_label_cursor_shape", "shape"), &WebInput::set_label_cursor_shape);
	ClassDB::bind_method(D_METHOD("get_label_cursor_shape"), &WebInput::get_label_cursor_shape);
	ClassDB::bind_method(D_METHOD("set_label_position", "position"), &WebInput::set_label_position);
	ClassDB::bind_method(D_METHOD("get_label_position"), &WebInput::get_label_position);
	ClassDB::bind_method(D_METHOD("set_label_wrap_width", "width"), &WebInput::set_label_wrap_width);
	ClassDB::bind_method(D_METHOD("get_label_wrap_width"), &WebInput::get_label_wrap_width);
	ClassDB::bind_method(D_METHOD("set_label_max_width", "width"), &WebInput::set_label_max_width);
	ClassDB::bind_method(D_METHOD("get_label_max_width"), &WebInput::get_label_max_width);
	ClassDB::bind_method(D_METHOD("set_label_letter_spacing", "spacing"), &WebInput::set_label_letter_spacing);
	ClassDB::bind_method(D_METHOD("get_label_letter_spacing"), &WebInput::get_label_letter_spacing);
	ClassDB::bind_method(D_METHOD("set_label_decoration_lines", "lines"), &WebInput::set_label_decoration_lines);
	ClassDB::bind_method(D_METHOD("get_label_decoration_lines"), &WebInput::get_label_decoration_lines);
	ClassDB::bind_method(D_METHOD("set_label_decoration_color", "color"), &WebInput::set_label_decoration_color);
	ClassDB::bind_method(D_METHOD("get_label_decoration_color"), &WebInput::get_label_decoration_color);
	ClassDB::bind_method(D_METHOD("set_label_decoration_style", "style"), &WebInput::set_label_decoration_style);
	ClassDB::bind_method(D_METHOD("get_label_decoration_style"), &WebInput::get_label_decoration_style);
	ClassDB::bind_method(D_METHOD("set_label_decoration_thickness", "thickness"), &WebInput::set_label_decoration_thickness);
	ClassDB::bind_method(D_METHOD("get_label_decoration_thickness"), &WebInput::get_label_decoration_thickness);
	ClassDB::bind_method(D_METHOD("set_label_alignment", "alignment"), &WebInput::set_label_alignment);
	ClassDB::bind_method(D_METHOD("get_label_alignment"), &WebInput::get_label_alignment);
	ClassDB::bind_method(D_METHOD("set_image_width", "width"), &WebInput::set_image_width);
	ClassDB::bind_method(D_METHOD("get_image_width"), &WebInput::get_image_width);
	ClassDB::bind_method(D_METHOD("set_image_height", "height"), &WebInput::set_image_height);
	ClassDB::bind_method(D_METHOD("get_image_height"), &WebInput::get_image_height);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "input_type", PROPERTY_HINT_ENUM,
						"Text,Password,Email,URL,Tel,Search,Number,Button,Submit,Reset,Image,Checkbox,Radio,Range,Color,File,Hidden,Date,Time,DatetimeLocal,Month,Week"),
			"set_input_type", "get_input_type");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "value"), "set_value", "get_value");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "placeholder"), "set_placeholder", "get_placeholder");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "input_name"), "set_input_name", "get_input_name");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "disabled"), "set_disabled", "is_disabled");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "draw_box"), "set_draw_box", "is_drawing_box");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "readonly"), "set_readonly", "is_readonly");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "required"), "set_required", "is_required");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "checked"), "set_checked", "is_checked");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "size_chars", PROPERTY_HINT_RANGE, "1,1000,1"), "set_size_chars", "get_size_chars");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "input_width", PROPERTY_HINT_RANGE, "-1,10000,1"), "set_input_width", "get_input_width");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "input_height", PROPERTY_HINT_RANGE, "-1,10000,1"), "set_input_height", "get_input_height");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "maxlength", PROPERTY_HINT_RANGE, "-1,100000,1"), "set_maxlength", "get_maxlength");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "min_value"), "set_min_value", "get_min_value");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_value"), "set_max_value", "get_max_value");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "step_value"), "set_step_value", "get_step_value");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "pattern"), "set_pattern", "get_pattern");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "multiple"), "set_multiple", "is_multiple");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "src"), "set_src", "get_src");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "alt"), "set_alt", "get_alt");
	ADD_GROUP("Label", "label_");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "label_text"), "set_label", "get_label");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "label_position", PROPERTY_HINT_ENUM, "Left,Top,Right,Bottom"),
			"set_label_position", "get_label_position");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "label_wrap_width", PROPERTY_HINT_NONE, "suffix:px"),
			"set_label_wrap_width", "get_label_wrap_width");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "label_max_width", PROPERTY_HINT_NONE, "suffix:px"),
			"set_label_max_width", "get_label_max_width");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "label_letter_spacing", PROPERTY_HINT_NONE, "suffix:px"),
			"set_label_letter_spacing", "get_label_letter_spacing");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "label_decoration_lines", PROPERTY_HINT_FLAGS, "Underline,Overline,Line Through"),
			"set_label_decoration_lines", "get_label_decoration_lines");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "label_decoration_color"), "set_label_decoration_color", "get_label_decoration_color");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "label_decoration_style", PROPERTY_HINT_ENUM, "Solid,Double,Dotted,Dashed,Wavy"),
			"set_label_decoration_style", "get_label_decoration_style");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "label_decoration_thickness", PROPERTY_HINT_NONE, "suffix:px"),
			"set_label_decoration_thickness", "get_label_decoration_thickness");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "label_alignment", PROPERTY_HINT_RANGE, "-1,3"),
			"set_label_alignment", "get_label_alignment");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "image_width", PROPERTY_HINT_RANGE, "-1,10000,1"), "set_image_width", "get_image_width");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "image_height", PROPERTY_HINT_RANGE, "-1,10000,1"), "set_image_height", "get_image_height");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "accept"), "set_accept", "get_accept");

	ClassDB::bind_method(D_METHOD("set_external_color_picker_enabled", "enabled"), &WebInput::set_external_color_picker_enabled);
	ClassDB::bind_method(D_METHOD("is_external_color_picker_enabled"), &WebInput::is_external_color_picker_enabled);
	ClassDB::bind_method(D_METHOD("apply_external_color", "color"), &WebInput::apply_external_color);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "external_color_picker_enabled"), "set_external_color_picker_enabled", "is_external_color_picker_enabled");
	ADD_SIGNAL(MethodInfo("color_picker_requested", PropertyInfo(Variant::COLOR, "color")));

	ADD_SIGNAL(MethodInfo("value_changed", PropertyInfo(Variant::STRING, "value")));
	ADD_SIGNAL(MethodInfo("submitted", PropertyInfo(Variant::STRING, "value")));
	ADD_SIGNAL(MethodInfo("pressed"));
	ADD_SIGNAL(MethodInfo("toggled", PropertyInfo(Variant::BOOL, "checked")));
	ADD_SIGNAL(MethodInfo("radio_navigation_requested", PropertyInfo(Variant::INT, "direction")));
	ADD_SIGNAL(MethodInfo("form_submitted", PropertyInfo(Variant::DICTIONARY, "data")));

	BIND_ENUM_CONSTANT(TYPE_TEXT);
	BIND_ENUM_CONSTANT(TYPE_PASSWORD);
	BIND_ENUM_CONSTANT(TYPE_EMAIL);
	BIND_ENUM_CONSTANT(TYPE_URL);
	BIND_ENUM_CONSTANT(TYPE_TEL);
	BIND_ENUM_CONSTANT(TYPE_SEARCH);
	BIND_ENUM_CONSTANT(TYPE_NUMBER);
	BIND_ENUM_CONSTANT(TYPE_BUTTON);
	BIND_ENUM_CONSTANT(TYPE_SUBMIT);
	BIND_ENUM_CONSTANT(TYPE_RESET);
	BIND_ENUM_CONSTANT(TYPE_IMAGE);
	BIND_ENUM_CONSTANT(TYPE_CHECKBOX);
	BIND_ENUM_CONSTANT(TYPE_RADIO);
	BIND_ENUM_CONSTANT(TYPE_RANGE);
	BIND_ENUM_CONSTANT(TYPE_COLOR);
	BIND_ENUM_CONSTANT(TYPE_FILE);
	BIND_ENUM_CONSTANT(TYPE_HIDDEN);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "date_locale"), "set_date_locale", "get_date_locale");

	BIND_ENUM_CONSTANT(TYPE_DATE);
	BIND_ENUM_CONSTANT(TYPE_TIME);
	BIND_ENUM_CONSTANT(TYPE_DATETIME_LOCAL);
	BIND_ENUM_CONSTANT(TYPE_MONTH);
	BIND_ENUM_CONSTANT(TYPE_WEEK);

	BIND_ENUM_CONSTANT(FIELD_TEXT);
	BIND_ENUM_CONSTANT(FIELD_YEAR);
	BIND_ENUM_CONSTANT(FIELD_MONTH);
	BIND_ENUM_CONSTANT(FIELD_MONTH_NAME);
	BIND_ENUM_CONSTANT(FIELD_DAY);
	BIND_ENUM_CONSTANT(FIELD_WEEK);
	BIND_ENUM_CONSTANT(FIELD_HOUR12);
	BIND_ENUM_CONSTANT(FIELD_HOUR24);
	BIND_ENUM_CONSTANT(FIELD_MINUTE);
	BIND_ENUM_CONSTANT(FIELD_SECOND);
	BIND_ENUM_CONSTANT(FIELD_AMPM);
	BIND_ENUM_CONSTANT(TYPE_MAX);

	BIND_ENUM_CONSTANT(LABEL_LEFT);
	BIND_ENUM_CONSTANT(LABEL_TOP);
	BIND_ENUM_CONSTANT(LABEL_RIGHT);
	BIND_ENUM_CONSTANT(LABEL_BOTTOM);

	// Theme items (the user-agent base; appear in the Theme editor).
	BIND_THEME_ITEM(Theme::DATA_TYPE_FONT, WebInput, font);
	BIND_THEME_ITEM(Theme::DATA_TYPE_FONT_SIZE, WebInput, font_size);
	BIND_THEME_ITEM(Theme::DATA_TYPE_COLOR, WebInput, font_color);
	BIND_THEME_ITEM(Theme::DATA_TYPE_COLOR, WebInput, font_placeholder_color);
	BIND_THEME_ITEM(Theme::DATA_TYPE_COLOR, WebInput, selection_color);
	BIND_THEME_ITEM(Theme::DATA_TYPE_COLOR, WebInput, caret_color);
	BIND_THEME_ITEM(Theme::DATA_TYPE_COLOR, WebInput, accent_color);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebInput, font_weight);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebInput, letter_spacing);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebInput, line_height);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebInput, text_align);
	BIND_THEME_ITEM(Theme::DATA_TYPE_CONSTANT, WebInput, box_sizing);
	BIND_THEME_ITEM(Theme::DATA_TYPE_FONT, WebInput, label_font);
	BIND_THEME_ITEM(Theme::DATA_TYPE_FONT_SIZE, WebInput, label_font_size);
	BIND_THEME_ITEM(Theme::DATA_TYPE_COLOR, WebInput, label_font_color);
	BIND_THEME_ITEM(Theme::DATA_TYPE_STYLEBOX, WebInput, field);
	BIND_THEME_ITEM(Theme::DATA_TYPE_STYLEBOX, WebInput, button);
	BIND_THEME_ITEM(Theme::DATA_TYPE_STYLEBOX, WebInput, focus);
	BIND_THEME_ITEM(Theme::DATA_TYPE_STYLEBOX, WebInput, hover);
	BIND_THEME_ITEM(Theme::DATA_TYPE_STYLEBOX, WebInput, pressed);
	BIND_THEME_ITEM(Theme::DATA_TYPE_ICON, WebInput, broken_image);
}

WebInput::WebInput() {
	set_focus_mode(FOCUS_ALL);
	set_default_cursor_shape(CURSOR_IBEAM);

	// Chromium uses Arial for default form controls on Windows. Load the real
	// font file so the glyph shapes AND vertical metrics match the browser
	// (SystemFont can report a taller ascent, which mis-centers the text); fall
	// back to a SystemFont if the file is unavailable.
	ua_font = _make_ua_font("C:/Windows/Fonts/arial.ttf", "Arial");
	ua_mono_font = _make_ua_font("C:/Windows/Fonts/consola.ttf", "Consolas");

	// Embedded editor for text-family types (created once; visibility toggled
	// by type). Its own chrome is stripped so WebInput draws the Chromium box.
	line_edit = memnew(LineEdit);
	line_edit->set_mouse_filter(MOUSE_FILTER_PASS);
	Ref<StyleBoxEmpty> empty = memnew(StyleBoxEmpty);
	le_box = empty;
	line_edit->add_theme_style_override("normal", empty);
	line_edit->add_theme_style_override("focus", empty);
	line_edit->add_theme_style_override("read_only", empty);
	line_edit->add_theme_color_override("font_color", CHROME_TEXT_FG);
	line_edit->add_theme_color_override("font_placeholder_color", CHROME_PLACEHOLDER);
	line_edit->add_theme_font_override("font", ua_font);
	line_edit->add_theme_font_size_override("font_size", (int)Math::round(CHROME_FONT_SIZE));
	line_edit->connect("text_changed", callable_mp(this, &WebInput::_on_line_edit_changed));
	line_edit->connect("text_submitted", callable_mp(this, &WebInput::_on_line_edit_submitted));
	line_edit->connect(SceneStringName(gui_input), callable_mp(this, &WebInput::_on_line_edit_gui_input));
	// Redraw the focus ring when the editor gains/loses focus (e.g. on click).
	line_edit->connect("focus_entered", callable_mp((CanvasItem *)this, &CanvasItem::queue_redraw));
	line_edit->connect("focus_exited", callable_mp((CanvasItem *)this, &CanvasItem::queue_redraw));
	editor_clip = memnew(Control);
	editor_clip->set_clip_contents(true);
	editor_clip->set_mouse_filter(MOUSE_FILTER_IGNORE);
	add_child(editor_clip, false, INTERNAL_MODE_FRONT);
	editor_clip->add_child(line_edit);
	line_edit->set_visible(_is_text_family());
}
