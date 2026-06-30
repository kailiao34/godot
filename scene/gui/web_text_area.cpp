/**************************************************************************/
/*  web_text_area.cpp                                                     */
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

#include "web_text_area.h"

#include "core/input/input_event.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "scene/gui/scroll_bar.h"
#include "scene/gui/web_input.h"
#include "scene/resources/font.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/style_box.h"
#include "scene/resources/style_box_flat.h"
#include "scene/theme/theme_db.h"
#include "servers/display/display_server.h"

// ---------------------------------------------------------------------------
// Chromium user-agent textarea, sampled from Chrome 148 on Windows. Sizes are
// CSS pixels; the box is content-box with a 1px border and 2px padding.
// ---------------------------------------------------------------------------
static const real_t TA_UA_FONT_SIZE = 13.3333; // -webkit-small-control.
static const real_t TA_SCROLLBAR = 15; // classic scrollbar thickness.
static const real_t TA_SCROLL_BUTTON = 17; // arrow button length.
static const real_t TA_THUMB_INSET = 3; // thumb is 9px wide in the 15px bar.
static const real_t TA_THUMB_MIN = 17;
static const Color TA_BG = Color(1, 1, 1, 1);
static const Color TA_BORDER = Color(118.0 / 255, 118.0 / 255, 118.0 / 255, 1); // #767676
static const Color TA_BORDER_HOVER = Color(79.0 / 255, 79.0 / 255, 79.0 / 255, 1); // #4f4f4f
static const Color TA_DISABLED_BG = Color(248.0 / 255, 248.0 / 255, 248.0 / 255, 1); // #f8f8f8
static const Color TA_DISABLED_BORDER = Color(208.0 / 255, 208.0 / 255, 208.0 / 255, 1); // #d0d0d0
static const Color TA_DISABLED_TEXT = Color(84.0 / 255, 84.0 / 255, 84.0 / 255, 1); // #545454
static const Color TA_TRACK = Color(252.0 / 255, 252.0 / 255, 252.0 / 255, 1); // #fcfcfc
static const Color TA_THUMB = Color(139.0 / 255, 139.0 / 255, 139.0 / 255, 1); // #8b8b8b
static const Color TA_THUMB_ACTIVE = Color(106.0 / 255, 106.0 / 255, 106.0 / 255, 1); // pressed thumb
static const Color TA_ARROW = Color(139.0 / 255, 139.0 / 255, 139.0 / 255, 1); // #8b8b8b
static const Color TA_CORNER_LINE = Color(217.0 / 255, 217.0 / 255, 217.0 / 255, 1); // #d9d9d9
static const Color TA_GRIP = Color(101.0 / 255, 101.0 / 255, 101.0 / 255, 1); // #656565 (in the scroll corner)
static const Color TA_GRIP_BARE = Color(102.0 / 255, 102.0 / 255, 102.0 / 255, 1); // #666666 (no scrollbars)
static const Color TA_GRIP_LIGHT = Color(254.0 / 255, 254.0 / 255, 254.0 / 255, 1); // #fefefe
static const Color TA_TEXT = Color(0, 0, 0, 1);
static const Color TA_PLACEHOLDER = Color(117.0 / 255, 117.0 / 255, 117.0 / 255, 1); // #757575
static const Color TA_SELECTION = Color(0.6, 0.8, 1.0, 0.4);
static const Color TA_FOCUS_RING = Color(16.0 / 255, 16.0 / 255, 16.0 / 255, 1); // #101010

// ---------------------------------------------------------------------------
// Font metrics. Blink on Windows builds the `normal` line box from the
// DirectWrite metrics: rounded usWin ascent and descent plus the rounded part
// of the hhea line that the Win metrics do not already cover (USE_TYPO_METRICS
// fonts use the typo set instead). These are read straight from the sfnt.
// ---------------------------------------------------------------------------
static uint16_t _ta_be16(const uint8_t *p) {
	return (uint16_t(p[0]) << 8) | uint16_t(p[1]);
}

static int16_t _ta_be16s(const uint8_t *p) {
	return (int16_t)_ta_be16(p);
}

static uint32_t _ta_be32(const uint8_t *p) {
	return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

struct TaSfntMetrics {
	int upm = 0;
	int hhea_ascent = 0;
	int hhea_descent = 0; // positive
	int hhea_gap = 0;
	int win_ascent = 0;
	int win_descent = 0;
	int typo_ascent = 0;
	int typo_descent = 0; // positive
	int typo_gap = 0;
	bool use_typo = false;
	bool has_os2 = false;
};

static bool _ta_read_sfnt(const PackedByteArray &p_data, int p_face, TaSfntMetrics &r) {
	const int64_t n = p_data.size();
	if (n < 12) {
		return false;
	}
	const uint8_t *d = p_data.ptr();
	uint32_t base = 0;
	if (d[0] == 't' && d[1] == 't' && d[2] == 'c' && d[3] == 'f') {
		uint32_t count = _ta_be32(d + 8);
		if (p_face < 0 || (uint32_t)p_face >= count || 12 + 4 * (int64_t)(p_face + 1) > n) {
			return false;
		}
		base = _ta_be32(d + 12 + 4 * p_face);
	}
	if ((int64_t)base + 12 > n) {
		return false;
	}
	uint32_t version = _ta_be32(d + base);
	if (version != 0x00010000 && version != 0x4F54544F /* OTTO */ && version != 0x74727565 /* true */) {
		return false; // WOFF/WOFF2 or unknown: tables are not directly readable.
	}
	uint16_t num_tables = _ta_be16(d + base + 4);
	bool has_head = false;
	bool has_hhea = false;
	for (uint16_t i = 0; i < num_tables; i++) {
		int64_t rec = base + 12 + 16 * (int64_t)i;
		if (rec + 16 > n) {
			return false;
		}
		const uint8_t *t = d + rec;
		uint32_t off = _ta_be32(t + 8);
		uint32_t len = _ta_be32(t + 12);
		if ((int64_t)off + len > n) {
			continue;
		}
		const uint8_t *tab = d + off;
		if (t[0] == 'h' && t[1] == 'e' && t[2] == 'a' && t[3] == 'd' && len >= 20) {
			r.upm = _ta_be16(tab + 18);
			has_head = true;
		} else if (t[0] == 'h' && t[1] == 'h' && t[2] == 'e' && t[3] == 'a' && len >= 10) {
			r.hhea_ascent = _ta_be16s(tab + 4);
			r.hhea_descent = -_ta_be16s(tab + 6);
			r.hhea_gap = _ta_be16s(tab + 8);
			has_hhea = true;
		} else if (t[0] == 'O' && t[1] == 'S' && t[2] == '/' && t[3] == '2' && len >= 78) {
			uint16_t fs_selection = _ta_be16(tab + 62);
			r.typo_ascent = _ta_be16s(tab + 68);
			r.typo_descent = -_ta_be16s(tab + 70);
			r.typo_gap = _ta_be16s(tab + 72);
			r.win_ascent = _ta_be16(tab + 74);
			r.win_descent = _ta_be16(tab + 76);
			r.use_typo = (fs_selection & (1 << 7)) != 0;
			r.has_os2 = true;
		}
	}
	return has_head && has_hhea && r.upm > 0;
}

// ---------------------------------------------------------------------------
// WebTextAreaEditor
// ---------------------------------------------------------------------------
int WebTextAreaEditor::_room_for_input(int p_caret) {
	if (!area || area->maxlength < 0) {
		return INT_MAX;
	}
	int length = area->_utf16_length(get_text());
	int selected = 0;
	for (int i = 0; i < get_caret_count(); i++) {
		if (p_caret != -1 && p_caret != i) {
			continue;
		}
		if (has_selection(i)) {
			selected += area->_utf16_length(get_selected_text(i));
		}
	}
	return MAX(0, area->maxlength - (length - selected));
}

String WebTextAreaEditor::_clip_to_room(const String &p_text, int p_caret) {
	int room = _room_for_input(p_caret);
	if (room == INT_MAX) {
		return p_text;
	}
	String out;
	int used = 0;
	for (int i = 0; i < p_text.length(); i++) {
		char32_t c = p_text[i];
		if (c == '\r') {
			continue; // the API value normalizes line breaks to LF.
		}
		int units = c >= 0x10000 ? 2 : 1;
		if (used + units > room) {
			break;
		}
		used += units;
		out += String::chr(c);
	}
	return out;
}

void WebTextAreaEditor::_handle_unicode_input_internal(const uint32_t p_unicode, int p_caret) {
	if (area && area->maxlength >= 0) {
		int units = p_unicode >= 0x10000 ? 2 : 1;
		if (_room_for_input(p_caret) < units) {
			return;
		}
	}
	TextEdit::_handle_unicode_input_internal(p_unicode, p_caret);
}

void WebTextAreaEditor::_paste_internal(int p_caret) {
	if (!area || area->maxlength < 0) {
		TextEdit::_paste_internal(p_caret);
		return;
	}
	if (!is_editable()) {
		return;
	}
	String clipped = _clip_to_room(DisplayServer::get_singleton()->clipboard_get(), p_caret);
	if (clipped.is_empty()) {
		return;
	}
	insert_text_at_caret(clipped, p_caret);
}

void WebTextAreaEditor::gui_input(const Ref<InputEvent> &p_event) {
	Ref<InputEventKey> k = p_event;
	if (k.is_valid() && k->is_pressed() && area && k->is_action("ui_focus_prev", true)) {
		// Shift+Tab: Godot's backward traversal stops on the field (this
		// editor's parent), whose FOCUS_ENTER hands focus straight back here;
		// step over the whole field instead, as the browser does.
		accept_event();
		Control *prev = area->find_prev_valid_focus();
		if (prev && prev != area && !area->is_ancestor_of(prev)) {
			prev->grab_focus();
		}
		return;
	}
	if (k.is_valid() && k->is_pressed()) {
		// A browser textarea moves focus with Tab rather than inserting a tab,
		// and has no "new line above/below" commands; leaving the event
		// unhandled lets the viewport's focus navigation take it.
		if (k->is_action("ui_text_indent", true) || k->is_action("ui_text_dedent", true) ||
				k->is_action("ui_text_newline_blank", true) || k->is_action("ui_text_newline_above", true)) {
			return;
		}
		if (area && area->maxlength >= 0 && k->is_action("ui_text_newline", true) && _room_for_input(-1) < 1) {
			accept_event();
			return;
		}
	}
	TextEdit::gui_input(p_event);
}

// ---------------------------------------------------------------------------
// WebTextAreaChrome
// ---------------------------------------------------------------------------
void WebTextAreaChrome::_notification(int p_what) {
	if (p_what == NOTIFICATION_DRAW && area) {
		area->_draw_chrome(this);
	}
}

bool WebTextAreaChrome::has_point(const Point2 &p_point) const {
	return area && area->_part_at(p_point) != WebTextArea::PART_NONE;
}

void WebTextAreaChrome::gui_input(const Ref<InputEvent> &p_event) {
	if (area) {
		area->_chrome_input(p_event);
		accept_event();
	}
}

Control::CursorShape WebTextAreaChrome::get_cursor_shape(const Point2 &p_pos) const {
	if (area && area->_part_at(p_pos) == WebTextArea::PART_RESIZER) {
		switch (area->resize_mode) {
			case WebTextArea::RESIZE_HORIZONTAL:
				return CURSOR_HSIZE;
			case WebTextArea::RESIZE_VERTICAL:
				return CURSOR_VSIZE;
			default:
				return CURSOR_FDIAGSIZE;
		}
	}
	return CURSOR_ARROW;
}

// ---------------------------------------------------------------------------
// Fonts
// ---------------------------------------------------------------------------
String WebTextArea::_effective_font_locale() const {
	String locale = font_locale.is_empty() ? OS::get_singleton()->get_locale() : font_locale;
	return locale.replace("-", "_");
}

struct TaMonoFace {
	const char *locale_prefix;
	const char *file;
	int face;
	const char *family;
};

// Chromium's default fixed-pitch family per UI language on Windows
// (IDS_FIXED_FONT_FAMILY); `monospace` resolves to it.
static const TaMonoFace TA_MONO_FACES[] = {
	{ "zh_TW", "C:/Windows/Fonts/mingliu.ttc", 0, "MingLiU" },
	{ "zh_HK", "C:/Windows/Fonts/mingliu.ttc", 0, "MingLiU" },
	{ "zh_MO", "C:/Windows/Fonts/mingliu.ttc", 0, "MingLiU" },
	{ "zh_Hant", "C:/Windows/Fonts/mingliu.ttc", 0, "MingLiU" },
	{ "zh", "C:/Windows/Fonts/simsun.ttc", 1, "NSimSun" },
	{ "ja", "C:/Windows/Fonts/msgothic.ttc", 0, "MS Gothic" },
	{ "ko", "C:/Windows/Fonts/gulim.ttc", 1, "GulimChe" },
};

Ref<Font> WebTextArea::_ua_monospace_font() {
	String locale = _effective_font_locale();
	if (ua_font.is_valid() && ua_font_locale == locale) {
		return ua_font;
	}
	String file = "C:/Windows/Fonts/consola.ttf";
	int face = 0;
	String family = "Consolas";
	for (const TaMonoFace &entry : TA_MONO_FACES) {
		if (locale.begins_with(entry.locale_prefix)) {
			file = entry.file;
			face = entry.face;
			family = entry.family;
			break;
		}
	}
	Ref<FontFile> ff;
	ff.instantiate();
	if (ff->load_dynamic_font(file) == OK) {
		if (face > 0) {
			ff->set_face_index(0, face);
		}
		// Unhinted, sub-pixel positioned advances, like the browser.
		ff->set_hinting(TextServer::HINTING_NONE);
		ff->set_subpixel_positioning(TextServer::SUBPIXEL_POSITIONING_ONE_QUARTER);
		ua_font = ff;
	} else {
		Ref<SystemFont> sf;
		sf.instantiate();
		PackedStringArray names;
		names.push_back(family);
		names.push_back("monospace");
		sf->set_font_names(names);
		ua_font = sf;
	}
	ua_font_locale = locale;
	metrics_cache.font_id = ObjectID();
	return ua_font;
}

// A browser textarea does not take the surrounding UI's theme: only values set
// on this control itself (theme overrides) replace the user-agent defaults.
Ref<Font> WebTextArea::_theme_font() const {
	return has_theme_font_override(SceneStringName(font)) ? theme_cache.font : Ref<Font>();
}

Color WebTextArea::_theme_color(const StringName &p_name, const Color &p_theme, const Color &p_ua) const {
	return has_theme_color_override(p_name) ? p_theme : p_ua;
}

Ref<Font> WebTextArea::_resolved_font() const {
	if (css_font.is_valid()) {
		return css_font;
	}
	if (_theme_font().is_valid()) {
		return _theme_font();
	}
	if (ua_font.is_valid()) {
		return ua_font;
	}
	return get_theme_default_font();
}

// An installed family as an unhinted FontFile, so its metrics come from the
// font's own tables like the browser's; null when the OS has no such family.
static Ref<FontFile> _ta_system_font_file(const String &p_family, int p_weight) {
	String path = OS::get_singleton()->get_system_font_path(p_family, p_weight);
	if (path.is_empty()) {
		return Ref<FontFile>();
	}
	Ref<FontFile> ff;
	ff.instantiate();
	if (ff->load_dynamic_font(path) != OK) {
		return Ref<FontFile>();
	}
	ff->set_hinting(TextServer::HINTING_NONE);
	ff->set_subpixel_positioning(TextServer::SUBPIXEL_POSITIONING_ONE_QUARTER);
	return ff;
}

void WebTextArea::_rebuild_css_font() {
	int weight = css.has_font_weight ? css.font_weight : 400;
	real_t spacing = css.has_letter_spacing ? css.letter_spacing : 0.0;
	if (css.font_family.is_empty() && weight == 400 && spacing == 0.0) {
		css_font = Ref<Font>();
		return;
	}
	Ref<Font> base;
	if (!css.font_family.is_empty()) {
		// The first installed family of the list is the primary font, as in the
		// browser; `monospace` is the UA font.
		for (const String &raw : css.font_family.split(",")) {
			String name = raw.strip_edges().trim_prefix("\"").trim_suffix("\"").trim_prefix("'").trim_suffix("'");
			if (name.is_empty()) {
				continue;
			}
			if (name == "monospace") {
				base = ua_font;
				break;
			}
			Ref<FontFile> file = _ta_system_font_file(name, weight);
			if (file.is_valid()) {
				base = file;
				break;
			}
		}
	}
	if (base.is_null()) {
		base = _theme_font().is_valid() ? _theme_font() : ua_font;
	}
	if (weight == 400 && spacing == 0.0) {
		css_font = base;
		return;
	}
	Ref<FontVariation> fv;
	fv.instantiate();
	fv->set_base_font(base);
	if (weight != 400) {
		Dictionary coords;
		coords["wght"] = weight;
		fv->set_variation_opentype(coords);
		fv->set_variation_embolden(CLAMP((weight - 400) / 400.0, -0.5, 1.0));
	}
	if (spacing != 0.0) {
		fv->set_spacing(TextServer::SPACING_GLYPH, (int)Math::round(spacing));
	}
	css_font = fv;
}

void WebTextArea::_read_font_metrics(const Ref<Font> &p_font) const {
	if (p_font.is_null()) {
		metrics_cache = FontMetricsCache();
		return;
	}
	if (metrics_cache.font_id == p_font->get_instance_id()) {
		return;
	}
	FontMetricsCache m;
	m.font_id = p_font->get_instance_id();
	Ref<Font> source = p_font;
	for (int depth = 0; depth < 4; depth++) {
		Ref<FontVariation> variation = source;
		if (variation.is_null()) {
			break;
		}
		source = variation->get_base_font();
	}
	Ref<FontFile> file = source;
	TaSfntMetrics sfnt;
	if (file.is_valid() && _ta_read_sfnt(file->get_data(), (int)file->get_face_index(0), sfnt)) {
		m.sfnt = true;
		m.upm = sfnt.upm;
		if (sfnt.use_typo) {
			m.win_ascent = sfnt.typo_ascent;
			m.win_descent = sfnt.typo_descent;
			m.hhea_ascent = sfnt.typo_ascent;
			m.hhea_descent = sfnt.typo_descent;
			m.hhea_gap = sfnt.typo_gap;
		} else if (sfnt.has_os2 && (sfnt.win_ascent > 0 || sfnt.win_descent > 0)) {
			m.win_ascent = sfnt.win_ascent;
			m.win_descent = sfnt.win_descent;
			m.hhea_ascent = sfnt.hhea_ascent;
			m.hhea_descent = sfnt.hhea_descent;
			m.hhea_gap = sfnt.hhea_gap;
		} else {
			m.win_ascent = sfnt.hhea_ascent;
			m.win_descent = sfnt.hhea_descent;
			m.hhea_ascent = sfnt.hhea_ascent;
			m.hhea_descent = sfnt.hhea_descent;
			m.hhea_gap = sfnt.hhea_gap;
		}
	}
	const int ref = 256;
	m.ascent_em = p_font->get_ascent(ref) / ref;
	m.descent_em = p_font->get_descent(ref) / ref;
	m.x_advance_em = p_font->get_char_size('x', ref).x / ref;
	if (m.x_advance_em <= 0) {
		m.x_advance_em = p_font->get_char_size('0', ref).x / ref;
	}
	metrics_cache = m;
}

real_t WebTextArea::_normal_line_height(const Ref<Font> &p_font, real_t p_font_size) const {
	_read_font_metrics(p_font);
	const FontMetricsCache &m = metrics_cache;
	if (m.sfnt) {
		real_t s = p_font_size / (real_t)m.upm;
		real_t ascent = Math::round(m.win_ascent * s);
		real_t descent = Math::round(m.win_descent * s);
		int uncovered = MAX(0, (m.hhea_ascent + m.hhea_descent + m.hhea_gap) - (m.win_ascent + m.win_descent));
		return ascent + descent + Math::round(uncovered * s);
	}
	return Math::round(m.ascent_em * p_font_size) + Math::round(m.descent_em * p_font_size);
}

real_t WebTextArea::_column_width(const Ref<Font> &p_font, real_t p_font_size) const {
	// Chromium sizes a textarea from the advance of 'x' in 26.6 fixed point,
	// rounded up to a whole pixel when that rounding goes up.
	_read_font_metrics(p_font);
	real_t x = Math::round(metrics_cache.x_advance_em * p_font_size * 64.0) / 64.0;
	return MAX(x, (real_t)Math::round(x));
}

int WebTextArea::_utf16_length(const String &p_text) const {
	int units = 0;
	for (int i = 0; i < p_text.length(); i++) {
		char32_t c = p_text[i];
		if (c == '\r') {
			continue;
		}
		units += c >= 0x10000 ? 2 : 1;
	}
	return units;
}

// ---------------------------------------------------------------------------
// Box model
// ---------------------------------------------------------------------------
WebTextArea::BoxModel WebTextArea::_compute_box_model() const {
	BoxModel b;
	// A theme size of 13 is the UA sentinel for Chromium's 13.3333px.
	int override_size = has_theme_font_size_override(SceneStringName(font_size)) ? theme_cache.font_size : 0;
	real_t theme_size = override_size > 0 && override_size != 13 ? (real_t)override_size : TA_UA_FONT_SIZE;
	b.font_size = css.has_font_size && css.font_size > 0 ? css.font_size : theme_size;
	Ref<Font> font = _resolved_font();
	b.line_height = css.has_line_height && css.line_height > 0 ? css.line_height : _normal_line_height(font, b.font_size);
	b.char_width = _column_width(font, b.font_size);
	b.text_color = _theme_color(SceneStringName(font_color), theme_cache.font_color, TA_TEXT);
	b.font_weight = css.has_font_weight ? css.font_weight : 400;
	b.letter_spacing = css.has_letter_spacing ? css.letter_spacing : 0;
	b.text_align = css.has_text_align ? css.text_align : 0;
	if (disabled) {
		b.background = TA_DISABLED_BG;
		b.text_color = TA_DISABLED_TEXT;
		b.border_color = TA_DISABLED_BORDER;
	}

	real_t content_w = Math::ceil(b.char_width * MAX(1, cols) - 1e-4) + TA_SCROLLBAR;
	real_t content_h = b.line_height * MAX(1, rows);

	for (int i = 0; i < 4; i++) {
		if (css.has_bw[i]) {
			b.border[i] = MAX((real_t)0, css.bw[i]);
			b.author_border = true;
		}
		if (css.has_pad[i]) {
			b.padding[i] = MAX((real_t)0, css.pad[i]);
		}
		if (css.has_radius[i]) {
			b.radius[i] = MAX((real_t)0, css.radius[i]);
			b.author_border = b.author_border || b.radius[i] > 0;
		}
	}
	if (css.border_none) {
		for (int i = 0; i < 4; i++) {
			b.border[i] = 0;
		}
		b.author_border = true;
	}
	if (css.has_bg) {
		b.background = css.bg;
	}
	if (css.has_bcol) {
		b.border_color = css.bcol;
		b.author_border = true;
	}
	if (css.has_color) {
		b.text_color = css.color;
	}

	real_t extra_w = b.border[1] + b.border[3] + b.padding[1] + b.padding[3];
	real_t extra_h = b.border[0] + b.border[2] + b.padding[0] + b.padding[2];
	b.intrinsic_box = Size2(content_w + extra_w, content_h + extra_h);
	b.border_box = b.intrinsic_box;
	bool border_box_sizing = css.has_box_sizing && css.border_box;
	bool has_w = css.has_width || input_width >= 0;
	bool has_h = css.has_height || input_height >= 0;
	if (has_w) {
		real_t w = css.has_width ? css.width : (real_t)input_width;
		b.border_box.x = MAX(border_box_sizing ? w : w + extra_w, extra_w);
	}
	if (has_h) {
		real_t h = css.has_height ? css.height : (real_t)input_height;
		b.border_box.y = MAX(border_box_sizing ? h : h + extra_h, extra_h);
	}
	return b;
}

Size2 WebTextArea::get_minimum_size() const {
	return _compute_box_model().border_box;
}

Color WebTextArea::_border_color(const BoxModel &p_box) const {
	if (!disabled && !p_box.author_border && hovered) {
		return TA_BORDER_HOVER; // the native field edge darkens under the pointer.
	}
	return p_box.border_color;
}

// ---------------------------------------------------------------------------
// Editor sync
// ---------------------------------------------------------------------------
void WebTextArea::_sync_editor() {
	if (!editor) {
		return;
	}
	syncing = true;
	BoxModel b = _compute_box_model();
	Size2 size = get_size();
	Point2 inner_pos(b.border[3], b.border[0]);
	Size2 inner_size(MAX((real_t)0, size.x - b.border[1] - b.border[3]), MAX((real_t)0, size.y - b.border[0] - b.border[2]));
	editor->set_position(inner_pos);
	editor->set_size(inner_size);
	if (editor_box.is_valid()) {
		editor_box->set_content_margin(SIDE_TOP, b.padding[0]);
		editor_box->set_content_margin(SIDE_RIGHT, b.padding[1]);
		editor_box->set_content_margin(SIDE_BOTTOM, b.padding[2]);
		editor_box->set_content_margin(SIDE_LEFT, b.padding[3]);
	}
	if (editor->get_text() != value) {
		editor->set_text(value);
	}
	editor->set_placeholder(placeholder);
	editor->set_editable(!disabled && !readonly);
	bool interactive = !disabled && get_focus_mode() != FOCUS_NONE;
	editor->set_focus_mode(interactive ? FOCUS_ALL : FOCUS_NONE);
	editor->set_mouse_filter(disabled ? MOUSE_FILTER_IGNORE : MOUSE_FILTER_PASS);
	editor->set_line_wrapping_mode(wrap_mode == WRAP_OFF ? TextEdit::LINE_WRAPPING_NONE : TextEdit::LINE_WRAPPING_BOUNDARY);
	editor->set_default_cursor_shape(disabled ? CURSOR_ARROW : CURSOR_IBEAM);
	set_default_cursor_shape(disabled ? CURSOR_ARROW : CURSOR_IBEAM);
	Ref<Font> font = _resolved_font();
	if (font.is_valid()) {
		editor->add_theme_font_override(SceneStringName(font), font);
	}
	editor->add_theme_font_size_override(SceneStringName(font_size), MAX(1, (int)Math::round(b.font_size)));
	editor->add_theme_color_override(SceneStringName(font_color), b.text_color);
	editor->add_theme_color_override("font_readonly_color", b.text_color);
	editor->add_theme_color_override("font_placeholder_color", _theme_color("font_placeholder_color", theme_cache.font_placeholder_color, TA_PLACEHOLDER));
	editor->add_theme_color_override("selection_color", _theme_color("selection_color", theme_cache.selection_color, TA_SELECTION));
	editor->add_theme_color_override("caret_color", _theme_color("caret_color", theme_cache.caret_color, TA_TEXT));
	_apply_line_spacing(b);
	if (chrome) {
		chrome->set_position(Point2());
		chrome->set_size(size);
		chrome->queue_redraw();
	}
	syncing = false;
	queue_redraw();
}

void WebTextArea::_apply_line_spacing(const BoxModel &p_box) {
	// TextEdit rows are the font height (truncated to whole pixels) plus
	// `line_spacing`; choose the spacing that makes a row exactly the CSS line
	// height. Computed from the font, not read back from the editor, whose
	// cached line heights lag a font change.
	int target = MAX(1, (int)Math::round(p_box.line_height));
	Ref<Font> font = _resolved_font();
	int font_size = MAX(1, (int)Math::round(p_box.font_size));
	int natural = font.is_valid() ? (int)font->get_height(font_size) : target;
	int spacing = target - MAX(1, natural);
	if (!editor->has_theme_constant_override("line_spacing") || editor->get_theme_constant("line_spacing") != spacing) {
		editor->add_theme_constant_override("line_spacing", spacing);
	}
}

// ---------------------------------------------------------------------------
// Scrollbars and corner
// ---------------------------------------------------------------------------
WebTextArea::ScrollGeometry WebTextArea::_scroll_geometry(const BoxModel &p_box) const {
	ScrollGeometry g;
	if (!editor) {
		return g;
	}
	ScrollBar *v = editor->get_v_scroll_bar();
	ScrollBar *h = editor->get_h_scroll_bar();
	g.vertical = v && v->is_visible();
	g.horizontal = h && h->is_visible();
	Size2 size = get_size();
	real_t left = p_box.border[3];
	real_t top = p_box.border[0];
	real_t right = size.x - p_box.border[1];
	real_t bottom = size.y - p_box.border[2];
	// The resizer always owns the scroll corner, so a single scrollbar still
	// stops short of it; with no scrollbar the grip sits on the field itself.
	g.corner = (g.vertical || g.horizontal) && (resize_mode != RESIZE_NONE || (g.vertical && g.horizontal));
	real_t corner = g.corner ? TA_SCROLLBAR : 0;
	if (g.vertical) {
		g.v_bar = Rect2(right - TA_SCROLLBAR, top, TA_SCROLLBAR, MAX((real_t)0, bottom - top - corner));
	}
	if (g.horizontal) {
		g.h_bar = Rect2(left, bottom - TA_SCROLLBAR, MAX((real_t)0, right - left - corner), TA_SCROLLBAR);
	}
	g.corner_rect = Rect2(right - TA_SCROLLBAR, bottom - TA_SCROLLBAR, TA_SCROLLBAR, TA_SCROLLBAR);
	if (g.vertical) {
		real_t track_from = g.v_bar.position.y + TA_SCROLL_BUTTON + 1;
		real_t track_len = MAX((real_t)0, g.v_bar.size.y - 2 * TA_SCROLL_BUTTON - 2);
		real_t range = MAX(1.0, v->get_max() - v->get_min());
		real_t page = CLAMP(v->get_page(), 0.0, range);
		real_t thumb = CLAMP(Math::round(track_len * page / range), MIN(TA_THUMB_MIN, track_len), track_len);
		real_t travel = range - page;
		real_t ratio = travel > 0 ? CLAMP((v->get_value() - v->get_min()) / travel, 0.0, 1.0) : 0.0;
		g.v_thumb = Rect2(g.v_bar.position.x + TA_THUMB_INSET, track_from + Math::round((track_len - thumb) * ratio),
				TA_SCROLLBAR - 2 * TA_THUMB_INSET, thumb);
	}
	if (g.horizontal) {
		real_t track_from = g.h_bar.position.x + TA_SCROLL_BUTTON + 1;
		real_t track_len = MAX((real_t)0, g.h_bar.size.x - 2 * TA_SCROLL_BUTTON - 2);
		real_t range = MAX(1.0, h->get_max() - h->get_min());
		real_t page = CLAMP(h->get_page(), 0.0, range);
		real_t thumb = CLAMP(Math::round(track_len * page / range), MIN(TA_THUMB_MIN, track_len), track_len);
		real_t travel = range - page;
		real_t ratio = travel > 0 ? CLAMP((h->get_value() - h->get_min()) / travel, 0.0, 1.0) : 0.0;
		g.h_thumb = Rect2(track_from + Math::round((track_len - thumb) * ratio), g.h_bar.position.y + TA_THUMB_INSET,
				thumb, TA_SCROLLBAR - 2 * TA_THUMB_INSET);
	}
	return g;
}

WebTextArea::ChromePart WebTextArea::_part_at(const Point2 &p_pos) const {
	BoxModel b = _compute_box_model();
	ScrollGeometry g = _scroll_geometry(b);
	bool can_resize = resize_mode != RESIZE_NONE && !disabled;
	if (can_resize && g.corner_rect.has_point(p_pos)) {
		return PART_RESIZER;
	}
	if (g.vertical && g.v_bar.has_point(p_pos)) {
		real_t y = p_pos.y - g.v_bar.position.y;
		if (y < TA_SCROLL_BUTTON) {
			return PART_V_DECREMENT;
		}
		if (y >= g.v_bar.size.y - TA_SCROLL_BUTTON) {
			return PART_V_INCREMENT;
		}
		if (p_pos.y < g.v_thumb.position.y) {
			return PART_V_TRACK_BEFORE;
		}
		if (p_pos.y >= g.v_thumb.position.y + g.v_thumb.size.y) {
			return PART_V_TRACK_AFTER;
		}
		return PART_V_THUMB;
	}
	if (g.horizontal && g.h_bar.has_point(p_pos)) {
		real_t x = p_pos.x - g.h_bar.position.x;
		if (x < TA_SCROLL_BUTTON) {
			return PART_H_DECREMENT;
		}
		if (x >= g.h_bar.size.x - TA_SCROLL_BUTTON) {
			return PART_H_INCREMENT;
		}
		if (p_pos.x < g.h_thumb.position.x) {
			return PART_H_TRACK_BEFORE;
		}
		if (p_pos.x >= g.h_thumb.position.x + g.h_thumb.size.x) {
			return PART_H_TRACK_AFTER;
		}
		return PART_H_THUMB;
	}
	return PART_NONE;
}

void WebTextArea::_draw_triangle(Control *p_canvas, const Point2 &p_center, int p_direction, const Color &p_color) const {
	// A 9x7 arrow (point first), the shape Chromium's scrollbar buttons paint.
	// Direction: 0 up, 1 down, 2 left, 3 right.
	Vector<Point2> pts;
	const real_t half_base = 4.5;
	const real_t half_height = 3.5;
	switch (p_direction) {
		case 0:
			pts.push_back(p_center + Point2(0, -half_height));
			pts.push_back(p_center + Point2(half_base, half_height));
			pts.push_back(p_center + Point2(-half_base, half_height));
			break;
		case 1:
			pts.push_back(p_center + Point2(0, half_height));
			pts.push_back(p_center + Point2(-half_base, -half_height));
			pts.push_back(p_center + Point2(half_base, -half_height));
			break;
		case 2:
			pts.push_back(p_center + Point2(-half_height, 0));
			pts.push_back(p_center + Point2(half_height, -half_base));
			pts.push_back(p_center + Point2(half_height, half_base));
			break;
		default:
			pts.push_back(p_center + Point2(half_height, 0));
			pts.push_back(p_center + Point2(-half_height, half_base));
			pts.push_back(p_center + Point2(-half_height, -half_base));
			break;
	}
	Vector<Color> colors;
	colors.push_back(p_color);
	p_canvas->draw_polygon(pts, colors);
	Vector<Point2> outline = pts;
	outline.push_back(pts[0]);
	p_canvas->draw_polyline(outline, p_color, 1.0, true);
}

void WebTextArea::_draw_resizer(Control *p_canvas, const Rect2 &p_corner, bool p_boxed) const {
	// Two diagonal strokes in the corner: a 7px one and a 3px one, each pixel
	// with a light pixel to its right when drawn inside the scroll corner.
	Color ink = p_boxed ? TA_GRIP : TA_GRIP_BARE;
	Point2 o = p_corner.position;
	for (int i = 0; i < 7; i++) {
		Point2 p = o + Point2(13 - i, 7 + i);
		p_canvas->draw_rect(Rect2(p, Size2(1, 1)), ink);
		if (p_boxed) {
			p_canvas->draw_rect(Rect2(p + Point2(1, 0), Size2(1, 1)), TA_GRIP_LIGHT);
		}
	}
	for (int i = 0; i < 3; i++) {
		Point2 p = o + Point2(13 - i, 11 + i);
		p_canvas->draw_rect(Rect2(p, Size2(1, 1)), ink);
		if (p_boxed) {
			p_canvas->draw_rect(Rect2(p + Point2(1, 0), Size2(1, 1)), TA_GRIP_LIGHT);
		}
	}
}

void WebTextArea::_draw_chrome(Control *p_canvas) {
	BoxModel b = _compute_box_model();
	ScrollGeometry g = _scroll_geometry(b);
	Ref<StyleBoxFlat> thumb_box;
	if (g.vertical || g.horizontal) {
		thumb_box.instantiate();
		thumb_box->set_corner_radius_all(4);
		thumb_box->set_anti_aliased(true);
	}
	if (g.vertical) {
		p_canvas->draw_rect(g.v_bar, TA_TRACK);
		real_t cx = g.v_bar.position.x + TA_SCROLLBAR * 0.5;
		_draw_triangle(p_canvas, Point2(cx, g.v_bar.position.y + 9), 0, TA_ARROW);
		_draw_triangle(p_canvas, Point2(cx, g.v_bar.position.y + g.v_bar.size.y - 8), 1, TA_ARROW);
		thumb_box->set_bg_color(pressed_part == PART_V_THUMB ? TA_THUMB_ACTIVE : TA_THUMB);
		p_canvas->draw_style_box(thumb_box, g.v_thumb);
	}
	if (g.horizontal) {
		p_canvas->draw_rect(g.h_bar, TA_TRACK);
		real_t cy = g.h_bar.position.y + TA_SCROLLBAR * 0.5;
		_draw_triangle(p_canvas, Point2(g.h_bar.position.x + 9, cy), 2, TA_ARROW);
		_draw_triangle(p_canvas, Point2(g.h_bar.position.x + g.h_bar.size.x - 8, cy), 3, TA_ARROW);
		thumb_box->set_bg_color(pressed_part == PART_H_THUMB ? TA_THUMB_ACTIVE : TA_THUMB);
		p_canvas->draw_style_box(thumb_box, g.h_thumb);
	}
	if (g.corner) {
		p_canvas->draw_rect(g.corner_rect, TA_TRACK);
		p_canvas->draw_rect(Rect2(g.corner_rect.position, Size2(TA_SCROLLBAR, 1)), TA_CORNER_LINE);
		p_canvas->draw_rect(Rect2(g.corner_rect.position, Size2(1, TA_SCROLLBAR)), TA_CORNER_LINE);
	}
	if (resize_mode != RESIZE_NONE) {
		_draw_resizer(p_canvas, g.corner_rect, g.corner);
	}
	// :focus-visible ring. A textarea always matches it when focused.
	if (editor && editor->has_focus() && !disabled) {
		Rect2 ring(Point2(), get_size());
		Ref<StyleBox> focus = has_theme_stylebox_override("focus") ? theme_cache.focus : ua_focus;
		if (focus.is_valid()) {
			p_canvas->draw_style_box(focus, ring);
		}
	}
}

void WebTextArea::_draw_box(const BoxModel &p_box) {
	Size2 size = get_size();
	if (size.x <= 0 || size.y <= 0) {
		return;
	}
	Color edge = _border_color(p_box);
	if (p_box.author_border) {
		Ref<StyleBoxFlat> sb;
		sb.instantiate();
		sb->set_bg_color(p_box.background);
		sb->set_border_width(SIDE_TOP, (int)Math::round(p_box.border[0]));
		sb->set_border_width(SIDE_RIGHT, (int)Math::round(p_box.border[1]));
		sb->set_border_width(SIDE_BOTTOM, (int)Math::round(p_box.border[2]));
		sb->set_border_width(SIDE_LEFT, (int)Math::round(p_box.border[3]));
		sb->set_border_color(edge);
		sb->set_corner_radius(CORNER_TOP_LEFT, (int)Math::round(p_box.radius[0]));
		sb->set_corner_radius(CORNER_TOP_RIGHT, (int)Math::round(p_box.radius[1]));
		sb->set_corner_radius(CORNER_BOTTOM_RIGHT, (int)Math::round(p_box.radius[2]));
		sb->set_corner_radius(CORNER_BOTTOM_LEFT, (int)Math::round(p_box.radius[3]));
		bool rounded = p_box.radius[0] > 0 || p_box.radius[1] > 0 || p_box.radius[2] > 0 || p_box.radius[3] > 0;
		sb->set_anti_aliased(rounded);
		draw_style_box(sb, Rect2(Point2(), size));
		return;
	}
	// The native field: a 1px edge whose four corner pixels are left unpainted,
	// with the fill stopping at the same notch.
	draw_rect(Rect2(1, 0, size.x - 2, size.y), p_box.background);
	draw_rect(Rect2(0, 1, 1, size.y - 2), p_box.background);
	draw_rect(Rect2(size.x - 1, 1, 1, size.y - 2), p_box.background);
	draw_rect(Rect2(1, 0, size.x - 2, 1), edge);
	draw_rect(Rect2(1, size.y - 1, size.x - 2, 1), edge);
	draw_rect(Rect2(0, 1, 1, size.y - 2), edge);
	draw_rect(Rect2(size.x - 1, 1, 1, size.y - 2), edge);
}

// ---------------------------------------------------------------------------
// Chrome interaction
// ---------------------------------------------------------------------------
void WebTextArea::_scroll_lines(bool p_vertical, int p_lines) {
	if (!editor) {
		return;
	}
	if (p_vertical) {
		// The arrow buttons step 40px, whatever the line height.
		real_t row = MAX(1, editor->get_line_height());
		editor->set_v_scroll(MAX(0.0, editor->get_v_scroll() + p_lines * 40.0 / row));
	} else {
		editor->set_h_scroll(MAX(0, editor->get_h_scroll() + p_lines * 40));
	}
	if (chrome) {
		chrome->queue_redraw();
	}
}

void WebTextArea::_scroll_pages(bool p_vertical, int p_pages) {
	if (!editor) {
		return;
	}
	ScrollBar *bar = p_vertical ? (ScrollBar *)editor->get_v_scroll_bar() : (ScrollBar *)editor->get_h_scroll_bar();
	if (!bar) {
		return;
	}
	real_t step = MAX(1.0, bar->get_page() * 0.875);
	if (p_vertical) {
		editor->set_v_scroll(MAX(0.0, editor->get_v_scroll() + p_pages * step));
	} else {
		editor->set_h_scroll(MAX(0, editor->get_h_scroll() + (int)Math::round(p_pages * step)));
	}
	if (chrome) {
		chrome->queue_redraw();
	}
}

void WebTextArea::_chrome_input(const Ref<InputEvent> &p_event) {
	Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_valid()) {
		if (mb->get_button_index() == MouseButton::WHEEL_UP || mb->get_button_index() == MouseButton::WHEEL_DOWN) {
			if (mb->is_pressed()) {
				_scroll_lines(true, mb->get_button_index() == MouseButton::WHEEL_UP ? -1 : 1);
			}
			return;
		}
		if (mb->get_button_index() != MouseButton::LEFT) {
			return;
		}
		if (!mb->is_pressed()) {
			pressed_part = PART_NONE;
			if (chrome) {
				chrome->queue_redraw();
			}
			return;
		}
		ChromePart part = _part_at(mb->get_position());
		pressed_part = part;
		drag_origin = mb->get_position();
		switch (part) {
			case PART_V_DECREMENT:
				_scroll_lines(true, -1);
				break;
			case PART_V_INCREMENT:
				_scroll_lines(true, 1);
				break;
			case PART_V_TRACK_BEFORE:
				_scroll_pages(true, -1);
				break;
			case PART_V_TRACK_AFTER:
				_scroll_pages(true, 1);
				break;
			case PART_H_DECREMENT:
				_scroll_lines(false, -1);
				break;
			case PART_H_INCREMENT:
				_scroll_lines(false, 1);
				break;
			case PART_H_TRACK_BEFORE:
				_scroll_pages(false, -1);
				break;
			case PART_H_TRACK_AFTER:
				_scroll_pages(false, 1);
				break;
			case PART_V_THUMB:
				drag_scroll_origin = editor ? editor->get_v_scroll_bar()->get_value() : 0.0;
				break;
			case PART_H_THUMB:
				drag_scroll_origin = editor ? editor->get_h_scroll_bar()->get_value() : 0.0;
				break;
			case PART_RESIZER:
				drag_size_origin = get_size();
				break;
			default:
				break;
		}
		if (chrome) {
			chrome->queue_redraw();
		}
		return;
	}

	Ref<InputEventMouseMotion> mm = p_event;
	if (mm.is_null() || pressed_part == PART_NONE || !editor) {
		return;
	}
	Vector2 delta = mm->get_position() - drag_origin;
	BoxModel b = _compute_box_model();
	ScrollGeometry g = _scroll_geometry(b);
	if (pressed_part == PART_V_THUMB && g.vertical) {
		ScrollBar *v = editor->get_v_scroll_bar();
		real_t track_len = MAX((real_t)1, g.v_bar.size.y - 2 * TA_SCROLL_BUTTON - 2 - g.v_thumb.size.y);
		real_t travel = MAX(0.0, v->get_max() - v->get_min() - v->get_page());
		editor->set_v_scroll(CLAMP(drag_scroll_origin + delta.y * travel / track_len, 0.0, travel));
		chrome->queue_redraw();
	} else if (pressed_part == PART_H_THUMB && g.horizontal) {
		ScrollBar *h = editor->get_h_scroll_bar();
		real_t track_len = MAX((real_t)1, g.h_bar.size.x - 2 * TA_SCROLL_BUTTON - 2 - g.h_thumb.size.x);
		real_t travel = MAX(0.0, h->get_max() - h->get_min() - h->get_page());
		editor->set_h_scroll((int)Math::round(CLAMP(drag_scroll_origin + delta.x * travel / track_len, 0.0, travel)));
		chrome->queue_redraw();
	} else if (pressed_part == PART_RESIZER) {
		real_t extra_w = b.border[1] + b.border[3] + b.padding[1] + b.padding[3];
		real_t extra_h = b.border[0] + b.border[2] + b.padding[0] + b.padding[2];
		Size2 next = drag_size_origin;
		if (resize_mode == RESIZE_BOTH || resize_mode == RESIZE_HORIZONTAL) {
			next.x = MAX(drag_size_origin.x + delta.x, extra_w + TA_SCROLLBAR);
		}
		if (resize_mode == RESIZE_BOTH || resize_mode == RESIZE_VERTICAL) {
			next.y = MAX(drag_size_origin.y + delta.y, extra_h + TA_SCROLLBAR);
		}
		// The browser writes the dragged size as an inline content-box size.
		bool border_box_sizing = css.has_box_sizing && css.border_box;
		css.has_width = false;
		css.has_height = false;
		input_width = (int)Math::round(border_box_sizing ? next.x : next.x - extra_w);
		input_height = (int)Math::round(border_box_sizing ? next.y : next.y - extra_h);
		update_minimum_size();
		set_size(next);
		emit_signal(SNAME("user_resized"), next);
	}
}

void WebTextArea::_on_editor_text_changed() {
	if (syncing || !editor) {
		return;
	}
	String text = editor->get_text();
	if (maxlength >= 0 && _utf16_length(text) > maxlength && _utf16_length(text) > _utf16_length(value)) {
		// Drag-and-drop and IME commits bypass the typed-input hooks; keep the
		// browser's rule that user input never grows the value past maxlength.
		syncing = true;
		int line = editor->get_caret_line();
		int column = editor->get_caret_column();
		editor->set_text(value);
		editor->set_caret_line(MIN(line, editor->get_line_count() - 1));
		editor->set_caret_column(column);
		syncing = false;
		return;
	}
	value = text;
	user_edited = true;
	emit_signal(SNAME("value_changed"), value);
	if (chrome) {
		chrome->queue_redraw();
	}
}

void WebTextArea::_on_editor_scrolled() {
	if (chrome) {
		chrome->queue_redraw();
	}
}

// ---------------------------------------------------------------------------
// Notifications / input
// ---------------------------------------------------------------------------
void WebTextArea::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			_ua_monospace_font();
			_rebuild_css_font();
			_sync_editor();
			update_minimum_size();
		} break;

		case NOTIFICATION_READY: {
			apply_autofocus();
		} break;

		case NOTIFICATION_RESIZED: {
			_sync_editor();
		} break;

		case NOTIFICATION_THEME_CHANGED: {
			_rebuild_css_font();
			metrics_cache.font_id = ObjectID();
			_sync_editor();
			update_minimum_size();
			queue_redraw();
		} break;

		case NOTIFICATION_MOUSE_ENTER: {
			hovered = true;
			queue_redraw();
		} break;

		case NOTIFICATION_MOUSE_EXIT: {
			hovered = false;
			queue_redraw();
		} break;

		case NOTIFICATION_FOCUS_ENTER: {
			if (editor && !disabled && editor->get_focus_mode() != FOCUS_NONE && !editor->has_focus()) {
				editor->grab_focus();
			}
		} break;

		case NOTIFICATION_DRAW: {
			_draw_box(_compute_box_model());
			if (chrome) {
				chrome->queue_redraw();
			}
		} break;
	}
}

void WebTextArea::gui_input(const Ref<InputEvent> &p_event) {
	// The editor and the chrome take every event that reaches the field.
}

// ---------------------------------------------------------------------------
// Properties
// ---------------------------------------------------------------------------
void WebTextArea::set_value(const String &p_value) {
	String normalized = p_value.remove_char('\r');
	// Compare with the live text too: typed edits reach `value` a frame late.
	if (value == normalized && get_value() == normalized) {
		return;
	}
	value = normalized;
	user_edited = false;
	_sync_editor();
}

String WebTextArea::get_value() const {
	// TextEdit reports edits one frame late (text_changed is deferred), so the
	// editor's own text is the live value.
	return editor ? editor->get_text() : value;
}

void WebTextArea::set_default_value(const String &p_value) {
	default_value = p_value.remove_char('\r');
}

String WebTextArea::get_default_value() const {
	return default_value;
}

void WebTextArea::set_placeholder(const String &p_placeholder) {
	if (placeholder == p_placeholder) {
		return;
	}
	placeholder = p_placeholder;
	_sync_editor();
}

String WebTextArea::get_placeholder() const {
	return placeholder;
}

void WebTextArea::set_input_name(const String &p_name) {
	attr_name = p_name;
}

String WebTextArea::get_input_name() const {
	return attr_name;
}

void WebTextArea::set_cols(int p_cols) {
	// A missing, zero or invalid `cols` falls back to the default 20.
	p_cols = p_cols > 0 ? p_cols : 20;
	if (cols == p_cols) {
		return;
	}
	cols = p_cols;
	update_minimum_size();
	_sync_editor();
}

int WebTextArea::get_cols() const {
	return cols;
}

void WebTextArea::set_rows(int p_rows) {
	p_rows = p_rows > 0 ? p_rows : 2;
	if (rows == p_rows) {
		return;
	}
	rows = p_rows;
	update_minimum_size();
	_sync_editor();
}

int WebTextArea::get_rows() const {
	return rows;
}

void WebTextArea::set_wrap_mode(WrapMode p_mode) {
	ERR_FAIL_INDEX(p_mode, 3);
	if (wrap_mode == p_mode) {
		return;
	}
	wrap_mode = p_mode;
	_sync_editor();
}

WebTextArea::WrapMode WebTextArea::get_wrap_mode() const {
	return wrap_mode;
}

void WebTextArea::set_wrap_string(const String &p_wrap) {
	String w = p_wrap.strip_edges().to_lower();
	set_wrap_mode(w == "off" ? WRAP_OFF : (w == "hard" ? WRAP_HARD : WRAP_SOFT));
}

String WebTextArea::get_wrap_string() const {
	switch (wrap_mode) {
		case WRAP_HARD:
			return "hard";
		case WRAP_OFF:
			return "off";
		default:
			return "soft";
	}
}

void WebTextArea::set_resize_mode(ResizeMode p_mode) {
	ERR_FAIL_INDEX(p_mode, 4);
	resize_mode = p_mode;
	if (chrome) {
		chrome->queue_redraw();
	}
}

WebTextArea::ResizeMode WebTextArea::get_resize_mode() const {
	return resize_mode;
}

void WebTextArea::set_disabled(bool p_disabled) {
	if (disabled == p_disabled) {
		return;
	}
	disabled = p_disabled;
	if (disabled && editor && editor->has_focus()) {
		editor->release_focus();
	}
	_sync_editor();
}

bool WebTextArea::is_disabled() const {
	return disabled;
}

void WebTextArea::set_readonly(bool p_readonly) {
	if (readonly == p_readonly) {
		return;
	}
	readonly = p_readonly;
	_sync_editor();
}

bool WebTextArea::is_readonly() const {
	return readonly;
}

void WebTextArea::set_required(bool p_required) {
	required = p_required;
}

bool WebTextArea::is_required() const {
	return required;
}

void WebTextArea::set_autofocus(bool p_autofocus) {
	autofocus = p_autofocus;
}

bool WebTextArea::has_autofocus() const {
	return autofocus;
}

void WebTextArea::set_maxlength(int p_maxlength) {
	maxlength = p_maxlength >= 0 ? p_maxlength : -1;
}

int WebTextArea::get_maxlength() const {
	return maxlength;
}

void WebTextArea::set_minlength(int p_minlength) {
	minlength = p_minlength >= 0 ? p_minlength : -1;
}

int WebTextArea::get_minlength() const {
	return minlength;
}

void WebTextArea::set_input_width(int p_width) {
	input_width = p_width;
	update_minimum_size();
	_sync_editor();
}

int WebTextArea::get_input_width() const {
	return input_width;
}

void WebTextArea::set_input_height(int p_height) {
	input_height = p_height;
	update_minimum_size();
	_sync_editor();
}

int WebTextArea::get_input_height() const {
	return input_height;
}

void WebTextArea::set_font_locale(const String &p_locale) {
	if (font_locale == p_locale) {
		return;
	}
	font_locale = p_locale;
	if (is_inside_tree()) {
		_ua_monospace_font();
		_rebuild_css_font();
		update_minimum_size();
		_sync_editor();
	}
}

String WebTextArea::get_font_locale() const {
	return font_locale;
}

TextEdit *WebTextArea::get_editor() const {
	return editor;
}

// ---------------------------------------------------------------------------
// Form behavior
// ---------------------------------------------------------------------------
String WebTextArea::get_form_value() const {
	if (wrap_mode != WRAP_HARD || !editor) {
		return get_value();
	}
	PackedStringArray lines;
	for (int i = 0; i < editor->get_line_count(); i++) {
		Vector<String> wrapped = editor->get_line_wrapped_text(i);
		if (wrapped.is_empty()) {
			lines.push_back(editor->get_line(i));
			continue;
		}
		for (const String &part : wrapped) {
			lines.push_back(part);
		}
	}
	return String("\n").join(lines);
}

void WebTextArea::reset() {
	user_edited = false;
	if (get_value() != default_value || value != default_value) {
		value = default_value;
		_sync_editor();
		emit_signal(SNAME("value_changed"), value);
	}
	if (editor) {
		editor->clear_undo_history();
	}
}

bool WebTextArea::apply_autofocus() {
	if (!autofocus || disabled || !editor || !is_visible_in_tree() || get_focus_mode() == FOCUS_NONE) {
		return false;
	}
	callable_mp((Control *)editor, &Control::grab_focus).call_deferred(false);
	return true;
}

Dictionary WebTextArea::get_validity() const {
	// A disabled or readonly textarea is barred from constraint validation.
	bool barred = disabled || readonly;
	String current = get_value();
	int length = _utf16_length(current);
	bool value_missing = !barred && required && current.is_empty();
	// tooLong / tooShort only apply to a value the user edited.
	bool too_long = !barred && user_edited && maxlength >= 0 && length > maxlength;
	bool too_short = !barred && user_edited && minlength >= 0 && length > 0 && length < minlength;
	Dictionary d;
	d["value_missing"] = value_missing;
	d["too_long"] = too_long;
	d["too_short"] = too_short;
	d["length"] = length;
	d["valid"] = !(value_missing || too_long || too_short);
	return d;
}

bool WebTextArea::check_validity() const {
	return (bool)get_validity()["valid"];
}

void WebTextArea::simulate_type(const String &p_text) {
	if (!editor || !editor->is_editable()) {
		return;
	}
	user_edited = true;
	for (int i = 0; i < p_text.length(); i++) {
		char32_t c = p_text[i];
		if (c == '\r') {
			continue;
		}
		if (c == '\n') {
			if (editor->_room_for_input(-1) >= 1) {
				editor->insert_text_at_caret("\n");
			}
			continue;
		}
		editor->_handle_unicode_input_internal(c, -1);
	}
}

// ---------------------------------------------------------------------------
// CSS author styling
// ---------------------------------------------------------------------------
static void _ta_expand_box(const String &p_val, bool (&r_has)[4], real_t (&r_out)[4]) {
	Vector<String> parts = p_val.split(" ", false);
	real_t v[4] = { 0, 0, 0, 0 };
	int n = 0;
	for (const String &p : parts) {
		real_t len;
		if (WebInput::_parse_css_length(p, len) && n < 4) {
			v[n++] = len;
		}
	}
	if (n == 0) {
		return;
	}
	real_t t = v[0], r = v[0], b = v[0], l = v[0];
	if (n == 2) {
		r = l = v[1];
	} else if (n == 3) {
		r = l = v[1];
		b = v[2];
	} else if (n == 4) {
		r = v[1];
		b = v[2];
		l = v[3];
	}
	r_out[0] = t;
	r_out[1] = r;
	r_out[2] = b;
	r_out[3] = l;
	for (int i = 0; i < 4; i++) {
		r_has[i] = true;
	}
}

static int _ta_side(const String &p_prop) {
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

void WebTextArea::set_css(const String &p_property, const String &p_value) {
	String prop = p_property.strip_edges().to_lower();
	String val = p_value.strip_edges();
	String low = val.to_lower();
	real_t len;
	Color col;
	bool clear = val.is_empty();

	if (prop == "background-color" || prop == "background") {
		css.has_bg = !clear && WebInput::_parse_css_color(val, col);
		css.bg = col;
	} else if (prop == "color") {
		css.has_color = !clear && WebInput::_parse_css_color(val, col);
		css.color = col;
	} else if (prop == "border") {
		if (clear) {
			css.border_none = false;
			css.has_bcol = false;
			for (int i = 0; i < 4; i++) {
				css.has_bw[i] = false;
			}
		}
		for (const String &t : val.split(" ", false)) {
			String tl = t.to_lower();
			if (tl == "none" || tl == "hidden") {
				css.border_none = true;
			} else if (WebInput::_parse_css_length(t, len)) {
				for (int i = 0; i < 4; i++) {
					css.has_bw[i] = true;
					css.bw[i] = len;
				}
			} else if (WebInput::_parse_css_color(t, col)) {
				css.has_bcol = true;
				css.bcol = col;
			} else {
				css.border_none = false;
			}
		}
	} else if (prop == "border-width") {
		for (int i = 0; i < 4; i++) {
			css.has_bw[i] = false;
		}
		_ta_expand_box(val, css.has_bw, css.bw);
	} else if (prop == "border-color") {
		css.has_bcol = !clear && WebInput::_parse_css_color(val, col);
		css.bcol = col;
	} else if (prop == "border-style") {
		css.border_none = low == "none" || low == "hidden";
	} else if (prop == "border-radius") {
		for (int i = 0; i < 4; i++) {
			css.has_radius[i] = false;
		}
		_ta_expand_box(val, css.has_radius, css.radius);
	} else if (prop.begins_with("border-") && prop.ends_with("-width")) {
		int side = _ta_side(prop);
		if (side >= 0) {
			css.has_bw[side] = !clear && WebInput::_parse_css_length(val, len);
			css.bw[side] = len;
		}
	} else if (prop == "padding") {
		for (int i = 0; i < 4; i++) {
			css.has_pad[i] = false;
		}
		_ta_expand_box(val, css.has_pad, css.pad);
	} else if (prop.begins_with("padding-")) {
		int side = _ta_side(prop);
		if (side >= 0) {
			css.has_pad[side] = !clear && WebInput::_parse_css_length(val, len);
			css.pad[side] = len;
		}
	} else if (prop == "box-sizing") {
		css.has_box_sizing = !clear;
		css.border_box = low == "border-box";
	} else if (prop == "width") {
		css.has_width = !clear && low != "auto" && WebInput::_parse_css_length(val, len);
		css.width = len;
	} else if (prop == "height") {
		css.has_height = !clear && low != "auto" && WebInput::_parse_css_length(val, len);
		css.height = len;
	} else if (prop == "font-size") {
		css.has_font_size = !clear && WebInput::_parse_css_length(val, len) && len > 0;
		css.font_size = len;
	} else if (prop == "font-family") {
		css.font_family = val;
	} else if (prop == "font-weight") {
		css.has_font_weight = !clear;
		css.font_weight = WebInput::_parse_css_font_weight(val);
	} else if (prop == "line-height") {
		css.has_line_height = !clear && low != "normal" && WebInput::_parse_css_length(val, len) && len > 0;
		css.line_height = len;
	} else if (prop == "letter-spacing") {
		css.has_letter_spacing = !clear && low != "normal" && WebInput::_parse_css_length(val, len);
		css.letter_spacing = len;
	} else if (prop == "text-align") {
		css.has_text_align = !clear;
		css.text_align = low == "center" ? (int)HORIZONTAL_ALIGNMENT_CENTER : ((low == "right" || low == "end") ? (int)HORIZONTAL_ALIGNMENT_RIGHT : (int)HORIZONTAL_ALIGNMENT_LEFT);
	} else if (prop == "resize") {
		set_resize_mode(low == "none" ? RESIZE_NONE : (low == "horizontal" ? RESIZE_HORIZONTAL : (low == "vertical" ? RESIZE_VERTICAL : RESIZE_BOTH)));
	}
	_rebuild_css_font();
	metrics_cache.font_id = ObjectID();
	update_minimum_size();
	_sync_editor();
}

void WebTextArea::set_css_text(const String &p_css) {
	for (const String &decl : p_css.split(";", false)) {
		int colon = decl.find(":");
		if (colon <= 0) {
			continue;
		}
		set_css(decl.substr(0, colon), decl.substr(colon + 1));
	}
}

void WebTextArea::clear_css() {
	css = CssStyle();
	css_font = Ref<Font>();
	metrics_cache.font_id = ObjectID();
	update_minimum_size();
	_sync_editor();
}

// ---------------------------------------------------------------------------
// Metrics
// ---------------------------------------------------------------------------
static Array _ta_color_array(const Color &p_color) {
	Array a;
	a.push_back(p_color.r);
	a.push_back(p_color.g);
	a.push_back(p_color.b);
	a.push_back(p_color.a);
	return a;
}

Dictionary WebTextArea::get_test_metrics() const {
	BoxModel b = _compute_box_model();
	ScrollGeometry g = _scroll_geometry(b);
	Dictionary d;
	Dictionary size_d;
	size_d["w"] = get_size().x;
	size_d["h"] = get_size().y;
	d["size"] = size_d;
	Dictionary box_d;
	box_d["w"] = b.border_box.x;
	box_d["h"] = b.border_box.y;
	d["border_box"] = box_d;
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
	d["font_size"] = b.font_size;
	d["line_height"] = b.line_height;
	d["char_width"] = b.char_width;
	d["row_height"] = editor ? editor->get_line_height() : 0;
	d["background_color"] = _ta_color_array(b.background);
	d["text_color"] = _ta_color_array(b.text_color);
	d["border_color"] = _ta_color_array(_border_color(b));
	Ref<Font> font = _resolved_font();
	d["font"] = font.is_valid() ? font->get_font_name() : String();
	d["ua_font"] = ua_font.is_valid() ? ua_font->get_font_name() : String();
	d["font_locale_effective"] = _effective_font_locale();
	d["theme_font"] = _theme_font().is_valid() ? _theme_font()->get_font_name() : String();
	d["v_scrollbar"] = g.vertical;
	d["h_scrollbar"] = g.horizontal;
	d["wrap"] = get_wrap_string();
	d["value"] = get_value();
	d["focused"] = editor && editor->has_focus();
	return d;
}

// ---------------------------------------------------------------------------
void WebTextArea::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_value", "value"), &WebTextArea::set_value);
	ClassDB::bind_method(D_METHOD("get_value"), &WebTextArea::get_value);
	ClassDB::bind_method(D_METHOD("set_default_value", "value"), &WebTextArea::set_default_value);
	ClassDB::bind_method(D_METHOD("get_default_value"), &WebTextArea::get_default_value);
	ClassDB::bind_method(D_METHOD("set_placeholder", "placeholder"), &WebTextArea::set_placeholder);
	ClassDB::bind_method(D_METHOD("get_placeholder"), &WebTextArea::get_placeholder);
	ClassDB::bind_method(D_METHOD("set_input_name", "name"), &WebTextArea::set_input_name);
	ClassDB::bind_method(D_METHOD("get_input_name"), &WebTextArea::get_input_name);
	ClassDB::bind_method(D_METHOD("set_cols", "cols"), &WebTextArea::set_cols);
	ClassDB::bind_method(D_METHOD("get_cols"), &WebTextArea::get_cols);
	ClassDB::bind_method(D_METHOD("set_rows", "rows"), &WebTextArea::set_rows);
	ClassDB::bind_method(D_METHOD("get_rows"), &WebTextArea::get_rows);
	ClassDB::bind_method(D_METHOD("set_wrap_mode", "mode"), &WebTextArea::set_wrap_mode);
	ClassDB::bind_method(D_METHOD("get_wrap_mode"), &WebTextArea::get_wrap_mode);
	ClassDB::bind_method(D_METHOD("set_wrap_string", "wrap"), &WebTextArea::set_wrap_string);
	ClassDB::bind_method(D_METHOD("get_wrap_string"), &WebTextArea::get_wrap_string);
	ClassDB::bind_method(D_METHOD("set_resize_mode", "mode"), &WebTextArea::set_resize_mode);
	ClassDB::bind_method(D_METHOD("get_resize_mode"), &WebTextArea::get_resize_mode);
	ClassDB::bind_method(D_METHOD("set_disabled", "disabled"), &WebTextArea::set_disabled);
	ClassDB::bind_method(D_METHOD("is_disabled"), &WebTextArea::is_disabled);
	ClassDB::bind_method(D_METHOD("set_readonly", "readonly"), &WebTextArea::set_readonly);
	ClassDB::bind_method(D_METHOD("is_readonly"), &WebTextArea::is_readonly);
	ClassDB::bind_method(D_METHOD("set_required", "required"), &WebTextArea::set_required);
	ClassDB::bind_method(D_METHOD("is_required"), &WebTextArea::is_required);
	ClassDB::bind_method(D_METHOD("set_autofocus", "autofocus"), &WebTextArea::set_autofocus);
	ClassDB::bind_method(D_METHOD("has_autofocus"), &WebTextArea::has_autofocus);
	ClassDB::bind_method(D_METHOD("set_maxlength", "maxlength"), &WebTextArea::set_maxlength);
	ClassDB::bind_method(D_METHOD("get_maxlength"), &WebTextArea::get_maxlength);
	ClassDB::bind_method(D_METHOD("set_minlength", "minlength"), &WebTextArea::set_minlength);
	ClassDB::bind_method(D_METHOD("get_minlength"), &WebTextArea::get_minlength);
	ClassDB::bind_method(D_METHOD("set_input_width", "width"), &WebTextArea::set_input_width);
	ClassDB::bind_method(D_METHOD("get_input_width"), &WebTextArea::get_input_width);
	ClassDB::bind_method(D_METHOD("set_input_height", "height"), &WebTextArea::set_input_height);
	ClassDB::bind_method(D_METHOD("get_input_height"), &WebTextArea::get_input_height);
	ClassDB::bind_method(D_METHOD("set_font_locale", "locale"), &WebTextArea::set_font_locale);
	ClassDB::bind_method(D_METHOD("get_font_locale"), &WebTextArea::get_font_locale);
	ClassDB::bind_method(D_METHOD("get_form_value"), &WebTextArea::get_form_value);
	ClassDB::bind_method(D_METHOD("reset"), &WebTextArea::reset);
	ClassDB::bind_method(D_METHOD("apply_autofocus"), &WebTextArea::apply_autofocus);
	ClassDB::bind_method(D_METHOD("get_validity"), &WebTextArea::get_validity);
	ClassDB::bind_method(D_METHOD("check_validity"), &WebTextArea::check_validity);
	ClassDB::bind_method(D_METHOD("set_css", "property", "value"), &WebTextArea::set_css);
	ClassDB::bind_method(D_METHOD("set_css_text", "css"), &WebTextArea::set_css_text);
	ClassDB::bind_method(D_METHOD("clear_css"), &WebTextArea::clear_css);
	ClassDB::bind_method(D_METHOD("get_test_metrics"), &WebTextArea::get_test_metrics);
	ClassDB::bind_method(D_METHOD("simulate_type", "text"), &WebTextArea::simulate_type);
	ClassDB::bind_method(D_METHOD("get_editor"), &WebTextArea::get_editor);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "value", PROPERTY_HINT_MULTILINE_TEXT), "set_value", "get_value");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "default_value", PROPERTY_HINT_MULTILINE_TEXT), "set_default_value", "get_default_value");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "placeholder"), "set_placeholder", "get_placeholder");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "input_name"), "set_input_name", "get_input_name");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "cols", PROPERTY_HINT_RANGE, "1,1000,1,or_greater"), "set_cols", "get_cols");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "rows", PROPERTY_HINT_RANGE, "1,1000,1,or_greater"), "set_rows", "get_rows");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "wrap_mode", PROPERTY_HINT_ENUM, "Soft,Hard,Off"), "set_wrap_mode", "get_wrap_mode");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "resize_mode", PROPERTY_HINT_ENUM, "None,Both,Horizontal,Vertical"), "set_resize_mode", "get_resize_mode");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "disabled"), "set_disabled", "is_disabled");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "readonly"), "set_readonly", "is_readonly");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "required"), "set_required", "is_required");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "autofocus"), "set_autofocus", "has_autofocus");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "maxlength", PROPERTY_HINT_RANGE, "-1,100000,1,or_greater"), "set_maxlength", "get_maxlength");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "minlength", PROPERTY_HINT_RANGE, "-1,100000,1,or_greater"), "set_minlength", "get_minlength");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "input_width"), "set_input_width", "get_input_width");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "input_height"), "set_input_height", "get_input_height");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "font_locale"), "set_font_locale", "get_font_locale");

	ADD_SIGNAL(MethodInfo("value_changed", PropertyInfo(Variant::STRING, "value")));
	ADD_SIGNAL(MethodInfo("user_resized", PropertyInfo(Variant::VECTOR2, "size")));

	BIND_ENUM_CONSTANT(WRAP_SOFT);
	BIND_ENUM_CONSTANT(WRAP_HARD);
	BIND_ENUM_CONSTANT(WRAP_OFF);
	BIND_ENUM_CONSTANT(RESIZE_NONE);
	BIND_ENUM_CONSTANT(RESIZE_BOTH);
	BIND_ENUM_CONSTANT(RESIZE_HORIZONTAL);
	BIND_ENUM_CONSTANT(RESIZE_VERTICAL);

	BIND_THEME_ITEM(Theme::DATA_TYPE_FONT, WebTextArea, font);
	BIND_THEME_ITEM(Theme::DATA_TYPE_FONT_SIZE, WebTextArea, font_size);
	BIND_THEME_ITEM(Theme::DATA_TYPE_COLOR, WebTextArea, font_color);
	BIND_THEME_ITEM(Theme::DATA_TYPE_COLOR, WebTextArea, font_placeholder_color);
	BIND_THEME_ITEM(Theme::DATA_TYPE_COLOR, WebTextArea, selection_color);
	BIND_THEME_ITEM(Theme::DATA_TYPE_COLOR, WebTextArea, caret_color);
	BIND_THEME_ITEM(Theme::DATA_TYPE_STYLEBOX, WebTextArea, focus);
}

static Ref<ImageTexture> _ta_blank_texture(int p_w, int p_h) {
	Ref<Image> image = Image::create_empty(p_w, p_h, false, Image::FORMAT_RGBA8);
	image->fill(Color(0, 0, 0, 0));
	return ImageTexture::create_from_image(image);
}

WebTextArea::WebTextArea() {
	Ref<StyleBoxFlat> ring;
	ring.instantiate();
	ring->set_draw_center(false);
	ring->set_border_width_all(2);
	ring->set_corner_radius_all(3);
	ring->set_anti_aliased(true);
	ring->set_border_color(TA_FOCUS_RING);
	ua_focus = ring;
	set_focus_mode(FOCUS_ALL);
	set_default_cursor_shape(CURSOR_IBEAM);
	_ua_monospace_font();

	editor = memnew(WebTextAreaEditor);
	editor->area = this;
	editor->set_mouse_filter(MOUSE_FILTER_PASS);
	Ref<StyleBoxEmpty> box;
	box.instantiate();
	editor_box = box;
	editor->add_theme_style_override("normal", box);
	editor->add_theme_style_override("focus", box);
	editor->add_theme_style_override("read_only", box);
	editor->add_theme_color_override("background_color", Color(0, 0, 0, 0));
	editor->add_theme_color_override("current_line_color", Color(0, 0, 0, 0));
	editor->add_theme_color_override("word_highlighted_color", Color(0, 0, 0, 0));
	editor->add_theme_color_override("font_selected_color", Color(0, 0, 0, 0));
	editor->add_theme_color_override("caret_background_color", Color(0, 0, 0, 0));
	editor->add_theme_color_override("search_result_color", Color(0, 0, 0, 0));
	editor->add_theme_color_override("search_result_border_color", Color(0, 0, 0, 0));
	editor->add_theme_constant_override("outline_size", 0);
	editor->add_theme_constant_override("caret_width", 1);
	editor->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	editor->set_line_wrapping_mode(TextEdit::LINE_WRAPPING_BOUNDARY);
	editor->set_context_menu_enabled(false);
	editor->set_caret_blink_enabled(true);
	editor->set_caret_blink_interval(0.5);
	editor->set_drag_and_drop_selection_enabled(true);
	editor->set_middle_mouse_paste_enabled(false);

	// The editor's own scrollbars stay in the layout — their 15px is what the
	// browser takes from the text when it overflows — but are never seen or
	// hit: the chrome paints and drives Chromium's scrollbars in their place.
	ScrollBar *bars[2] = { editor->get_v_scroll_bar(), editor->get_h_scroll_bar() };
	for (int i = 0; i < 2; i++) {
		ScrollBar *bar = bars[i];
		Ref<StyleBoxEmpty> empty;
		empty.instantiate();
		Ref<ImageTexture> icon = i == 0 ? _ta_blank_texture((int)TA_SCROLLBAR, (int)TA_SCROLL_BUTTON)
										: _ta_blank_texture((int)TA_SCROLL_BUTTON, (int)TA_SCROLLBAR);
		for (const char *name : { "scroll", "scroll_focus", "grabber", "grabber_highlight", "grabber_pressed" }) {
			bar->add_theme_style_override(name, empty);
		}
		for (const char *name : { "increment", "increment_highlight", "increment_pressed", "decrement", "decrement_highlight", "decrement_pressed" }) {
			bar->add_theme_icon_override(name, icon);
		}
		bar->set_self_modulate(Color(1, 1, 1, 0));
		bar->set_mouse_filter(MOUSE_FILTER_IGNORE);
		bar->connect(SceneStringName(value_changed), callable_mp(this, &WebTextArea::_on_editor_scrolled).unbind(1));
		bar->connect(SceneStringName(visibility_changed), callable_mp(this, &WebTextArea::_on_editor_scrolled));
		bar->connect(CoreStringName(changed), callable_mp(this, &WebTextArea::_on_editor_scrolled));
	}
	editor->connect(SceneStringName(text_changed), callable_mp(this, &WebTextArea::_on_editor_text_changed));
	editor->connect(SceneStringName(focus_entered), callable_mp(this, &WebTextArea::_on_editor_scrolled));
	editor->connect(SceneStringName(focus_exited), callable_mp(this, &WebTextArea::_on_editor_scrolled));
	add_child(editor, false, INTERNAL_MODE_FRONT);

	chrome = memnew(WebTextAreaChrome);
	chrome->area = this;
	chrome->set_mouse_filter(MOUSE_FILTER_STOP);
	add_child(chrome, false, INTERNAL_MODE_FRONT);
}
