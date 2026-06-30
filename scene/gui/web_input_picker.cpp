/**************************************************************************/
/*  web_input_picker.cpp                                                  */
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

#include "web_input_picker.h"

#include "core/input/input_event.h"
#include "core/object/class_db.h"
#include "core/os/time.h"
#include "scene/resources/font.h"

// Chromium's light-mode picker palette.
static const Color PICKER_BG = Color(1, 1, 1, 1);
static const Color PICKER_BORDER = Color(0, 0, 0, 0.28);
static const Color PICKER_TEXT = Color(0.05, 0.05, 0.05, 1);
static const Color PICKER_MUTED = Color(0.55, 0.55, 0.55, 1);
static const Color PICKER_ACCENT = Color(0.0, 120.0 / 255.0, 215.0 / 255.0, 1);
static const Color PICKER_HOVER = Color(0.93, 0.93, 0.93, 1);

// ---------------------------------------------------------------------------
// Proleptic Gregorian date helpers (days-from-civil, Howard Hinnant).
// ---------------------------------------------------------------------------
static bool is_leap_year(int y) {
	return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

static int days_in_month(int y, int m) {
	static const int len[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	if (m < 1 || m > 12) {
		return 30;
	}
	if (m == 2 && is_leap_year(y)) {
		return 29;
	}
	return len[m - 1];
}

static int64_t days_from_civil(int y, int m, int d) {
	y -= m <= 2;
	const int64_t era = (y >= 0 ? y : y - 399) / 400;
	const int64_t yoe = y - era * 400;
	const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}

static void civil_from_days(int64_t z, int &r_y, int &r_m, int &r_d) {
	z += 719468;
	const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
	const int64_t doe = z - era * 146097;
	const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	const int64_t y = yoe + era * 400;
	const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	const int64_t mp = (5 * doy + 2) / 153;
	const int64_t d = doy - (153 * mp + 2) / 5 + 1;
	const int64_t m = mp + (mp < 10 ? 3 : -9);
	r_y = (int)(y + (m <= 2));
	r_m = (int)m;
	r_d = (int)d;
}

// 0 = Sunday .. 6 = Saturday.
static int weekday_of(int y, int m, int d) {
	int64_t z = days_from_civil(y, m, d);
	// 1970-01-01 was a Thursday (4).
	return (int)(((z % 7) + 11) % 7);
}

// Monday of ISO week `w` of week-numbering year `y`.
static int64_t iso_week_monday(int y, int w) {
	int64_t jan4 = days_from_civil(y, 1, 4);
	int dow = weekday_of(y, 1, 4); // 0 = Sunday.
	int iso_dow = (dow == 0) ? 7 : dow; // 1 = Monday .. 7 = Sunday.
	return jan4 - (iso_dow - 1) + (int64_t)(w - 1) * 7;
}

static void iso_week_of(int y, int m, int d, int &r_year, int &r_week) {
	int64_t day = days_from_civil(y, m, d);
	int dow = weekday_of(y, m, d);
	int iso_dow = (dow == 0) ? 7 : dow;
	int64_t thursday = day + (4 - iso_dow); // Thursday of this week decides the year.
	int ty, tm, td;
	civil_from_days(thursday, ty, tm, td);
	r_year = ty;
	r_week = (int)((thursday - iso_week_monday(ty, 1)) / 7) + 1;
}

static String pad2(int v) {
	return String::num_int64(v).pad_zeros(2);
}

static String pad4(int v) {
	return String::num_int64(v).pad_zeros(4);
}

// ---------------------------------------------------------------------------
WebInputPickerPanel::WebInputPickerPanel() {
	set_focus_mode(FOCUS_NONE);
	set_mouse_filter(MOUSE_FILTER_STOP);
}

void WebInputPickerPanel::set_picker_font(const Ref<Font> &p_font, int p_font_size) {
	font = p_font;
	font_size = p_font_size;
	queue_redraw();
}

void WebInputPickerPanel::setup(Mode p_mode, const String &p_value, bool p_hour24, int p_step_minutes,
		const Vector<String> &p_month_names, const Vector<String> &p_weekday_names,
		const Vector<String> &p_ampm_names, int p_first_weekday) {
	mode = p_mode;
	hour24 = p_hour24;
	step_minutes = CLAMP(p_step_minutes, 1, 720);
	month_names = p_month_names;
	weekday_names = p_weekday_names;
	ampm_names = p_ampm_names;
	// ISO weeks always start on Monday, so the week grid does too; otherwise the
	// highlighted row would not line up with the week being selected.
	first_weekday = (p_mode == MODE_WEEK) ? 1 : (p_first_weekday ? 1 : 0);

	Dictionary now = Time::get_singleton()->get_datetime_dict_from_system(false);
	sel_year = (int)now["year"];
	sel_month = (int)now["month"];
	sel_day = 0;
	sel_week = 0;
	sel_minutes = -1;

	// Parse whichever HTML value shape this mode uses. Anything unparseable
	// leaves the picker on today's page with no selection, like the browser.
	String v = p_value.strip_edges();
	String date_part = v;
	String time_part;
	int t_at = v.find("T");
	if (t_at >= 0) {
		date_part = v.substr(0, t_at);
		time_part = v.substr(t_at + 1);
	} else if (mode == MODE_TIME) {
		date_part = String();
		time_part = v;
	}
	if (!date_part.is_empty()) {
		Vector<String> bits = date_part.split("-");
		if (bits.size() >= 1 && bits[0].is_valid_int()) {
			sel_year = (int)bits[0].to_int();
		}
		if (bits.size() >= 2) {
			if (bits[1].begins_with("W")) {
				sel_week = (int)bits[1].substr(1).to_int();
				int wy, wm, wd;
				civil_from_days(iso_week_monday(sel_year, MAX(1, sel_week)), wy, wm, wd);
				sel_month = wm;
				sel_day = wd;
			} else if (bits[1].is_valid_int()) {
				sel_month = CLAMP((int)bits[1].to_int(), 1, 12);
			}
		}
		if (bits.size() >= 3 && bits[2].is_valid_int()) {
			sel_day = CLAMP((int)bits[2].to_int(), 1, days_in_month(sel_year, sel_month));
		}
	}
	if (!time_part.is_empty()) {
		Vector<String> hm = time_part.split(":");
		if (hm.size() >= 2 && hm[0].is_valid_int() && hm[1].is_valid_int()) {
			sel_minutes = CLAMP((int)hm[0].to_int(), 0, 23) * 60 + CLAMP((int)hm[1].to_int(), 0, 59);
		}
	}
	page_year = sel_year;
	page_month = sel_month;
	view = (mode == MODE_MONTH) ? VIEW_MONTHS : VIEW_DAYS;
	year_page_base = page_year - YEAR_CELLS / 2;
	hot_year_label = false;

	// Scroll the time list so the current value is visible.
	time_scroll = 0.0;
	if (sel_minutes >= 0 && (mode == MODE_TIME || mode == MODE_DATETIME)) {
		int row = sel_minutes / step_minutes;
		real_t max_scroll = MAX((real_t)0.0, (real_t)((_time_row_count() - TIME_VISIBLE_ROWS) * TIME_ROW_H));
		time_scroll = CLAMP((real_t)((row - TIME_VISIBLE_ROWS / 2) * TIME_ROW_H), (real_t)0.0, max_scroll);
	}
	set_size(get_panel_size());
	_update_layout();
	queue_redraw();
}

Size2 WebInputPickerPanel::get_panel_size() const {
	real_t grid_w = (real_t)CELL_W * GRID_COLS;
	switch (mode) {
		case MODE_TIME:
			return Size2(TIME_W + PAD * 2, (real_t)TIME_ROW_H * TIME_VISIBLE_ROWS + PAD * 2);
		case MODE_MONTH:
			return Size2(grid_w + PAD * 2, HEADER_H + (real_t)CELL_H * 4 + PAD * 2);
		case MODE_DATETIME:
			return Size2(grid_w + TIME_W + PAD * 3, HEADER_H + WEEKDAY_H + (real_t)CELL_H * GRID_ROWS + PAD * 2);
		default: // MODE_DATE / MODE_WEEK
			return Size2(grid_w + PAD * 2, HEADER_H + WEEKDAY_H + (real_t)CELL_H * GRID_ROWS + PAD * 2);
	}
}

void WebInputPickerPanel::_update_layout() {
	// Fall back to the intrinsic size while the panel has not been laid out yet
	// (the header's navigation buttons are positioned from the right edge, so a
	// zero size would push them off the panel).
	Size2 sz = get_size();
	if (sz.x <= 0 || sz.y <= 0) {
		sz = get_panel_size();
	}
	real_t grid_w = (real_t)CELL_W * GRID_COLS;
	header_rect = Rect2(PAD, PAD, sz.x - PAD * 2, HEADER_H);
	// Navigation chevrons sit at the right end of the header.
	prev_rect = Rect2(header_rect.position.x + header_rect.size.x - 56, header_rect.position.y, 24, HEADER_H);
	next_rect = Rect2(header_rect.position.x + header_rect.size.x - 26, header_rect.position.y, 24, HEADER_H);
	switch (mode) {
		case MODE_TIME: {
			header_rect = Rect2();
			prev_rect = Rect2();
			next_rect = Rect2();
			time_rect = Rect2(PAD, PAD, sz.x - PAD * 2, sz.y - PAD * 2);
			grid_rect = Rect2();
		} break;
		case MODE_MONTH: {
			grid_rect = Rect2(PAD, PAD + HEADER_H, grid_w, (real_t)CELL_H * 4);
			time_rect = Rect2();
		} break;
		case MODE_DATETIME: {
			header_rect.size.x = grid_w;
			prev_rect.position.x = PAD + grid_w - 56;
			next_rect.position.x = PAD + grid_w - 26;
			grid_rect = Rect2(PAD, PAD + HEADER_H + WEEKDAY_H, grid_w, (real_t)CELL_H * GRID_ROWS);
			time_rect = Rect2(PAD * 2 + grid_w, PAD, TIME_W, sz.y - PAD * 2);
		} break;
		default: {
			grid_rect = Rect2(PAD, PAD + HEADER_H + WEEKDAY_H, grid_w, (real_t)CELL_H * GRID_ROWS);
			time_rect = Rect2();
		} break;
	}
}

Rect2 WebInputPickerPanel::_year_grid_rect() const {
	// The year list has no weekday strip, so it reclaims that space.
	if (mode == MODE_MONTH) {
		return grid_rect;
	}
	return Rect2(grid_rect.position - Vector2(0, WEEKDAY_H), grid_rect.size + Size2(0, WEEKDAY_H));
}

String WebInputPickerPanel::_page_month_name() const {
	if (page_month >= 1 && page_month <= month_names.size()) {
		return month_names[page_month - 1];
	}
	return pad2(page_month);
}

Rect2 WebInputPickerPanel::_year_label_rect() const {
	// The year in the header doubles as the button that opens the year list.
	if (header_rect.size == Size2() || font.is_null() || view == VIEW_YEARS) {
		return Rect2();
	}
	real_t x = header_rect.position.x + 2;
	if (mode != MODE_MONTH) {
		x += font->get_string_size(_page_month_name() + " ", HORIZONTAL_ALIGNMENT_LEFT, -1, font_size).x;
	}
	Size2 ys = font->get_string_size(String::num_int64(page_year), HORIZONTAL_ALIGNMENT_LEFT, -1, font_size);
	// Room for the text plus the little caret drawn after it.
	return Rect2(x - 3, header_rect.position.y + 4, ys.x + 16, header_rect.size.y - 8);
}

int WebInputPickerPanel::_leading_blanks() const {
	int wd = weekday_of(page_year, page_month, 1); // 0 = Sunday.
	return (wd - first_weekday + 7) % 7;
}

int WebInputPickerPanel::_grid_cell_at(const Point2 &p_pos) const {
	if (view == VIEW_YEARS) {
		Rect2 area = _year_grid_rect();
		if (!area.has_point(p_pos)) {
			return -1;
		}
		int col = (int)((p_pos.x - area.position.x) / (area.size.x / YEAR_COLS));
		int row = (int)((p_pos.y - area.position.y) / (area.size.y / YEAR_ROWS));
		return CLAMP(row, 0, YEAR_ROWS - 1) * YEAR_COLS + CLAMP(col, 0, YEAR_COLS - 1);
	}
	if (!grid_rect.has_point(p_pos)) {
		return -1;
	}
	int col = (int)((p_pos.x - grid_rect.position.x) / CELL_W);
	int row = (int)((p_pos.y - grid_rect.position.y) / CELL_H);
	if (mode == MODE_MONTH) {
		// The 12 months are laid out 3 per row across the 7-column grid width,
		// so each month cell spans a bit more than two columns.
		col = (int)((p_pos.x - grid_rect.position.x) / (grid_rect.size.x / 3));
		return CLAMP(row, 0, 3) * 3 + CLAMP(col, 0, 2);
	}
	if (col < 0 || col >= GRID_COLS || row < 0 || row >= GRID_ROWS) {
		return -1;
	}
	return row * GRID_COLS + col;
}

int WebInputPickerPanel::_time_row_count() const {
	return (24 * 60) / step_minutes;
}

int WebInputPickerPanel::_time_for_row(int p_row) const {
	return p_row * step_minutes;
}

int WebInputPickerPanel::_time_row_at(const Point2 &p_pos) const {
	if (time_rect.size == Size2() || !time_rect.has_point(p_pos)) {
		return -1;
	}
	int rows = _time_row_count();
	int visible_rows = (int)(time_rect.size.y / TIME_ROW_H);
	int first = CLAMP((int)(time_scroll / TIME_ROW_H), 0, MAX(0, rows - visible_rows));
	int row = first + (int)((p_pos.y - time_rect.position.y) / TIME_ROW_H);
	if (row < 0 || row >= rows) {
		return -1;
	}
	return row;
}

void WebInputPickerPanel::_step_page(int p_delta) {
	if (view == VIEW_YEARS) {
		year_page_base += p_delta * YEAR_CELLS;
		queue_redraw();
		return;
	}
	if (mode == MODE_MONTH) {
		page_year += p_delta;
	} else {
		page_month += p_delta;
		while (page_month > 12) {
			page_month -= 12;
			page_year++;
		}
		while (page_month < 1) {
			page_month += 12;
			page_year--;
		}
	}
	queue_redraw();
}

void WebInputPickerPanel::_emit_current() {
	String out;
	switch (mode) {
		case MODE_DATE: {
			if (sel_day <= 0) {
				return;
			}
			out = pad4(sel_year) + "-" + pad2(sel_month) + "-" + pad2(sel_day);
		} break;
		case MODE_MONTH: {
			out = pad4(sel_year) + "-" + pad2(sel_month);
		} break;
		case MODE_WEEK: {
			if (sel_week <= 0) {
				return;
			}
			out = pad4(sel_year) + "-W" + pad2(sel_week);
		} break;
		case MODE_TIME: {
			if (sel_minutes < 0) {
				return;
			}
			out = pad2(sel_minutes / 60) + ":" + pad2(sel_minutes % 60);
		} break;
		case MODE_DATETIME: {
			if (sel_day <= 0) {
				return;
			}
			int mins = sel_minutes < 0 ? 0 : sel_minutes;
			out = pad4(sel_year) + "-" + pad2(sel_month) + "-" + pad2(sel_day) +
					"T" + pad2(mins / 60) + ":" + pad2(mins % 60);
		} break;
	}
	emit_signal(SNAME("value_picked"), out);
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
void WebInputPickerPanel::_draw_header() {
	if (header_rect.size == Size2() || font.is_null()) {
		return;
	}
	RID ci = get_canvas_item();
	real_t base_y = header_rect.position.y +
			(header_rect.size.y + font->get_ascent(font_size) - font->get_descent(font_size)) * 0.5;
	real_t x = header_rect.position.x + 2;

	if (view == VIEW_YEARS) {
		String range = String::num_int64(year_page_base) + " - " + String::num_int64(year_page_base + YEAR_CELLS - 1);
		font->draw_string(ci, Point2(x, base_y), range, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, PICKER_TEXT);
	} else {
		if (mode != MODE_MONTH) {
			String lead = _page_month_name() + " ";
			font->draw_string(ci, Point2(x, base_y), lead, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, PICKER_TEXT);
			x += font->get_string_size(lead, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size).x;
		}
		Rect2 yr = _year_label_rect();
		if (hot_year_label) {
			draw_rect(yr, PICKER_HOVER, true);
		}
		String ys = String::num_int64(page_year);
		font->draw_string(ci, Point2(x, base_y), ys, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, PICKER_TEXT);
		// Down caret marking the year as the button that opens the year list.
		real_t cx = x + font->get_string_size(ys, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size).x + 5;
		real_t cy = header_rect.position.y + header_rect.size.y * 0.5 + 1;
		Vector<Point2> caret;
		caret.push_back(Point2(cx - 3, cy - 1.5));
		caret.push_back(Point2(cx + 3, cy - 1.5));
		caret.push_back(Point2(cx, cy + 2));
		draw_colored_polygon(caret, PICKER_TEXT);
	}

	// Chevrons, drawn as two strokes so they stay crisp at any size.
	for (int i = 0; i < 2; i++) {
		Rect2 r = i == 0 ? prev_rect : next_rect;
		bool hot = (hot_nav == (i == 0 ? -1 : 1));
		if (hot) {
			draw_rect(r, PICKER_HOVER, true);
		}
		Point2 c = r.position + r.size * 0.5;
		real_t d = 3.5;
		real_t dir = i == 0 ? -1.0 : 1.0;
		draw_line(c + Point2(-dir * d * 0.5, -d), c + Point2(dir * d * 0.5, 0), PICKER_TEXT, 1.5, true);
		draw_line(c + Point2(dir * d * 0.5, 0), c + Point2(-dir * d * 0.5, d), PICKER_TEXT, 1.5, true);
	}
}

void WebInputPickerPanel::_draw_calendar() {
	if (font.is_null()) {
		return;
	}
	// Weekday initials.
	real_t wy = grid_rect.position.y - WEEKDAY_H;
	for (int i = 0; i < GRID_COLS; i++) {
		int wd = (i + first_weekday) % 7;
		String s = wd < weekday_names.size() ? weekday_names[wd] : String();
		Size2 ts = font->get_string_size(s, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size);
		Point2 p(grid_rect.position.x + CELL_W * i + (CELL_W - ts.x) * 0.5,
				wy + (WEEKDAY_H + font->get_ascent(font_size) - font->get_descent(font_size)) * 0.5);
		font->draw_string(get_canvas_item(), p, s, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, PICKER_MUTED);
	}

	Dictionary now = Time::get_singleton()->get_datetime_dict_from_system(false);
	int ty = (int)now["year"], tm = (int)now["month"], td = (int)now["day"];

	int blanks = _leading_blanks();
	int dim = days_in_month(page_year, page_month);
	int sel_row = -1;
	if (mode == MODE_WEEK && sel_week > 0) {
		// Highlight the whole row that holds the selected ISO week.
		for (int cell = 0; cell < GRID_ROWS * GRID_COLS; cell++) {
			int day = cell - blanks + 1;
			if (day < 1 || day > dim) {
				continue;
			}
			int wy2, ww;
			iso_week_of(page_year, page_month, day, wy2, ww);
			if (wy2 == sel_year && ww == sel_week) {
				sel_row = cell / GRID_COLS;
				break;
			}
		}
	}

	for (int cell = 0; cell < GRID_ROWS * GRID_COLS; cell++) {
		int day = cell - blanks + 1;
		int row = cell / GRID_COLS;
		int col = cell % GRID_COLS;
		Rect2 cr(grid_rect.position.x + CELL_W * col, grid_rect.position.y + CELL_H * row, CELL_W, CELL_H);
		if (day < 1 || day > dim) {
			continue;
		}
		bool selected = (mode == MODE_WEEK) ? (row == sel_row)
											: (sel_day == day && sel_year == page_year && sel_month == page_month);
		bool hot = (mode == MODE_WEEK) ? (hot_cell >= 0 && hot_cell / GRID_COLS == row) : (hot_cell == cell);
		bool today = (ty == page_year && tm == page_month && td == day);
		if (selected) {
			draw_rect(cr, PICKER_ACCENT, true);
		} else if (hot) {
			draw_rect(cr, PICKER_HOVER, true);
		}
		if (today && !selected) {
			draw_rect(cr, PICKER_ACCENT, false, 1.0);
		}
		String s = String::num_int64(day);
		Size2 ts = font->get_string_size(s, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size);
		Point2 p(cr.position.x + (cr.size.x - ts.x) * 0.5,
				cr.position.y + (cr.size.y + font->get_ascent(font_size) - font->get_descent(font_size)) * 0.5);
		font->draw_string(get_canvas_item(), p, s, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size,
				selected ? Color(1, 1, 1) : PICKER_TEXT);
	}
}

void WebInputPickerPanel::_draw_month_grid() {
	if (font.is_null()) {
		return;
	}
	real_t cw = grid_rect.size.x / 3;
	for (int i = 0; i < 12; i++) {
		Rect2 cr(grid_rect.position.x + cw * (i % 3), grid_rect.position.y + CELL_H * (i / 3), cw, CELL_H);
		bool selected = (sel_month == i + 1 && sel_year == page_year);
		if (selected) {
			draw_rect(cr, PICKER_ACCENT, true);
		} else if (hot_cell == i) {
			draw_rect(cr, PICKER_HOVER, true);
		}
		String s = i < month_names.size() ? month_names[i] : pad2(i + 1);
		Size2 ts = font->get_string_size(s, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size);
		Point2 p(cr.position.x + (cr.size.x - ts.x) * 0.5,
				cr.position.y + (cr.size.y + font->get_ascent(font_size) - font->get_descent(font_size)) * 0.5);
		font->draw_string(get_canvas_item(), p, s, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size,
				selected ? Color(1, 1, 1) : PICKER_TEXT);
	}
}

void WebInputPickerPanel::_draw_year_grid() {
	if (font.is_null()) {
		return;
	}
	Rect2 area = _year_grid_rect();
	real_t cw = area.size.x / YEAR_COLS;
	real_t ch = area.size.y / YEAR_ROWS;
	for (int i = 0; i < YEAR_CELLS; i++) {
		int year = year_page_base + i;
		Rect2 cr(area.position.x + cw * (i % YEAR_COLS), area.position.y + ch * (i / YEAR_COLS), cw, ch);
		bool selected = (year == page_year);
		if (selected) {
			draw_rect(cr, PICKER_ACCENT, true);
		} else if (hot_cell == i) {
			draw_rect(cr, PICKER_HOVER, true);
		}
		String s = String::num_int64(year);
		Size2 ts = font->get_string_size(s, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size);
		Point2 p(cr.position.x + (cr.size.x - ts.x) * 0.5,
				cr.position.y + (cr.size.y + font->get_ascent(font_size) - font->get_descent(font_size)) * 0.5);
		font->draw_string(get_canvas_item(), p, s, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size,
				selected ? Color(1, 1, 1) : PICKER_TEXT);
	}
}

void WebInputPickerPanel::_draw_time_list() {
	if (time_rect.size == Size2() || font.is_null()) {
		return;
	}
	RID ci = get_canvas_item();
	int rows = _time_row_count();
	// `time_scroll` is snapped to whole rows, so the visible window always lines
	// up with the list rectangle and no clipping is needed.
	int visible_rows = (int)(time_rect.size.y / TIME_ROW_H);
	int first = CLAMP((int)(time_scroll / TIME_ROW_H), 0, MAX(0, rows - visible_rows));
	int last = MIN(rows - 1, first + visible_rows - 1);
	for (int r = first; r <= last; r++) {
		Rect2 rr(time_rect.position.x, time_rect.position.y + (r - first) * TIME_ROW_H, time_rect.size.x, TIME_ROW_H);
		int mins = _time_for_row(r);
		// A value that falls between two rows (13:45 in a half-hour list) marks
		// the row it belongs to, so the current time is always visible.
		bool selected = (sel_minutes >= mins && sel_minutes < mins + step_minutes);
		if (selected) {
			draw_rect(rr, PICKER_ACCENT, true);
		} else if (hot_time == r) {
			draw_rect(rr, PICKER_HOVER, true);
		}
		String s;
		if (hour24) {
			s = pad2(mins / 60) + ":" + pad2(mins % 60);
		} else {
			int h = mins / 60;
			int h12 = h % 12;
			if (h12 == 0) {
				h12 = 12;
			}
			String ap = (h < 12 ? (ampm_names.size() > 0 ? ampm_names[0] : String("AM"))
							    : (ampm_names.size() > 1 ? ampm_names[1] : String("PM")));
			s = pad2(h12) + ":" + pad2(mins % 60) + " " + ap;
		}
		Size2 ts = font->get_string_size(s, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size);
		Point2 p(rr.position.x + (rr.size.x - ts.x) * 0.5,
				rr.position.y + (rr.size.y + font->get_ascent(font_size) - font->get_descent(font_size)) * 0.5);
		font->draw_string(ci, p, s, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, selected ? Color(1, 1, 1) : PICKER_TEXT);
	}
}

void WebInputPickerPanel::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_RESIZED: {
			_update_layout();
		} break;
		case NOTIFICATION_DRAW: {
			draw_rect(Rect2(Point2(), get_size()), PICKER_BG, true);
			// Inset by half a pixel so the 1px stroke lands fully inside the
			// panel on every edge.
			draw_rect(Rect2(Point2(0.5, 0.5), get_size() - Size2(1, 1)), PICKER_BORDER, false, 1.0);
			_draw_header();
			if (view == VIEW_YEARS) {
				_draw_year_grid();
				if (mode == MODE_DATETIME) {
					_draw_time_list();
				}
			} else {
				switch (mode) {
					case MODE_MONTH:
						_draw_month_grid();
						break;
					case MODE_TIME:
						_draw_time_list();
						break;
					case MODE_DATETIME:
						_draw_calendar();
						_draw_time_list();
						break;
					default:
						_draw_calendar();
						break;
				}
			}
		} break;
	}
}

void WebInputPickerPanel::gui_input(const Ref<InputEvent> &p_event) {
	Ref<InputEventMouseMotion> mm = p_event;
	if (mm.is_valid()) {
		int cell = _grid_cell_at(mm->get_position());
		int trow = _time_row_at(mm->get_position());
		int nav = prev_rect.has_point(mm->get_position()) ? -1 : (next_rect.has_point(mm->get_position()) ? 1 : 0);
		Rect2 yr = _year_label_rect();
		bool hot_year = yr.size != Size2() && yr.has_point(mm->get_position());
		if (cell != hot_cell || trow != hot_time || nav != hot_nav || hot_year != hot_year_label) {
			hot_cell = cell;
			hot_time = trow;
			hot_nav = nav;
			hot_year_label = hot_year;
			queue_redraw();
		}
		return;
	}

	Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_null() || !mb->is_pressed()) {
		return;
	}
	if (mb->get_button_index() == MouseButton::WHEEL_UP || mb->get_button_index() == MouseButton::WHEEL_DOWN) {
		if (time_rect.size != Size2() && time_rect.has_point(mb->get_position())) {
			real_t max_scroll = MAX((real_t)0.0, (real_t)((_time_row_count() - TIME_VISIBLE_ROWS) * TIME_ROW_H));
			time_scroll = CLAMP(time_scroll + (real_t)(mb->get_button_index() == MouseButton::WHEEL_UP ? -TIME_ROW_H * 3 : TIME_ROW_H * 3), (real_t)0.0, max_scroll);
		} else {
			_step_page(mb->get_button_index() == MouseButton::WHEEL_UP ? -1 : 1);
		}
		queue_redraw();
		accept_event();
		return;
	}
	if (mb->get_button_index() != MouseButton::LEFT) {
		return;
	}

	Point2 pos = mb->get_position();
	if (prev_rect.has_point(pos)) {
		_step_page(-1);
		accept_event();
		return;
	}
	if (next_rect.has_point(pos)) {
		_step_page(1);
		accept_event();
		return;
	}

	// The year in the header opens the year list.
	Rect2 yr = _year_label_rect();
	if (yr.size != Size2() && yr.has_point(pos)) {
		view = VIEW_YEARS;
		year_page_base = page_year - YEAR_CELLS / 2;
		hot_cell = -1;
		hot_year_label = false;
		queue_redraw();
		accept_event();
		return;
	}

	int trow = _time_row_at(pos);
	if (trow >= 0) {
		sel_minutes = _time_for_row(trow);
		queue_redraw();
		_emit_current();
		accept_event();
		return;
	}

	int cell = _grid_cell_at(pos);
	if (cell < 0) {
		return;
	}
	if (view == VIEW_YEARS) {
		// Picking a year returns to the grid the picker opened on; the value is
		// only committed once a day (or month) is chosen there.
		page_year = year_page_base + cell;
		view = (mode == MODE_MONTH) ? VIEW_MONTHS : VIEW_DAYS;
		hot_cell = -1;
		queue_redraw();
		accept_event();
		return;
	}
	if (mode == MODE_MONTH) {
		sel_year = page_year;
		sel_month = cell + 1;
		queue_redraw();
		_emit_current();
		accept_event();
		return;
	}
	int day = cell - _leading_blanks() + 1;
	if (day < 1 || day > days_in_month(page_year, page_month)) {
		return;
	}
	if (mode == MODE_WEEK) {
		iso_week_of(page_year, page_month, day, sel_year, sel_week);
	} else {
		sel_year = page_year;
		sel_month = page_month;
		sel_day = day;
	}
	queue_redraw();
	_emit_current();
	accept_event();
}

void WebInputPickerPanel::_bind_methods() {
	ADD_SIGNAL(MethodInfo("value_picked", PropertyInfo(Variant::STRING, "value")));

	BIND_ENUM_CONSTANT(MODE_DATE);
	BIND_ENUM_CONSTANT(MODE_MONTH);
	BIND_ENUM_CONSTANT(MODE_WEEK);
	BIND_ENUM_CONSTANT(MODE_TIME);
	BIND_ENUM_CONSTANT(MODE_DATETIME);
}
