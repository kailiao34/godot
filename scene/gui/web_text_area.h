/**************************************************************************/
/*  web_text_area.h                                                       */
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
#include "scene/gui/text_edit.h"

class Font;
class StyleBox;
class StyleBoxFlat;
class WebTextArea;

// The editor inside a WebTextArea. A TextEdit with the browser's input rules:
// `maxlength` caps what the user can type, paste or break into a new line,
// counted in UTF-16 code units the way the HTML API value is measured.
class WebTextAreaEditor : public TextEdit {
	GDCLASS(WebTextAreaEditor, TextEdit);

	friend class WebTextArea;
	WebTextArea *area = nullptr;

	// Code units the user may still add, given the current selection.
	int _room_for_input(int p_caret);
	String _clip_to_room(const String &p_text, int p_caret);

protected:
	virtual void _handle_unicode_input_internal(const uint32_t p_unicode, int p_caret) override;
	virtual void _paste_internal(int p_caret) override;
	virtual void gui_input(const Ref<InputEvent> &p_event) override;
	static void _bind_methods() {}
};

// Draws the parts Chromium paints over the field — scrollbars, the scroll
// corner with its resize grip, and the focus ring — and takes the pointer only
// over the scrollbars and the grip, so every other click reaches the editor.
class WebTextAreaChrome : public Control {
	GDCLASS(WebTextAreaChrome, Control);

	friend class WebTextArea;
	WebTextArea *area = nullptr;

protected:
	void _notification(int p_what);
	static void _bind_methods() {}

public:
	virtual bool has_point(const Point2 &p_point) const override;
	virtual void gui_input(const Ref<InputEvent> &p_event) override;
	virtual CursorShape get_cursor_shape(const Point2 &p_pos = Point2()) const override;
};

// WebTextArea: a Control that mirrors the HTML <textarea> element. Its intrinsic
// size, typography, chrome and editing rules reproduce Chromium's user-agent
// textarea; Control.size == the CSS border box.
class WebTextArea : public Control {
	GDCLASS(WebTextArea, Control);

	friend class WebTextAreaEditor;
	friend class WebTextAreaChrome;

public:
	enum WrapMode {
		WRAP_SOFT, // default: lines wrap on screen; the value keeps its own breaks.
		WRAP_HARD, // wraps on screen and submits the wrapped lines as real breaks.
		WRAP_OFF, // no wrapping; long lines scroll horizontally.
	};

	enum ResizeMode {
		RESIZE_NONE,
		RESIZE_BOTH, // UA default for textarea.
		RESIZE_HORIZONTAL,
		RESIZE_VERTICAL,
	};

	// Resolved box model in border-box terms (matches getBoundingClientRect).
	struct BoxModel {
		Size2 border_box;
		Size2 intrinsic_box; // auto size before an explicit width/height.
		real_t border[4] = { 1, 1, 1, 1 }; // top, right, bottom, left.
		real_t padding[4] = { 2, 2, 2, 2 };
		real_t radius[4] = { 0, 0, 0, 0 }; // TL, TR, BR, BL.
		Color background = Color(1, 1, 1, 1);
		Color text_color = Color(0, 0, 0, 1);
		Color border_color = Color(118.0 / 255, 118.0 / 255, 118.0 / 255, 1);
		bool author_border = false; // an author border/radius replaces the native edge.
		real_t font_size = 13.3333;
		real_t line_height = 0; // resolved px (normal resolved from font metrics).
		real_t char_width = 0; // the column width `cols` multiplies.
		int font_weight = 400;
		int text_align = 0; // HorizontalAlignment.
		real_t letter_spacing = 0;
	};

private:
	// --- HTML attributes ---
	String value; // the current (API) value.
	String default_value; // the markup content; reset() returns to it.
	String placeholder;
	String attr_name;
	int cols = 20;
	int rows = 2;
	WrapMode wrap_mode = WRAP_SOFT;
	ResizeMode resize_mode = RESIZE_BOTH;
	bool disabled = false;
	bool readonly = false;
	bool required = false;
	bool autofocus = false;
	int maxlength = -1;
	int minlength = -1;
	int input_width = -1; // explicit box width in px (-1 = auto).
	int input_height = -1; // explicit box height in px (-1 = auto).
	String font_locale; // decides the UA monospace font; empty = OS locale.

	// --- CSS author overrides (unset => UA default) ---
	struct CssStyle {
		bool has_bg = false;
		Color bg;
		bool has_bw[4] = { false, false, false, false };
		real_t bw[4] = { 0, 0, 0, 0 };
		bool has_bcol = false;
		Color bcol;
		bool border_none = false;
		bool has_radius[4] = { false, false, false, false };
		real_t radius[4] = { 0, 0, 0, 0 };
		bool has_pad[4] = { false, false, false, false };
		real_t pad[4] = { 0, 0, 0, 0 };
		bool has_box_sizing = false;
		bool border_box = false;
		bool has_width = false;
		real_t width = 0;
		bool has_height = false;
		real_t height = 0;
		bool has_color = false;
		Color color;
		bool has_font_size = false;
		real_t font_size = 0;
		String font_family;
		bool has_font_weight = false;
		int font_weight = 400;
		bool has_line_height = false;
		real_t line_height = 0; // px.
		bool has_text_align = false;
		int text_align = 0;
		bool has_letter_spacing = false;
		real_t letter_spacing = 0;
	};
	CssStyle css;
	Ref<Font> css_font; // resolved font when the author changes family/weight/spacing.

	struct ThemeCache {
		Ref<Font> font; // empty => the UA monospace font.
		int font_size = 0;
		Color font_color = Color(0, 0, 0, 1);
		Color font_placeholder_color = Color(117.0 / 255, 117.0 / 255, 117.0 / 255, 1);
		Color selection_color = Color(0.6, 0.8, 1.0, 0.4);
		Color caret_color = Color(0, 0, 0, 1);
		Ref<StyleBox> focus; // :focus-visible ring.
	} theme_cache;

	WebTextAreaEditor *editor = nullptr;
	WebTextAreaChrome *chrome = nullptr;
	Ref<StyleBox> editor_box; // carries the padding as content margins.
	Ref<StyleBox> ua_focus; // Chromium's 2px focus ring.
	Ref<Font> ua_font; // the locale's default fixed-pitch font.
	String ua_font_locale; // locale ua_font was resolved for.
	bool syncing = false; // set while pushing state into the editor.
	bool hovered = false;
	bool user_edited = false; // tooLong/tooShort only apply to user edits.

	// Chromium's `normal` line height and column width, cached per font.
	struct FontMetricsCache {
		ObjectID font_id;
		bool sfnt = false; // OS/2 + hhea were readable.
		int upm = 0;
		int hhea_ascent = 0;
		int hhea_descent = 0; // positive.
		int hhea_gap = 0;
		int win_ascent = 0;
		int win_descent = 0;
		real_t ascent_em = 0; // fallback when the tables are unavailable.
		real_t descent_em = 0;
		real_t x_advance_em = 0;
	};
	mutable FontMetricsCache metrics_cache;

	// Pointer interaction with the chrome.
	enum ChromePart {
		PART_NONE,
		PART_V_DECREMENT,
		PART_V_INCREMENT,
		PART_V_TRACK_BEFORE,
		PART_V_TRACK_AFTER,
		PART_V_THUMB,
		PART_H_DECREMENT,
		PART_H_INCREMENT,
		PART_H_TRACK_BEFORE,
		PART_H_TRACK_AFTER,
		PART_H_THUMB,
		PART_RESIZER,
	};
	ChromePart pressed_part = PART_NONE;
	Point2 drag_origin;
	double drag_scroll_origin = 0;
	Size2 drag_size_origin;

	struct ScrollGeometry {
		bool vertical = false;
		bool horizontal = false;
		bool corner = false;
		Rect2 v_bar;
		Rect2 h_bar;
		Rect2 corner_rect;
		Rect2 v_thumb;
		Rect2 h_thumb;
	};

	Ref<Font> _resolved_font() const;
	Ref<Font> _theme_font() const;
	Color _theme_color(const StringName &p_name, const Color &p_theme, const Color &p_ua) const;
	Ref<Font> _ua_monospace_font();
	String _effective_font_locale() const;
	void _rebuild_css_font();
	void _read_font_metrics(const Ref<Font> &p_font) const;
	real_t _normal_line_height(const Ref<Font> &p_font, real_t p_font_size) const;
	real_t _column_width(const Ref<Font> &p_font, real_t p_font_size) const;
	BoxModel _compute_box_model() const;
	void _sync_editor();
	void _apply_line_spacing(const BoxModel &p_box);
	ScrollGeometry _scroll_geometry(const BoxModel &p_box) const;
	ChromePart _part_at(const Point2 &p_pos) const;
	void _draw_box(const BoxModel &p_box);
	void _draw_chrome(Control *p_canvas);
	void _draw_triangle(Control *p_canvas, const Point2 &p_center, int p_direction, const Color &p_color) const;
	void _draw_resizer(Control *p_canvas, const Rect2 &p_corner, bool p_boxed) const;
	void _chrome_input(const Ref<InputEvent> &p_event);
	void _scroll_lines(bool p_vertical, int p_lines);
	void _scroll_pages(bool p_vertical, int p_pages);
	void _on_editor_text_changed();
	void _on_editor_scrolled();
	int _utf16_length(const String &p_text) const;
	Color _border_color(const BoxModel &p_box) const;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	virtual Size2 get_minimum_size() const override;
	virtual void gui_input(const Ref<InputEvent> &p_event) override;

	void set_value(const String &p_value);
	String get_value() const;
	void set_default_value(const String &p_value);
	String get_default_value() const;
	void set_placeholder(const String &p_placeholder);
	String get_placeholder() const;
	void set_input_name(const String &p_name);
	String get_input_name() const;
	void set_cols(int p_cols);
	int get_cols() const;
	void set_rows(int p_rows);
	int get_rows() const;
	void set_wrap_mode(WrapMode p_mode);
	WrapMode get_wrap_mode() const;
	void set_wrap_string(const String &p_wrap); // "soft" | "hard" | "off"
	String get_wrap_string() const;
	void set_resize_mode(ResizeMode p_mode);
	ResizeMode get_resize_mode() const;
	void set_disabled(bool p_disabled);
	bool is_disabled() const;
	void set_readonly(bool p_readonly);
	bool is_readonly() const;
	void set_required(bool p_required);
	bool is_required() const;
	void set_autofocus(bool p_autofocus);
	bool has_autofocus() const;
	void set_maxlength(int p_maxlength);
	int get_maxlength() const;
	void set_minlength(int p_minlength);
	int get_minlength() const;
	void set_input_width(int p_width);
	int get_input_width() const;
	void set_input_height(int p_height);
	int get_input_height() const;
	void set_font_locale(const String &p_locale);
	String get_font_locale() const;

	// The value a form submits: with wrap=hard the on-screen soft wraps become
	// line breaks, exactly as the browser serializes it.
	String get_form_value() const;
	// Restores the markup value (HTML form reset), discarding user edits.
	void reset();
	// Focuses the field when `autofocus` is set and it can take focus, the way a
	// page load does. Returns whether focus was taken.
	bool apply_autofocus();
	// HTML ValidityState subset a textarea can fail: value_missing, too_long,
	// too_short, valid.
	Dictionary get_validity() const;
	bool check_validity() const;

	void set_css(const String &p_property, const String &p_value);
	void set_css_text(const String &p_css);
	void clear_css();

	// Resolved geometry for comparison with Chromium.
	Dictionary get_test_metrics() const;
	// Automation: types text through the same input rules as the keyboard.
	void simulate_type(const String &p_text);
	TextEdit *get_editor() const;

	WebTextArea();
};

VARIANT_ENUM_CAST(WebTextArea::WrapMode);
VARIANT_ENUM_CAST(WebTextArea::ResizeMode);
