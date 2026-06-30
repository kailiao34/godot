/**************************************************************************/
/*  web_input_picker.h                                                    */
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

class Font;

// The drop-down body of a WebInput date/time picker, mirroring the popup
// Chromium opens from the calendar / clock indicator of an <input type=date>
// family field. WebInput parents one of these to a plain Popup; the panel does
// all of the drawing and hit-testing and reports the chosen value back as an
// HTML-format (ISO) string through the `value_picked` signal.
class WebInputPickerPanel : public Control {
	GDCLASS(WebInputPickerPanel, Control);

public:
	// Which page the panel is showing. The calendar and month grids can be
	// paged back to a list of years by clicking the year in the header, the way
	// the browser's picker does.
	enum View {
		VIEW_DAYS,
		VIEW_MONTHS,
		VIEW_YEARS,
	};

	enum Mode {
		MODE_DATE, // month grid, day granularity.
		MODE_MONTH, // 12-month grid.
		MODE_WEEK, // month grid, whole-week rows.
		MODE_TIME, // scrollable list of times.
		MODE_DATETIME, // month grid + time list side by side.
	};

	// Layout metrics, in the same CSS pixels WebInput uses.
	enum {
		PAD = 8,
		CELL_W = 32,
		CELL_H = 24,
		HEADER_H = 30,
		WEEKDAY_H = 20,
		GRID_ROWS = 6,
		GRID_COLS = 7,
		TIME_W = 84,
		TIME_ROW_H = 24,
		TIME_VISIBLE_ROWS = 8,
		YEAR_COLS = 3,
		YEAR_ROWS = 4,
		YEAR_CELLS = YEAR_COLS * YEAR_ROWS,
	};

private:
	Mode mode = MODE_DATE;

	// The value under edit, split into components. `year`/`month` also drive
	// which page the calendar shows, so they stay valid even when the field is
	// only partly filled.
	int sel_year = 0;
	int sel_month = 1; // 1-12
	int sel_day = 0; // 0 = no day selected yet.
	int sel_week = 0; // ISO week, week mode only.
	int sel_minutes = -1; // minutes since midnight, time modes only.
	int page_year = 0; // calendar page (may differ from the selection).
	int page_month = 1;

	bool hour24 = true;
	int step_minutes = 30; // granularity of the time list.
	int first_weekday = 0; // 0 = Sunday, 1 = Monday.

	Vector<String> month_names;
	Vector<String> weekday_names;
	Vector<String> ampm_names;

	Ref<Font> font;
	int font_size = 13;

	// Hover/press feedback. `hot_*` is the item under the pointer.
	int hot_cell = -1; // day/month grid index.
	int hot_nav = 0; // -1 previous page, +1 next page, 0 none.
	int hot_time = -1; // time list row.
	bool hot_year_label = false; // pointer over the header's year label.
	real_t time_scroll = 0.0;

	View view = VIEW_DAYS;
	int year_page_base = 0; // first year shown by the year grid.

	// Sub-rectangles of the panel, recomputed by _update_layout().
	Rect2 grid_rect;
	Rect2 header_rect;
	Rect2 prev_rect;
	Rect2 next_rect;
	Rect2 time_rect;

	void _update_layout();
	// Area the day/month/year grid is drawn in. The year grid also covers the
	// weekday-header strip, which it has no use for.
	Rect2 _year_grid_rect() const;
	// Clickable year label in the header (empty while the year list is open).
	Rect2 _year_label_rect() const;
	String _page_month_name() const;
	int _grid_cell_at(const Point2 &p_pos) const;
	int _time_row_at(const Point2 &p_pos) const;
	int _time_row_count() const;
	int _time_for_row(int p_row) const;
	// First grid column occupied by day 1 of the displayed page.
	int _leading_blanks() const;
	void _step_page(int p_delta);
	void _emit_current();
	void _draw_calendar();
	void _draw_month_grid();
	void _draw_year_grid();
	void _draw_time_list();
	void _draw_header();

protected:
	void _notification(int p_what);
	virtual void gui_input(const Ref<InputEvent> &p_event) override;
	static void _bind_methods();

public:
	// Configures the panel for one field. `p_value` is the input's HTML value
	// (e.g. "2024-03-07"); an empty string opens on today's page with nothing
	// selected.
	void setup(Mode p_mode, const String &p_value, bool p_hour24, int p_step_minutes,
			const Vector<String> &p_month_names, const Vector<String> &p_weekday_names,
			const Vector<String> &p_ampm_names, int p_first_weekday);
	void set_picker_font(const Ref<Font> &p_font, int p_font_size);
	// Panel size for the configured mode, so the caller can size the Popup.
	Size2 get_panel_size() const;

	WebInputPickerPanel();
};

VARIANT_ENUM_CAST(WebInputPickerPanel::Mode);
VARIANT_ENUM_CAST(WebInputPickerPanel::View);
