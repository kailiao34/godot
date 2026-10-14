/**************************************************************************/
/*  web_input.h                                                           */
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

class ColorPicker;
class LineEdit;
class Font;
class Texture2D;
class FileDialog;
class Popup;
class PopupPanel;
class StyleBox;
class StyleBoxFlat;
class TextLine;
class WebInputPickerPanel;

// WebInput: a Control node that mirrors the HTML <input> element. A single
// node switches appearance and behavior by its `input_type`, exactly like the
// HTML `type` attribute. Default styling reproduces Chromium's UA stylesheet
// (values captured by tools/webinput_bridge). Control.size == CSS width/height.
class WebInput : public Control {
	GDCLASS(WebInput, Control);

public:
	enum Type {
		TYPE_TEXT, // default
		TYPE_PASSWORD,
		TYPE_EMAIL,
		TYPE_URL,
		TYPE_TEL,
		TYPE_SEARCH,
		TYPE_NUMBER,
		TYPE_BUTTON,
		TYPE_SUBMIT,
		TYPE_RESET,
		TYPE_IMAGE,
		TYPE_CHECKBOX,
		TYPE_RADIO,
		TYPE_RANGE,
		TYPE_COLOR,
		TYPE_FILE,
		TYPE_HIDDEN,
		TYPE_DATE,
		TYPE_TIME,
		TYPE_DATETIME_LOCAL,
		TYPE_MONTH,
		TYPE_WEEK,
		TYPE_MAX,
	};

	// Where the label text sits relative to the input box, mirroring a wrapping
	// HTML <label> (text before/after the input, with or without a <br>).
	enum LabelPosition {
		LABEL_LEFT, // <label>Text<input></label>
		LABEL_TOP, // <label>Text<br><input></label>
		LABEL_RIGHT, // <label><input>Text</label>
		LABEL_BOTTOM, // <label><input><br>Text</label>
	};

	// Resolved box model for a given type, in border-box terms (matches the
	// browser getBoundingClientRect that the comparator checks against).
	struct BoxModel {
		Size2 border_box; // total size including border + padding.
		// Auto (intrinsic) size, captured before any explicit width/height is
		// applied. Content that does not stretch with the box -- the range
		// slider, the file button -- keeps this height.
		Size2 intrinsic_box;
		real_t border[4] = { 0, 0, 0, 0 }; // structural (CSS) top, right, bottom, left.
		// Drawn border thickness. Chromium's native theme paints a thinner line
		// than the CSS computed border (appearance:auto), so the drawn width can
		// differ from the structural width reported to the comparator. -1 = use
		// the structural width.
		real_t draw_border[4] = { -1, -1, -1, -1 };
		real_t padding[4] = { 0, 0, 0, 0 }; // top, right, bottom, left.
		Color background = Color(1, 1, 1, 1);
		Color text_color = Color(0, 0, 0, 1);
		Color border_color = Color(0, 0, 0, 1); // structural (CSS computed).
		Color draw_border_color = Color(0, 0, 0, 1); // painted (native theme).
		bool draw_border_color_set = false;
		bool border_inset = false; // 3D inset bevel (text fields).
		bool border_outset = false; // 3D outset bevel (buttons).
		real_t font_size = 13.3333;

		// CSS author overrides resolved into the box (defaults are inert).
		real_t radius[4] = { 0, 0, 0, 0 }; // border-radius TL, TR, BR, BL.
		bool has_shadow = false; // box-shadow present.
		Color shadow_color = Color(0, 0, 0, 0);
		Vector2 shadow_offset;
		real_t shadow_blur = 0;
		real_t shadow_spread = 0;
		int font_weight = 400;
		real_t line_height = 0; // <=0 => normal.
		int text_align = -1; // HorizontalAlignment; -1 => UA default.
		real_t letter_spacing = 0;
	};

private:
	Type input_type = TYPE_TEXT;

	// --- HTML attributes (the commonly-typed ones get real properties; the
	// long tail lives in `extra_attributes` and is set via set_html_attribute). ---
	String attr_name;
	String value;
	String placeholder;
	bool disabled = false;
	// False when the host paints the control's background and border itself
	// (an author-styled field whose box the host composes like any CSS box);
	// text, carets, scrollbars and the focus ring are still drawn here.
	bool draw_box = true;
	bool readonly = false;
	bool required = false;
	bool checked = false; // checkbox/radio
	int size_chars = 20; // `size` attribute (text family default 20)
	int input_width = -1; // explicit box width in px (-1 = auto / UA default).
	int input_height = -1; // explicit box height in px (-1 = auto).
	int maxlength = -1;
	int minlength = -1;
	// Numeric bounds for type=number / type=range. HTML carries these as
	// strings, but they are exposed as plain integers here; see the class docs
	// for what that rules out (fractional steps, unbounded number fields).
	int min_value = 0;
	int max_value = 100;
	int step_value = 1;
	// The attribute text as authored: Chromium sizes a number field from the
	// digits of min/max/step, which the integer properties cannot carry.
	String min_attr;
	String max_attr;
	String step_attr;
	String pattern;
	bool multiple = false;
	String src; // image
	String alt; // image
	String accept; // file: comma-separated extensions / mime types.
	PackedStringArray selected_files; // file: chosen paths.
	FileDialog *file_dialog = nullptr;
	// The one dialog serves both type=file (picking the submitted files) and
	// type=image (picking the button's own image), so it records what it is for.
	enum FileTarget {
		FILE_TARGET_FILES,
		FILE_TARGET_SRC,
	};
	FileTarget file_target = FILE_TARGET_FILES;
	int image_width = -1; // image render width (-1 = natural).
	int image_height = -1; // image render height.
	Ref<Texture2D> src_texture; // loaded image for type=image.
	String default_value; // markup `value` for form reset (defaultValue).
	bool default_checked = false; // markup `checked` for form reset.
	HashMap<String, String> extra_attributes;

	// --- CSS author overrides (layered over the UA defaults, like the cascade).
	// Each has a `has_*` flag; unset => the type's UA default is used. ---
	struct CssStyle {
		bool has_bg = false;
		Color bg = Color(1, 1, 1, 1);
		bool has_bw[4] = { false, false, false, false }; // border-width T,R,B,L.
		real_t bw[4] = { 0, 0, 0, 0 };
		bool has_bcol = false;
		Color bcol = Color(0, 0, 0, 1);
		bool border_none = false; // border-style: none.
		bool has_radius[4] = { false, false, false, false }; // TL,TR,BR,BL.
		real_t radius[4] = { 0, 0, 0, 0 };
		bool has_pad[4] = { false, false, false, false }; // padding T,R,B,L.
		real_t pad[4] = { 0, 0, 0, 0 };
		bool has_shadow = false;
		Color shadow_color = Color(0, 0, 0, 0);
		Vector2 shadow_offset;
		real_t shadow_blur = 0;
		real_t shadow_spread = 0;
		bool has_box_sizing = false;
		bool border_box = false; // box-sizing: border-box.
		bool has_width = false;
		real_t width = 0;
		bool has_height = false;
		real_t height = 0;
		// Typography.
		bool has_color = false;
		Color color = Color(0, 0, 0, 1);
		bool has_font_size = false;
		real_t font_size = 0;
		String font_family; // empty => unset.
		bool has_font_weight = false;
		int font_weight = 400;
		bool has_line_height = false;
		real_t line_height = 0; // px; <=0 => normal.
		bool has_text_align = false;
		int text_align = 0; // HorizontalAlignment.
		bool has_letter_spacing = false;
		real_t letter_spacing = 0;
	};
	CssStyle css;
	Ref<Font> css_font; // resolved FontVariation when typography is overridden.
	// What css_font was built from: an unchanged face is kept, so the editor
	// is not re-shaped with an identical new font.
	struct CssFontKey {
		Ref<Font> base;
		bool themed = false;
		String family;
		int weight = 400;
		int spacing = 0;
		bool operator==(const CssFontKey &p_other) const {
			return base == p_other.base && themed == p_other.themed && family == p_other.family && weight == p_other.weight && spacing == p_other.spacing;
		}
	} css_font_key;

	// Godot theme items (appear in the Theme editor). These are the user-agent
	// base that author CSS (set_css) layers on top of.
	struct ThemeCache {
		// Input typography.
		Ref<Font> font; // form-control font (empty => bundled Arial).
		int font_size = 0; // 0 => Chromium default 13.
		Color font_color = Color(0, 0, 0, 1);
		Color font_placeholder_color = Color(0.459, 0.459, 0.459, 1);
		Color selection_color = Color(0.6, 0.8, 1.0, 0.4);
		Color caret_color = Color(0, 0, 0, 1); // text caret (CSS caret-color).
		Color accent_color = Color(0.0, 0.47, 0.84, 1); // checked controls (CSS accent-color), default blue.
		int font_weight = 400;
		int letter_spacing = 0;
		int line_height = 0; // 0 => normal.
		int text_align = 0; // HorizontalAlignment.
		// Label typography (independent of the input text).
		Ref<Font> label_font;
		int label_font_size = 0;
		Color label_font_color = Color(0, 0, 0, 1);
		// Box.
		int box_sizing = 0; // 0 = content-box, 1 = border-box.
		// State styleboxes (drawn over the control by interaction state).
		Ref<StyleBox> field; // text field box.
		Ref<StyleBox> button; // button box.
		Ref<StyleBox> focus; // :focus ring.
		Ref<StyleBox> hover; // :hover overlay.
		Ref<StyleBox> pressed; // :active overlay.
		// type=image without a loaded image: the browser's broken-image icon.
		Ref<Texture2D> broken_image;
	} theme_cache;

	// Label (a wrapping HTML <label>) and its position relative to the input.
	String label_text;
	LabelPosition label_position = LABEL_LEFT;
	// Inline size the <label>'s line box may take (-1 = unbounded). When the
	// text and the control do not fit on one line, the browser breaks between
	// them, so a left/right label stacks like a top/bottom one.
	real_t label_wrap_width = -1;
	// The label box's max-width in px (-1 = none): a stacked label aligns its
	// lines in the clamped box, as `max-width:100%` does in narrow views.
	real_t label_max_width = -1;
	// The <label>'s own letter-spacing and text-align: an element, it inherits
	// them from the page, while the control inside resets them (UA) unless the
	// node styles them itself (css).
	real_t label_letter_spacing = 0;
	// The <label>'s own text-decoration (an absolutely positioned item gets
	// none from its ancestors). Lines: 1 underline, 2 overline, 4 line-through.
	// A transparent colour means currentColor; thickness <= 0 means auto.
	int label_decoration_lines = 0;
	Color label_decoration_color = Color(0, 0, 0, 0);
	int label_decoration_style = 0; // 0 solid, 1 double, 2 dotted, 3 dashed, 4 wavy.
	real_t label_decoration_thickness = 0;
	void _draw_label_decorations(const Ref<Font> &p_font, const Point2 &p_baseline) const;
	int label_alignment = -1; // HorizontalAlignment; -1 = follow the control's.
	int label_selection_from = 0;
	int label_selection_to = 0;
	CursorShape label_cursor_shape = CURSOR_ARROW;
	Ref<TextLine> _shape_label() const;

	// Resolved composite layout: where the label and the input box sit, and the
	// total node size (= input box size when there is no label).
	struct Layout {
		Size2 total;
		Point2 label_pos; // top-left of the label text.
		Point2 input_origin; // top-left of the input border-box within the node.
		bool has_label = false;
	};
	Layout _compute_layout(const BoxModel &p_box) const;

	// A `line-height: normal` line box: the union of every face the text
	// draws with (see _line_box in the .cpp).
	struct LineBox {
		real_t ascent = 0;
		real_t descent = 0;
		real_t height() const { return ascent + descent; }
	};
	LineBox _line_box(const Ref<Font> &p_font, const String &p_text, real_t p_font_size, bool p_with_gap = true) const;
	// The label face with the CSS letter-spacing the <label> carries.
	Ref<Font> _label_draw_font() const;
	mutable Ref<FontVariation> label_spacing_font;
	mutable Ref<Font> label_spacing_base;
	mutable int label_spacing_px = 0;
	LineBox _date_line_box() const; // the date/time editor's fields line.
	// Baseline of the control's own text, from the top of its border box.
	real_t _input_text_baseline(const BoxModel &p_box) const;
	String _ua_text(const char *p_key) const; // UA strings in the browser's UI language.
	// Draws one line of text at a fractional CSS font size (see the .cpp).
	void _draw_text_css(const Ref<Font> &p_font, const Point2 &p_baseline, const String &p_text, real_t p_css_size,
			const Color &p_color, real_t p_width = -1);
	Point2 draw_base; // the input box's offset inside the node while it is drawn.
	CursorShape _cursor_for_type() const;

	LineEdit *line_edit = nullptr; // embedded editor for text-family types.
	// Clipping wrapper around the editor. A LineEdit refuses to shrink below its
	// own minimum size, so an explicitly narrowed field would otherwise draw its
	// text past the box instead of clipping it the way the browser does.
	Control *editor_clip = nullptr;
	Ref<StyleBox> le_box; // the LineEdit's (empty) stylebox; carries content margins.
	// The editor's typography theme overrides as last written. Every override
	// re-shapes the editor, so they are written only when one of them changes.
	struct EditorTypography {
		Ref<Font> font;
		int font_size = 0;
		Color font_color;
		Color placeholder_color;
		Color selection_color;
		Color caret_color;
		bool operator==(const EditorTypography &p_other) const {
			return font == p_other.font && font_size == p_other.font_size && font_color == p_other.font_color && placeholder_color == p_other.placeholder_color && selection_color == p_other.selection_color && caret_color == p_other.caret_color;
		}
	} editor_typography;
	bool editor_typography_written = false;
	Ref<Font> ua_font; // Chromium UA default form font (Arial on Windows).
	Ref<Font> ua_mono_font; // monospace font for date/time fields.

public:
	// Which calendar/clock component a date/time part edits. FIELD_TEXT parts
	// are inert separators ("/", ":", " ", "年", "Week ").
	enum DateField {
		FIELD_TEXT,
		FIELD_YEAR,
		FIELD_MONTH, // numeric month.
		FIELD_MONTH_NAME, // localized month name ("March").
		FIELD_DAY,
		FIELD_WEEK,
		FIELD_HOUR12, // 1-12
		FIELD_HOUR24, // 0-23
		FIELD_MINUTE,
		FIELD_SECOND,
		FIELD_AMPM,
	};

private:
	// One laid-out piece of a date/time editor, mirroring Chromium's
	// -webkit-datetime-edit-*-field and -webkit-datetime-edit-text shadow
	// elements. An editable field paints its value (or placeholder) centred in a
	// box one pixel wider than the text on each side; a text part is a plain
	// separator drawn on the baseline.
	struct DatePart {
		DateField field = FIELD_TEXT;
		String text; // literal separator text (FIELD_TEXT only).
		String placeholder; // drawn while the field is unset.
		int value = -1; // -1 = unset; AM/PM uses 0 = am, 1 = pm.
		real_t x = 0; // offset from the content box's left edge.
		real_t width = 0; // laid-out width (current text + 2px padding).
	};
	Vector<DatePart> date_parts;
	// Cached max-content width of the editor: recomputing it means measuring
	// every field's widest possible text (all twelve month names included), so
	// it is invalidated on layout/font changes rather than measured per draw.
	mutable real_t date_edit_width_cache = -1;
	mutable real_t date_line_height_cache = -1;
	int focused_part = -1; // index into date_parts; -1 = nothing focused.
	int part_typed = 0; // digits already typed into the focused field.
	String date_locale; // author override; empty => follow the OS locale.
	// Picker drop-down (created lazily on first use).
	Popup *picker_popup = nullptr;
	WebInputPickerPanel *picker_panel = nullptr;
	bool hovered = false;
	bool pressed = false;
	// Chromium only paints the focus ring when the focus is "visible": always
	// for text-entry fields, but for buttons, range and colour only when the
	// focus arrived from the keyboard rather than a click.
	bool focus_visible = false;
	bool focus_from_mouse = false;
	int hovered_spin = 0; // type=number spinner: 1 up, -1 down, 0 neither.
	bool hovered_file_btn = false; // pointer over type=file's "Choose File".
	// Colour drop-down (created lazily on first use).
	bool external_color_picker_enabled = false;
	PopupPanel *color_popup = nullptr;
	ColorPicker *color_picker = nullptr;

	BoxModel _compute_box_model() const;
	void _apply_css_overrides(BoxModel &p_box) const; // merge css onto UA defaults.
	void _rebuild_css_font(); // build resolved FontVariation when typography set.
	bool _apply_css_declaration(const String &p_property, const String &p_value); // false: unsupported.
	bool _apply_css_text(const String &p_css); // true when any declaration applied.
	void _css_changed(); // one font rebuild + editor sync for a batch of declarations.
	Ref<Font> _resolved_font() const; // css_font, else ua_font.
	Ref<StyleBoxFlat> _make_style_box(const BoxModel &p_box) const;
	void _update_type();
	void _sync_line_edit();
	void _draw_box(const BoxModel &p_box);
	void _draw_text_content(const BoxModel &p_box);
	void _draw_button(const BoxModel &p_box);
	void _draw_checkbox(const BoxModel &p_box, bool p_radio);
	void _draw_range(const BoxModel &p_box);
	void _draw_color(const BoxModel &p_box);
	// Right-hand spinner of a type=number field, which Chromium only paints
	// while the pointer is over the control.
	Rect2 _spin_rect(const BoxModel &p_box) const;
	// Space type=search keeps at its right edge for the clear button, which
	// Chromium reserves whether or not the button is currently visible.
	real_t _search_clear_width(const BoxModel &p_box) const;
	void _draw_search_clear(const BoxModel &p_box);
	void _draw_number_spinner(const BoxModel &p_box);
	void _step_number(int p_dir);
	void _open_color_picker();
	void _on_color_picked(const Color &p_color);
	// Native control face/border for the current interaction state.
	Color _state_face_color() const;
	Color _state_border_color() const;
	// Replaces a value the new type cannot represent with that type's default.
	void _coerce_value_for_type();
	void _draw_datetime(const BoxModel &p_box);
	void _draw_file(const BoxModel &p_box);
	void _draw_image(const BoxModel &p_box);
	void _load_src_texture();
	// Width of type=file's "Choose File" button, which is also its hit area.
	real_t _file_button_width(const BoxModel &p_box) const;
	void _open_file_dialog(FileTarget p_target);
	void _on_files_chosen(const PackedStringArray &p_files);
	void _on_file_chosen(const String &p_file); // single-selection signal.
	String _files_label() const;
	bool _is_datetime_family() const;
	bool _is_calendar_family() const; // date/month/week/datetime-local (calendar icon).
	// Whether Chromium's UA stylesheet gives this type box-sizing: border-box,
	// which decides how an explicit width/height maps onto the box.
	bool _ua_border_box() const;
	// Resolved BCP-47-ish locale tag ("en_US") the date/time editor formats with.
	String _resolved_date_locale() const;
	// Builds date_parts from the locale's pattern for the current type, then
	// lays them out (x/width) with the current font.
	void _build_date_parts();
	void _layout_date_parts();
	Ref<Font> _date_font() const;
	real_t _date_font_size() const; // resolved CSS/theme font-size for the editor.
	// Advance width of `p_text` at a fractional font size. Font::draw_string
	// only takes integer sizes, but Chromium lays these editors out at
	// 13.3333px, so measurements go through a large reference size and scale.
	real_t _date_text_width(const Ref<Font> &p_font, const String &p_text, real_t p_font_size) const;
	real_t _date_line_height() const; // height of the editor's fields line box.
	// Width of the widest text a field can ever show; drives the control's
	// intrinsic width the way Chromium's max-content pass does.
	real_t _date_part_max_width(const DatePart &p_part, const Ref<Font> &p_font, real_t p_font_size) const;
	String _date_part_text(const DatePart &p_part) const; // value, else placeholder.
	// Total width of the laid-out parts plus Chromium's trailing edit slack.
	real_t _date_edit_width() const;
	real_t _date_indicator_size() const; // picker indicator box (square).
	int _date_part_at(const Point2 &p_pos) const; // hit-test, -1 = none.
	void _focus_date_part(int p_index, int p_dir); // snaps onto an editable part.
	bool _date_key_input(const Ref<InputEventKey> &p_key);
	void _date_commit();
	String _date_iso_value() const;
	void _date_parts_from_value(); // parse `value` back into the fields.
	void _date_step_focused(int p_delta);
	void _draw_picker_indicator(const BoxModel &p_box);
	void _open_picker();
	void _on_picker_value(const String &p_value);
	bool _is_text_family() const;
	Color _accent_color() const;
	static Ref<Font> _make_ua_font(const String &p_path, const String &p_system_name);
	static Ref<Font> _shared_ua_font(int p_slot, const String &p_path, const String &p_system_name);
	// Numeric helpers (number / range).
	double _parse_num(const String &p_s, double p_def) const;
	double _range_min() const;
	double _range_max() const;
	double _range_step() const;
	double _range_value() const; // current value, defaulting to the midpoint.
	void _set_range_from_pos(real_t p_x);
	void _enforce_radio_group();
	bool _activation_key_input(const Ref<InputEventKey> &p_key);
	void _activate_from_key();
	void _navigate_radio_siblings(int p_direction);
	String _default_value_for_type() const;
	String _display_text() const;
	Ref<Font> _get_font() const;
	Ref<Font> _label_font() const; // wrapping <label> text font.
	// Number field sized to its min/max/step digits (Chromium); false = use size.
	bool _number_preferred_size(int &r_chars) const;
	// Chromium's intrinsic content box of a text-family field at a font size.
	Size2 _text_field_content_size(real_t p_font_size) const;
	String _image_fallback_text() const; // type=image without a loaded image.
	real_t _label_font_size() const;

	void _on_line_edit_changed(const String &p_text);
	void _on_line_edit_submitted(const String &p_text);
	void _on_line_edit_gui_input(const Ref<InputEvent> &p_event);

protected:
	void _notification(int p_what);
	virtual void gui_input(const Ref<InputEvent> &p_event) override;
	static void _bind_methods();

public:
	virtual Size2 get_minimum_size() const override;

	void set_input_type(Type p_type);
	Type get_input_type() const;

	// Harness/bridge contract: set type from the HTML string ("datetime-local").
	void set_input_type_string(const String &p_type);
	String get_input_type_string() const;

	// Harness/bridge contract: generic attribute setter (HTML names with '-').
	void set_html_attribute(const String &p_name, const Variant &p_value);
	Variant get_html_attribute(const String &p_name) const;

	// CSS author styling (layered over UA defaults, like the cascade).
	void set_css(const String &p_property, const String &p_value);
	void set_css_text(const String &p_css); // parse an inline-style string.
	void replace_css(const String &p_css); // clear_css + set_css_text, refreshed once.
	void clear_css();

	// CSS value parsers (public/static so the inline-style expansion helpers and
	// tooling can reuse them).
	static bool _parse_css_length(const String &p_s, real_t &r_out);
	static bool _parse_css_color(const String &p_s, Color &r_out);
	static int _parse_css_font_weight(const String &p_s);

	// Harness/bridge contract: resolved box model for compare.py.
	Dictionary get_test_metrics() const;

	void set_external_color_picker_enabled(bool p_enabled);
	bool is_external_color_picker_enabled() const;
	void apply_external_color(const Color &p_color);
	void set_value(const String &p_value);
	String get_value() const;
	void set_placeholder(const String &p_placeholder);
	String get_placeholder() const;
	void set_input_name(const String &p_name);
	String get_input_name() const;
	void set_disabled(bool p_disabled);
	bool is_disabled() const;
	void set_draw_box(bool p_draw);
	bool is_drawing_box() const;
	void set_readonly(bool p_readonly);
	bool is_readonly() const;
	void set_required(bool p_required);
	bool is_required() const;
	void set_checked(bool p_checked);
	bool is_checked() const;
	void set_size_chars(int p_size);
	int get_size_chars() const;
	void set_input_width(int p_width);
	int get_input_width() const;
	void set_input_height(int p_height);
	int get_input_height() const;
	void set_maxlength(int p_maxlength);
	int get_maxlength() const;
	void set_min_value(int p_min);
	int get_min_value() const;
	void set_max_value(int p_max);
	int get_max_value() const;
	void set_step_value(int p_step);
	int get_step_value() const;
	void set_pattern(const String &p_pattern);
	String get_pattern() const;
	void set_multiple(bool p_multiple);
	bool is_multiple() const;
	void set_src(const String &p_src);
	String get_src() const;
	void set_alt(const String &p_alt);
	String get_alt() const;
	void set_label(const String &p_label);
	String get_label() const;
	// Document selection in the wrapping label, independent of the input editor.
	Rect2 get_label_text_rect() const;
	int get_label_column_at_pos(const Point2 &p_position) const;
	Point2 get_label_caret_position(int p_column) const;
	void set_label_selection(int p_from, int p_to);
	String get_selected_label_text() const;
	void activate_label();
	void set_label_cursor_shape(CursorShape p_shape);
	CursorShape get_label_cursor_shape() const;
	virtual CursorShape get_cursor_shape(const Point2 &p_position = Point2()) const override;
	// Locale the date/time editor formats with (e.g. "en_US", "zh_TW"). Empty
	// follows the OS locale, matching a browser's UI language.
	void set_date_locale(const String &p_locale);
	String get_date_locale() const;
	// Opens the calendar / clock drop-down, like clicking the picker indicator.
	void show_picker();
	void set_label_position(LabelPosition p_position);
	LabelPosition get_label_position() const;
	void set_label_wrap_width(real_t p_width);
	real_t get_label_wrap_width() const;
	void set_label_max_width(real_t p_width);
	real_t get_label_max_width() const;
	void set_label_letter_spacing(real_t p_spacing);
	real_t get_label_letter_spacing() const;
	void set_label_decoration_lines(int p_lines);
	int get_label_decoration_lines() const;
	void set_label_decoration_color(const Color &p_color);
	Color get_label_decoration_color() const;
	void set_label_decoration_style(int p_style);
	int get_label_decoration_style() const;
	void set_label_decoration_thickness(real_t p_thickness);
	real_t get_label_decoration_thickness() const;
	void set_label_alignment(int p_alignment);
	int get_label_alignment() const;
	void set_image_width(int p_w);
	int get_image_width() const;
	void set_image_height(int p_h);
	int get_image_height() const;

	void set_accept(const String &p_accept);
	String get_accept() const;
	// File model: programmatic selection (also used by tests) + accessor.
	void set_selected_files(const PackedStringArray &p_files);
	PackedStringArray get_selected_files() const;

	// Form model (submit/reset operate on sibling WebInputs).
	void reset_form();
	Dictionary submit_form();

	// Automation/testing helper: inject a key into a date/time field.
	void send_date_key(int p_keycode);
	// Automation/testing helper: type text into a text-family field character by
	// character (exercising input filtering, e.g. type=number).
	void simulate_type(const String &p_text);

	// Validity (HTML constraint validation, subset).
	bool check_validity() const;

	// Filters a string to the characters allowed in a type=number field (static
	// so it is unit-testable without a text server).
	static String filter_number_text(const String &p_text);

	WebInput();
};

VARIANT_ENUM_CAST(WebInput::Type);
VARIANT_ENUM_CAST(WebInput::LabelPosition);
VARIANT_ENUM_CAST(WebInput::DateField);
