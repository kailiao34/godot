/**************************************************************************/
/*  web_svg_style.cpp                                                     */
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

#include "web_svg_style.h"

#include "core/math/math_funcs.h"
#include "core/math/rect2.h"
#include "core/templates/hash_set.h"
#include "core/templates/vector.h"

void WebSVGStyleReport::add_unsupported(const String &p_what) {
	if (!unsupported.has(p_what)) {
		unsupported.push_back(p_what);
	}
}

void WebSVGStyleReport::add_warning(const String &p_what) {
	if (!warnings.has(p_what)) {
		warnings.push_back(p_what);
	}
}

namespace {

typedef WebSVGStyleEnv Env;
typedef WebSVGStyleReport Rep;

// ---------------------------------------------------------------------------
// Character and string helpers
// ---------------------------------------------------------------------------

inline bool is_ws(char32_t c) {
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}
inline bool is_digit(char32_t c) {
	return c >= '0' && c <= '9';
}
inline bool is_alpha(char32_t c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
inline bool is_ident_char(char32_t c) {
	return is_alpha(c) || is_digit(c) || c == '-' || c == '_' || c >= 0x80;
}
inline int hex_val(char32_t c) {
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	return -1;
}

String fmt_num(double p_v) {
	if (Math::is_nan(p_v) || Math::is_inf(p_v)) {
		return "0";
	}
	if (Math::abs(p_v) < 0.0000005) {
		return "0";
	}
	String s = String::num(p_v, 6);
	if (s.find_char('.') != -1) {
		while (s.ends_with("0")) {
			s = s.substr(0, s.length() - 1);
		}
		if (s.ends_with(".")) {
			s = s.substr(0, s.length() - 1);
		}
	}
	if (s == "-0" || s.is_empty()) {
		s = "0";
	}
	return s;
}

// Index of the `)` closing the `(` at `p_open`, or -1.
int find_close_paren(const String &p_s, int p_open) {
	int depth = 0;
	char32_t quote = 0;
	for (int i = p_open; i < p_s.length(); i++) {
		const char32_t c = p_s[i];
		if (quote) {
			if (c == '\\') {
				i++;
			} else if (c == quote) {
				quote = 0;
			}
			continue;
		}
		if (c == '"' || c == '\'') {
			quote = c;
		} else if (c == '(') {
			depth++;
		} else if (c == ')') {
			depth--;
			if (depth == 0) {
				return i;
			}
		}
	}
	return -1;
}

// Splits on `p_sep` outside of parentheses, brackets and quotes. Pieces are not trimmed.
Vector<String> split_top(const String &p_s, char32_t p_sep) {
	Vector<String> out;
	int depth = 0;
	char32_t quote = 0;
	int start = 0;
	for (int i = 0; i < p_s.length(); i++) {
		const char32_t c = p_s[i];
		if (quote) {
			if (c == '\\') {
				i++;
			} else if (c == quote) {
				quote = 0;
			}
			continue;
		}
		if (c == '"' || c == '\'') {
			quote = c;
		} else if (c == '(' || c == '[') {
			depth++;
		} else if (c == ')' || c == ']') {
			depth--;
		} else if (c == p_sep && depth == 0) {
			out.push_back(p_s.substr(start, i - start));
			start = i + 1;
		}
	}
	out.push_back(p_s.substr(start));
	return out;
}

// Splits on runs of whitespace outside of parentheses and quotes.
Vector<String> split_ws_top(const String &p_s) {
	Vector<String> out;
	int depth = 0;
	char32_t quote = 0;
	int start = -1;
	for (int i = 0; i < p_s.length(); i++) {
		const char32_t c = p_s[i];
		if (quote) {
			if (c == '\\') {
				i++;
			} else if (c == quote) {
				quote = 0;
			}
			continue;
		}
		if (depth == 0 && is_ws(c)) {
			if (start != -1) {
				out.push_back(p_s.substr(start, i - start));
				start = -1;
			}
			continue;
		}
		if (start == -1) {
			start = i;
		}
		if (c == '"' || c == '\'') {
			quote = c;
		} else if (c == '(') {
			depth++;
		} else if (c == ')') {
			depth--;
		}
	}
	if (start != -1) {
		out.push_back(p_s.substr(start));
	}
	return out;
}

String strip_css_comments(const String &p_s) {
	if (p_s.find("/*") == -1) {
		return p_s;
	}
	String out;
	char32_t quote = 0;
	for (int i = 0; i < p_s.length(); i++) {
		const char32_t c = p_s[i];
		if (quote) {
			out += String::chr(c);
			if (c == '\\' && i + 1 < p_s.length()) {
				out += String::chr(p_s[++i]);
			} else if (c == quote) {
				quote = 0;
			}
			continue;
		}
		if (c == '"' || c == '\'') {
			quote = c;
			out += String::chr(c);
		} else if (c == '/' && i + 1 < p_s.length() && p_s[i + 1] == '*') {
			const int end = p_s.find("*/", i + 2);
			if (end == -1) {
				break;
			}
			i = end + 1;
			out += " ";
		} else {
			out += String::chr(c);
		}
	}
	return out;
}

String xml_unescape(const String &p_s) {
	if (p_s.find_char('&') == -1) {
		return p_s;
	}
	return p_s.replace("&quot;", "\"").replace("&apos;", "'").replace("&lt;", "<").replace("&gt;", ">").replace("&amp;", "&");
}

// True when `p_s` at `p_pos` starts the identifier `p_name` followed by `(`, with
// a word boundary before it.
bool call_at(const String &p_s, int p_pos, const char *p_name) {
	const int len = (int)strlen(p_name);
	if (p_pos + len >= p_s.length() || p_s[p_pos + len] != '(') {
		return false;
	}
	if (p_pos > 0 && is_ident_char(p_s[p_pos - 1])) {
		return false;
	}
	for (int i = 0; i < len; i++) {
		if (String::char_lowercase(p_s[p_pos + i]) != (char32_t)p_name[i]) {
			return false;
		}
	}
	return true;
}

// ---------------------------------------------------------------------------
// Numbers, units and math functions
// ---------------------------------------------------------------------------

enum DimKind {
	D_NUM,
	D_LEN, // px
	D_PCT,
	D_ANGLE, // deg
	D_TIME, // s
};

struct Dim {
	double v = 0;
	int kind = D_NUM;
};

bool read_number(const String &p_s, int &r_i, double &r_out) {
	const int start = r_i;
	const int n = p_s.length();
	int i = r_i;
	if (i < n && (p_s[i] == '+' || p_s[i] == '-')) {
		i++;
	}
	int digits = 0;
	while (i < n && is_digit(p_s[i])) {
		i++;
		digits++;
	}
	if (i < n && p_s[i] == '.') {
		i++;
		while (i < n && is_digit(p_s[i])) {
			i++;
			digits++;
		}
	}
	if (digits == 0) {
		return false;
	}
	if (i < n && (p_s[i] == 'e' || p_s[i] == 'E')) {
		int j = i + 1;
		if (j < n && (p_s[j] == '+' || p_s[j] == '-')) {
			j++;
		}
		if (j < n && is_digit(p_s[j])) {
			while (j < n && is_digit(p_s[j])) {
				j++;
			}
			i = j;
		}
	}
	r_out = p_s.substr(start, i - start).to_float();
	r_i = i;
	return true;
}

bool unit_to_dim(const String &p_unit, double p_v, Dim &r_d, const Env &p_env, double p_font_px) {
	const String u = p_unit.to_lower();
	r_d.v = p_v;
	if (u.is_empty()) {
		r_d.kind = D_NUM;
	} else if (u == "%") {
		r_d.kind = D_PCT;
	} else if (u == "px") {
		r_d.kind = D_LEN;
	} else if (u == "pt") {
		r_d.kind = D_LEN;
		r_d.v = p_v * 4.0 / 3.0;
	} else if (u == "pc") {
		r_d.kind = D_LEN;
		r_d.v = p_v * 16.0;
	} else if (u == "in") {
		r_d.kind = D_LEN;
		r_d.v = p_v * 96.0;
	} else if (u == "cm") {
		r_d.kind = D_LEN;
		r_d.v = p_v * 96.0 / 2.54;
	} else if (u == "mm") {
		r_d.kind = D_LEN;
		r_d.v = p_v * 96.0 / 25.4;
	} else if (u == "q") {
		r_d.kind = D_LEN;
		r_d.v = p_v * 96.0 / 101.6;
	} else if (u == "em") {
		r_d.kind = D_LEN;
		r_d.v = p_v * p_font_px;
	} else if (u == "rem") {
		r_d.kind = D_LEN;
		r_d.v = p_v * p_env.root_font_size;
	} else if (u == "ex" || u == "ch") {
		r_d.kind = D_LEN;
		r_d.v = p_v * p_font_px * 0.5;
	} else if (u == "vw") {
		r_d.kind = D_LEN;
		r_d.v = p_v * p_env.viewport.x / 100.0;
	} else if (u == "vh") {
		r_d.kind = D_LEN;
		r_d.v = p_v * p_env.viewport.y / 100.0;
	} else if (u == "vmin") {
		r_d.kind = D_LEN;
		r_d.v = p_v * MIN(p_env.viewport.x, p_env.viewport.y) / 100.0;
	} else if (u == "vmax") {
		r_d.kind = D_LEN;
		r_d.v = p_v * MAX(p_env.viewport.x, p_env.viewport.y) / 100.0;
	} else if (u == "deg") {
		r_d.kind = D_ANGLE;
	} else if (u == "grad") {
		r_d.kind = D_ANGLE;
		r_d.v = p_v * 0.9;
	} else if (u == "rad") {
		r_d.kind = D_ANGLE;
		r_d.v = p_v * 180.0 / Math::PI;
	} else if (u == "turn") {
		r_d.kind = D_ANGLE;
		r_d.v = p_v * 360.0;
	} else if (u == "s") {
		r_d.kind = D_TIME;
	} else if (u == "ms") {
		r_d.kind = D_TIME;
		r_d.v = p_v * 0.001;
	} else {
		return false;
	}
	return true;
}

// Parses a single dimension token such as `12px`, `50%`, `0.5turn` or `3`.
bool parse_dim(const String &p_tok, Dim &r_d, const Env &p_env, double p_font_px) {
	const String t = p_tok.strip_edges();
	int i = 0;
	double v = 0;
	if (!read_number(t, i, v)) {
		return false;
	}
	return unit_to_dim(t.substr(i), v, r_d, p_env, p_font_px);
}

String dim_to_string(const Dim &p_d) {
	switch (p_d.kind) {
		case D_LEN:
			return fmt_num(p_d.v) + "px";
		case D_PCT:
			return fmt_num(p_d.v) + "%";
		case D_ANGLE:
			return fmt_num(p_d.v) + "deg";
		case D_TIME:
			return fmt_num(p_d.v) + "s";
		default:
			return fmt_num(p_d.v);
	}
}

// Recursive-descent evaluator for the CSS math functions.
struct MathParser {
	String s;
	int i = 0;
	const Env *env = nullptr;
	double font = 16;
	bool ok = true;
	int depth = 0;

	void ws() {
		while (i < s.length() && is_ws(s[i])) {
			i++;
		}
	}

	Dim expr() {
		Dim a = term();
		while (ok) {
			ws();
			if (i < s.length() && (s[i] == '+' || s[i] == '-')) {
				const char32_t op = s[i++];
				const Dim b = term();
				if (a.kind != b.kind) {
					ok = false;
					break;
				}
				a.v += (op == '+') ? b.v : -b.v;
			} else {
				break;
			}
		}
		return a;
	}

	Dim term() {
		Dim a = factor();
		while (ok) {
			ws();
			if (i < s.length() && (s[i] == '*' || s[i] == '/')) {
				const char32_t op = s[i++];
				const Dim b = factor();
				if (op == '*') {
					if (a.kind == D_NUM) {
						a.kind = b.kind;
						a.v *= b.v;
					} else if (b.kind == D_NUM) {
						a.v *= b.v;
					} else {
						ok = false;
					}
				} else {
					if (b.kind == D_NUM) {
						a.v = (b.v == 0) ? 0 : a.v / b.v;
					} else if (a.kind == b.kind) {
						a.v = (b.v == 0) ? 0 : a.v / b.v;
						a.kind = D_NUM;
					} else {
						ok = false;
					}
				}
			} else {
				break;
			}
		}
		return a;
	}

	Dim factor() {
		ws();
		Dim r;
		if (i >= s.length() || depth > 32) {
			ok = false;
			return r;
		}
		const char32_t c = s[i];
		if (c == '(') {
			i++;
			depth++;
			r = expr();
			depth--;
			ws();
			if (i < s.length() && s[i] == ')') {
				i++;
			} else {
				ok = false;
			}
			return r;
		}
		if (c == '+' || c == '-' || c == '.' || is_digit(c)) {
			int save = i;
			double v = 0;
			if (read_number(s, i, v)) {
				int us = i;
				while (i < s.length() && (is_alpha(s[i]) || s[i] == '%')) {
					i++;
				}
				if (!unit_to_dim(s.substr(us, i - us), v, r, *env, font)) {
					ok = false;
				}
				return r;
			}
			i = save;
			if (c == '+' || c == '-') {
				i++;
				Dim x = factor();
				if (c == '-') {
					x.v = -x.v;
				}
				return x;
			}
			ok = false;
			return r;
		}
		if (is_alpha(c)) {
			const int st = i;
			while (i < s.length() && is_ident_char(s[i])) {
				i++;
			}
			const String name = s.substr(st, i - st).to_lower();
			if (i < s.length() && s[i] == '(') {
				return call(name);
			}
			if (name == "pi") {
				r.v = Math::PI;
			} else if (name == "e") {
				r.v = Math::E;
			} else if (name == "infinity") {
				r.v = Math::INF;
			} else if (name == "nan") {
				r.v = Math::NaN;
			} else {
				ok = false;
			}
			return r;
		}
		ok = false;
		return r;
	}

	Dim call(const String &p_name) {
		i++; // (
		depth++;
		Vector<Dim> args;
		String strategy;
		ws();
		if (p_name == "round") {
			// Optional rounding strategy keyword.
			int j = i;
			while (j < s.length() && (is_alpha(s[j]) || s[j] == '-')) {
				j++;
			}
			int k = j;
			while (k < s.length() && is_ws(s[k])) {
				k++;
			}
			if (j > i && k < s.length() && s[k] == ',') {
				strategy = s.substr(i, j - i).to_lower();
				i = k + 1;
			}
		}
		while (ok) {
			args.push_back(expr());
			ws();
			if (i < s.length() && s[i] == ',') {
				i++;
				continue;
			}
			break;
		}
		ws();
		if (i < s.length() && s[i] == ')') {
			i++;
		} else {
			ok = false;
		}
		depth--;
		Dim r;
		if (!ok || args.is_empty()) {
			ok = false;
			return r;
		}
		r = args[0];
		const int n = args.size();
		auto same_kind = [&]() {
			for (int k = 1; k < n; k++) {
				if (args[k].kind != args[0].kind) {
					ok = false;
				}
			}
		};
		auto as_radians = [&](const Dim &d) {
			if (d.kind == D_ANGLE) {
				return d.v * Math::PI / 180.0;
			}
			if (d.kind != D_NUM) {
				ok = false;
			}
			return d.v;
		};
		auto need_num = [&](const Dim &d) {
			if (d.kind != D_NUM) {
				ok = false;
			}
			return d.v;
		};
		if (p_name == "calc") {
			return r;
		} else if (p_name == "min" || p_name == "max") {
			same_kind();
			for (int k = 1; k < n; k++) {
				r.v = (p_name == "min") ? MIN(r.v, args[k].v) : MAX(r.v, args[k].v);
			}
		} else if (p_name == "clamp") {
			if (n != 3) {
				ok = false;
				return r;
			}
			same_kind();
			r.v = MAX(args[0].v, MIN(args[1].v, args[2].v));
			r.kind = args[0].kind;
		} else if (p_name == "abs") {
			r.v = Math::abs(r.v);
		} else if (p_name == "sign") {
			r.v = (r.v > 0) ? 1 : (r.v < 0 ? -1 : 0);
			r.kind = D_NUM;
		} else if (p_name == "sqrt") {
			r.v = Math::sqrt(need_num(r));
		} else if (p_name == "exp") {
			r.v = Math::exp(need_num(r));
		} else if (p_name == "log") {
			r.v = Math::log(need_num(r));
		} else if (p_name == "pow") {
			if (n != 2) {
				ok = false;
				return r;
			}
			r.v = Math::pow(need_num(args[0]), need_num(args[1]));
		} else if (p_name == "hypot") {
			same_kind();
			double sum = 0;
			for (int k = 0; k < n; k++) {
				sum += args[k].v * args[k].v;
			}
			r.v = Math::sqrt(sum);
		} else if (p_name == "sin") {
			r.v = Math::sin(as_radians(r));
			r.kind = D_NUM;
		} else if (p_name == "cos") {
			r.v = Math::cos(as_radians(r));
			r.kind = D_NUM;
		} else if (p_name == "tan") {
			r.v = Math::tan(as_radians(r));
			r.kind = D_NUM;
		} else if (p_name == "asin") {
			r.v = Math::asin(need_num(r)) * 180.0 / Math::PI;
			r.kind = D_ANGLE;
		} else if (p_name == "acos") {
			r.v = Math::acos(need_num(r)) * 180.0 / Math::PI;
			r.kind = D_ANGLE;
		} else if (p_name == "atan") {
			r.v = Math::atan(need_num(r)) * 180.0 / Math::PI;
			r.kind = D_ANGLE;
		} else if (p_name == "atan2") {
			if (n != 2 || args[0].kind != args[1].kind) {
				ok = false;
				return r;
			}
			r.v = Math::atan2(args[0].v, args[1].v) * 180.0 / Math::PI;
			r.kind = D_ANGLE;
		} else if (p_name == "round" || p_name == "mod" || p_name == "rem") {
			if (n < 1 || n > 2) {
				ok = false;
				return r;
			}
			const double step = (n == 2) ? args[1].v : 1.0;
			if (n == 2 && args[0].kind != args[1].kind) {
				ok = false;
				return r;
			}
			if (step == 0) {
				ok = false;
				return r;
			}
			if (p_name == "mod") {
				r.v = args[0].v - step * Math::floor(args[0].v / step);
			} else if (p_name == "rem") {
				r.v = Math::fmod(args[0].v, step);
			} else if (strategy == "up") {
				r.v = Math::ceil(args[0].v / step) * step;
			} else if (strategy == "down") {
				r.v = Math::floor(args[0].v / step) * step;
			} else if (strategy == "to-zero") {
				const double q = args[0].v / step;
				r.v = (q < 0 ? Math::ceil(q) : Math::floor(q)) * step;
			} else {
				r.v = Math::round(args[0].v / step) * step;
			}
		} else {
			ok = false;
		}
		return r;
	}
};

bool eval_math(const String &p_call, Dim &r_out, const Env &p_env, double p_font_px) {
	MathParser p;
	p.s = p_call;
	p.env = &p_env;
	p.font = p_font_px;
	r_out = p.expr();
	p.ws();
	return p.ok && p.i == p.s.length();
}

// Replaces every math function call (calc(), min(), ...) in a value by its result.
String replace_math(const String &p_value, const Env &p_env, double p_font_px, Rep *r_rep) {
	static const char *names[] = { "calc", "min", "max", "clamp", "abs", "sign", "round", "mod", "rem", "sin", "cos", "tan", "asin", "acos", "atan", "atan2", "sqrt", "pow", "hypot", "exp", "log", nullptr };
	if (p_value.find("(") == -1) {
		return p_value;
	}
	String out;
	int i = 0;
	const int n = p_value.length();
	while (i < n) {
		const char32_t c = p_value[i];
		if (is_alpha(c) && (i == 0 || !is_ident_char(p_value[i - 1]))) {
			bool matched = false;
			for (int k = 0; names[k]; k++) {
				if (call_at(p_value, i, names[k])) {
					const int open = i + (int)strlen(names[k]);
					const int close = find_close_paren(p_value, open);
					if (close == -1) {
						break;
					}
					const String call = p_value.substr(i, close - i + 1);
					Dim d;
					if (eval_math(call, d, p_env, p_font_px)) {
						out += dim_to_string(d);
					} else {
						out += call;
						if (r_rep) {
							r_rep->add_warning("Cannot resolve " + String(names[k]) + "() (percentages or mixed units): " + call);
						}
					}
					i = close + 1;
					matched = true;
					break;
				}
			}
			if (matched) {
				continue;
			}
		}
		out += String::chr(c);
		i++;
	}
	return out;
}

// ---------------------------------------------------------------------------
// Colors
// ---------------------------------------------------------------------------

struct RGBA {
	double r = 0, g = 0, b = 0, a = 1; // sRGB 0..1, not premultiplied
};

double srgb_to_linear(double c) {
	return (c <= 0.04045) ? c / 12.92 : Math::pow((c + 0.055) / 1.055, 2.4);
}
double linear_to_srgb(double c) {
	return (c <= 0.0031308) ? 12.92 * c : 1.055 * Math::pow(c, 1.0 / 2.4) - 0.055;
}
double clamp01(double c) {
	return c < 0 ? 0 : (c > 1 ? 1 : c);
}
double cbrt_signed(double x) {
	return x < 0 ? -Math::pow(-x, 1.0 / 3.0) : Math::pow(x, 1.0 / 3.0);
}

RGBA linear_to_rgba(double r, double g, double b, double a) {
	RGBA o;
	o.r = clamp01(linear_to_srgb(r));
	o.g = clamp01(linear_to_srgb(g));
	o.b = clamp01(linear_to_srgb(b));
	o.a = clamp01(a);
	return o;
}

RGBA xyz_d65_to_rgba(double x, double y, double z, double a) {
	const double r = 3.2404542 * x - 1.5371385 * y - 0.4985314 * z;
	const double g = -0.9692660 * x + 1.8760108 * y + 0.0415560 * z;
	const double b = 0.0556434 * x - 0.2040259 * y + 1.0572252 * z;
	return linear_to_rgba(r, g, b, a);
}

RGBA oklab_to_rgba(double L, double A, double B, double a) {
	const double l_ = L + 0.3963377774 * A + 0.2158037573 * B;
	const double m_ = L - 0.1055613458 * A - 0.0638541728 * B;
	const double s_ = L - 0.0894841775 * A - 1.2914855480 * B;
	const double l = l_ * l_ * l_;
	const double m = m_ * m_ * m_;
	const double s = s_ * s_ * s_;
	return linear_to_rgba(
			4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
			-1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
			-0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s, a);
}

void rgba_to_oklab(const RGBA &c, double &L, double &A, double &B) {
	const double r = srgb_to_linear(c.r), g = srgb_to_linear(c.g), b = srgb_to_linear(c.b);
	const double l = cbrt_signed(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
	const double m = cbrt_signed(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
	const double s = cbrt_signed(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
	L = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s;
	A = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
	B = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
}

RGBA lab_to_rgba(double L, double A, double B, double a) {
	const double kappa = 24389.0 / 27.0;
	const double eps = 216.0 / 24389.0;
	const double fy = (L + 16.0) / 116.0;
	const double fx = A / 500.0 + fy;
	const double fz = fy - B / 200.0;
	const double xr = (fx * fx * fx > eps) ? fx * fx * fx : (116.0 * fx - 16.0) / kappa;
	const double yr = (L > kappa * eps) ? fy * fy * fy : L / kappa;
	const double zr = (fz * fz * fz > eps) ? fz * fz * fz : (116.0 * fz - 16.0) / kappa;
	// D50 white, then Bradford-adapted to D65.
	const double X = xr * 0.96422, Y = yr, Z = zr * 0.82521;
	const double x65 = 0.9555766 * X - 0.0230393 * Y + 0.0631636 * Z;
	const double y65 = -0.0282895 * X + 1.0099416 * Y + 0.0210077 * Z;
	const double z65 = 0.0122982 * X - 0.0204830 * Y + 1.3299098 * Z;
	return xyz_d65_to_rgba(x65, y65, z65, a);
}

RGBA hsl_to_rgba(double h, double s, double l, double a) {
	h = Math::fmod(h, 360.0);
	if (h < 0) {
		h += 360.0;
	}
	s = clamp01(s);
	l = clamp01(l);
	auto f = [&](double n) {
		const double k = Math::fmod(n + h / 30.0, 12.0);
		return l - s * MIN(l, 1.0 - l) * MAX(-1.0, MIN(MIN(k - 3.0, 9.0 - k), 1.0));
	};
	RGBA o;
	o.r = clamp01(f(0));
	o.g = clamp01(f(8));
	o.b = clamp01(f(4));
	o.a = clamp01(a);
	return o;
}

RGBA hwb_to_rgba(double h, double w, double bl, double a) {
	w = clamp01(w);
	bl = clamp01(bl);
	if (w + bl >= 1.0) {
		const double g = w / (w + bl);
		RGBA o;
		o.r = o.g = o.b = g;
		o.a = clamp01(a);
		return o;
	}
	RGBA c = hsl_to_rgba(h, 1.0, 0.5, a);
	const double scale = 1.0 - w - bl;
	c.r = c.r * scale + w;
	c.g = c.g * scale + w;
	c.b = c.b * scale + w;
	return c;
}

RGBA display_p3_to_rgba(double r, double g, double b, double a) {
	const double lr = srgb_to_linear(r), lg = srgb_to_linear(g), lb = srgb_to_linear(b);
	const double x = 0.4865709 * lr + 0.2656677 * lg + 0.1982173 * lb;
	const double y = 0.2289746 * lr + 0.6917385 * lg + 0.0792869 * lb;
	const double z = 0.0 * lr + 0.0451134 * lg + 1.0439444 * lb;
	return xyz_d65_to_rgba(x, y, z, a);
}

struct NamedColor {
	const char *name;
	uint32_t rgb;
};
const NamedColor basic_colors[] = {
	{ "black", 0x000000 }, { "silver", 0xc0c0c0 }, { "gray", 0x808080 }, { "grey", 0x808080 },
	{ "white", 0xffffff }, { "maroon", 0x800000 }, { "red", 0xff0000 }, { "purple", 0x800080 },
	{ "fuchsia", 0xff00ff }, { "magenta", 0xff00ff }, { "green", 0x008000 }, { "lime", 0x00ff00 },
	{ "olive", 0x808000 }, { "yellow", 0xffff00 }, { "navy", 0x000080 }, { "blue", 0x0000ff },
	{ "teal", 0x008080 }, { "aqua", 0x00ffff }, { "cyan", 0x00ffff }, { "orange", 0xffa500 },
	{ "pink", 0xffc0cb }, { "brown", 0xa52a2a }, { "gold", 0xffd700 }, { "indigo", 0x4b0082 },
	{ "violet", 0xee82ee }, { "coral", 0xff7f50 }, { "crimson", 0xdc143c }, { "tomato", 0xff6347 },
	{ nullptr, 0 }
};

bool parse_hex(const String &p_h, RGBA &r_out) {
	const int n = p_h.length();
	if (n != 3 && n != 4 && n != 6 && n != 8) {
		return false;
	}
	int v[8];
	for (int i = 0; i < n; i++) {
		v[i] = hex_val(p_h[i]);
		if (v[i] < 0) {
			return false;
		}
	}
	if (n <= 4) {
		r_out.r = (v[0] * 17) / 255.0;
		r_out.g = (v[1] * 17) / 255.0;
		r_out.b = (v[2] * 17) / 255.0;
		r_out.a = (n == 4) ? (v[3] * 17) / 255.0 : 1.0;
	} else {
		r_out.r = (v[0] * 16 + v[1]) / 255.0;
		r_out.g = (v[2] * 16 + v[3]) / 255.0;
		r_out.b = (v[4] * 16 + v[5]) / 255.0;
		r_out.a = (n == 8) ? (v[6] * 16 + v[7]) / 255.0 : 1.0;
	}
	return true;
}

// One color component: a number, a percentage (scaled by `p_pct_scale`) or `none`.
bool color_comp(const String &p_t, double p_pct_scale, double &r_v) {
	const String t = p_t.strip_edges().to_lower();
	if (t == "none") {
		r_v = 0;
		return true;
	}
	int i = 0;
	double v = 0;
	if (!read_number(t, i, v)) {
		return false;
	}
	if (i == t.length()) {
		r_v = v;
		return true;
	}
	if (t.substr(i) == "%") {
		r_v = v / 100.0 * p_pct_scale;
		return true;
	}
	return false;
}

bool color_hue(const String &p_t, double &r_deg) {
	const String t = p_t.strip_edges().to_lower();
	if (t == "none") {
		r_deg = 0;
		return true;
	}
	Dim d;
	Env env;
	if (!parse_dim(t, d, env, 16)) {
		return false;
	}
	if (d.kind != D_NUM && d.kind != D_ANGLE) {
		return false;
	}
	r_deg = d.v;
	return true;
}

// Splits color function arguments into components and an alpha, for both the
// legacy comma syntax and the space / slash syntax.
bool split_color_args(const String &p_args, Vector<String> &r_parts, String &r_alpha) {
	r_alpha = String();
	const Vector<String> commas = split_top(p_args, ',');
	if (commas.size() > 1) {
		for (int i = 0; i < commas.size(); i++) {
			if (i < 3) {
				r_parts.push_back(commas[i].strip_edges());
			} else if (i == 3) {
				r_alpha = commas[i].strip_edges();
			} else {
				return false;
			}
		}
		return r_parts.size() == 3;
	}
	const Vector<String> slash = split_top(p_args, '/');
	if (slash.size() > 2) {
		return false;
	}
	if (slash.size() == 2) {
		r_alpha = slash[1].strip_edges();
	}
	const Vector<String> parts = split_ws_top(slash[0]);
	for (int i = 0; i < parts.size(); i++) {
		r_parts.push_back(parts[i]);
	}
	return r_parts.size() == 3;
}

bool parse_color_rec(const String &p_value, RGBA &r_out, const Env &p_env, int p_depth);

bool parse_alpha(const String &p_alpha, double &r_a) {
	if (p_alpha.is_empty()) {
		r_a = 1.0;
		return true;
	}
	double v = 1;
	if (!color_comp(p_alpha, 1.0, v)) {
		return false;
	}
	r_a = clamp01(v);
	return true;
}

// Interpolation in the requested color space, premultiplied alpha.
bool mix_colors(const String &p_space, const String &p_hue_method, const RGBA &p_a, const RGBA &p_b, double p_t, RGBA &r_out) {
	const double a1 = p_a.a, a2 = p_b.a;
	const double alpha = a1 * (1.0 - p_t) + a2 * p_t;
	auto mix_premult = [&](double x, double y) {
		if (alpha <= 0) {
			return 0.0;
		}
		return (x * a1 * (1.0 - p_t) + y * a2 * p_t) / alpha;
	};
	auto mix_hue = [&](double h1, double h2) {
		double d = h2 - h1;
		if (p_hue_method == "longer") {
			if (d > 0 && d < 180) {
				d -= 360;
			} else if (d <= 0 && d > -180) {
				d += 360;
			}
		} else if (p_hue_method == "increasing") {
			if (d < 0) {
				d += 360;
			}
		} else if (p_hue_method == "decreasing") {
			if (d > 0) {
				d -= 360;
			}
		} else {
			if (d > 180) {
				d -= 360;
			} else if (d < -180) {
				d += 360;
			}
		}
		return h1 + d * p_t;
	};
	if (p_space == "srgb") {
		r_out.r = clamp01(mix_premult(p_a.r, p_b.r));
		r_out.g = clamp01(mix_premult(p_a.g, p_b.g));
		r_out.b = clamp01(mix_premult(p_a.b, p_b.b));
	} else if (p_space == "srgb-linear") {
		const double r = mix_premult(srgb_to_linear(p_a.r), srgb_to_linear(p_b.r));
		const double g = mix_premult(srgb_to_linear(p_a.g), srgb_to_linear(p_b.g));
		const double b = mix_premult(srgb_to_linear(p_a.b), srgb_to_linear(p_b.b));
		r_out = linear_to_rgba(r, g, b, alpha);
	} else if (p_space == "oklab" || p_space == "oklch") {
		double L1, A1, B1, L2, A2, B2;
		rgba_to_oklab(p_a, L1, A1, B1);
		rgba_to_oklab(p_b, L2, A2, B2);
		if (p_space == "oklab") {
			r_out = oklab_to_rgba(mix_premult(L1, L2), mix_premult(A1, A2), mix_premult(B1, B2), alpha);
		} else {
			const double C1 = Math::sqrt(A1 * A1 + B1 * B1), C2 = Math::sqrt(A2 * A2 + B2 * B2);
			double H1 = Math::rad_to_deg(Math::atan2(B1, A1)), H2 = Math::rad_to_deg(Math::atan2(B2, A2));
			if (C1 < 1e-6) {
				H1 = H2;
			}
			if (C2 < 1e-6) {
				H2 = H1;
			}
			const double L = mix_premult(L1, L2), C = mix_premult(C1, C2), H = Math::deg_to_rad(mix_hue(H1, H2));
			r_out = oklab_to_rgba(L, C * Math::cos(H), C * Math::sin(H), alpha);
		}
	} else if (p_space == "hsl") {
		auto to_hsl = [](const RGBA &c, double &h, double &s, double &l) {
			const double mx = MAX(c.r, MAX(c.g, c.b)), mn = MIN(c.r, MIN(c.g, c.b));
			l = (mx + mn) / 2.0;
			const double d = mx - mn;
			h = 0;
			s = 0;
			if (d > 1e-9) {
				s = (l > 0.5) ? d / (2.0 - mx - mn) : d / (mx + mn);
				if (mx == c.r) {
					h = (c.g - c.b) / d + (c.g < c.b ? 6 : 0);
				} else if (mx == c.g) {
					h = (c.b - c.r) / d + 2;
				} else {
					h = (c.r - c.g) / d + 4;
				}
				h *= 60.0;
			}
		};
		double h1, s1, l1, h2, s2, l2;
		to_hsl(p_a, h1, s1, l1);
		to_hsl(p_b, h2, s2, l2);
		if (s1 < 1e-6) {
			h1 = h2;
		}
		if (s2 < 1e-6) {
			h2 = h1;
		}
		r_out = hsl_to_rgba(mix_hue(h1, h2), mix_premult(s1, s2), mix_premult(l1, l2), alpha);
	} else {
		return false;
	}
	r_out.a = clamp01(alpha);
	return true;
}

bool parse_color_mix(const String &p_args, RGBA &r_out, const Env &p_env, int p_depth) {
	const Vector<String> args = split_top(p_args, ',');
	if (args.size() != 3) {
		return false;
	}
	const Vector<String> head = split_ws_top(args[0].strip_edges());
	if (head.size() < 2 || head[0].to_lower() != "in") {
		return false;
	}
	const String space = head[1].to_lower();
	String hue_method = "shorter";
	if (head.size() >= 4 && head[3].to_lower() == "hue") {
		hue_method = head[2].to_lower();
	}
	RGBA colors[2];
	double pct[2] = { -1, -1 };
	for (int k = 0; k < 2; k++) {
		const Vector<String> toks = split_ws_top(args[k + 1].strip_edges());
		String color_text;
		for (int i = 0; i < toks.size(); i++) {
			const String &t = toks[i];
			if (t.ends_with("%")) {
				pct[k] = t.substr(0, t.length() - 1).to_float();
			} else {
				color_text += (color_text.is_empty() ? "" : " ") + t;
			}
		}
		if (!parse_color_rec(color_text, colors[k], p_env, p_depth + 1)) {
			return false;
		}
	}
	if (pct[0] < 0 && pct[1] < 0) {
		pct[0] = pct[1] = 50;
	} else if (pct[0] < 0) {
		pct[0] = 100 - pct[1];
	} else if (pct[1] < 0) {
		pct[1] = 100 - pct[0];
	}
	const double sum = pct[0] + pct[1];
	if (sum <= 0) {
		return false;
	}
	const double t = pct[1] / sum;
	if (!mix_colors(space, hue_method, colors[0], colors[1], t, r_out)) {
		return false;
	}
	if (sum < 100) {
		r_out.a *= sum / 100.0;
	}
	return true;
}

bool parse_color_rec(const String &p_value, RGBA &r_out, const Env &p_env, int p_depth) {
	if (p_depth > 8) {
		return false;
	}
	const String v = p_value.strip_edges();
	if (v.is_empty()) {
		return false;
	}
	const String lv = v.to_lower();
	if (lv == "transparent") {
		r_out = RGBA();
		r_out.a = 0;
		return true;
	}
	if (lv == "currentcolor") {
		r_out.r = p_env.current_color.r;
		r_out.g = p_env.current_color.g;
		r_out.b = p_env.current_color.b;
		r_out.a = p_env.current_color.a;
		return true;
	}
	if (v[0] == '#') {
		return parse_hex(v.substr(1), r_out);
	}
	for (int k = 0; basic_colors[k].name; k++) {
		if (lv == basic_colors[k].name) {
			const uint32_t c = basic_colors[k].rgb;
			r_out.r = ((c >> 16) & 0xff) / 255.0;
			r_out.g = ((c >> 8) & 0xff) / 255.0;
			r_out.b = (c & 0xff) / 255.0;
			r_out.a = 1;
			return true;
		}
	}
	const int open = v.find_char('(');
	if (open <= 0 || !v.ends_with(")")) {
		return false;
	}
	const String name = lv.substr(0, open);
	const String args = v.substr(open + 1, v.length() - open - 2);

	if (name == "light-dark") {
		const Vector<String> two = split_top(args, ',');
		if (two.size() != 2) {
			return false;
		}
		return parse_color_rec(two[p_env.dark_color_scheme ? 1 : 0], r_out, p_env, p_depth + 1);
	}
	if (name == "color-mix") {
		return parse_color_mix(args, r_out, p_env, p_depth);
	}

	Vector<String> parts;
	String alpha_text;
	if (name == "color") {
		const Vector<String> slash = split_top(args, '/');
		if (slash.size() > 2) {
			return false;
		}
		if (slash.size() == 2) {
			alpha_text = slash[1].strip_edges();
		}
		const Vector<String> toks = split_ws_top(slash[0]);
		if (toks.size() != 4) {
			return false;
		}
		double a = 1;
		if (!parse_alpha(alpha_text, a)) {
			return false;
		}
		double c[3];
		for (int i = 0; i < 3; i++) {
			if (!color_comp(toks[i + 1], 1.0, c[i])) {
				return false;
			}
		}
		const String space = toks[0].to_lower();
		if (space == "srgb") {
			r_out.r = clamp01(c[0]);
			r_out.g = clamp01(c[1]);
			r_out.b = clamp01(c[2]);
			r_out.a = a;
		} else if (space == "srgb-linear") {
			r_out = linear_to_rgba(c[0], c[1], c[2], a);
		} else if (space == "display-p3") {
			r_out = display_p3_to_rgba(c[0], c[1], c[2], a);
		} else {
			return false;
		}
		return true;
	}

	if (!split_color_args(args, parts, alpha_text)) {
		return false;
	}
	double a = 1;
	if (!parse_alpha(alpha_text, a)) {
		return false;
	}
	if (name == "rgb" || name == "rgba") {
		double c[3];
		for (int i = 0; i < 3; i++) {
			if (!color_comp(parts[i], 255.0, c[i])) {
				return false;
			}
		}
		r_out.r = clamp01(c[0] / 255.0);
		r_out.g = clamp01(c[1] / 255.0);
		r_out.b = clamp01(c[2] / 255.0);
		r_out.a = a;
		return true;
	}
	if (name == "hsl" || name == "hsla" || name == "hwb") {
		double h = 0, x = 0, y = 0;
		if (!color_hue(parts[0], h) || !color_comp(parts[1], 1.0, x) || !color_comp(parts[2], 1.0, y)) {
			return false;
		}
		// Bare numbers in the saturation / lightness slots are percentages.
		if (!parts[1].ends_with("%")) {
			x /= 100.0;
		}
		if (!parts[2].ends_with("%")) {
			y /= 100.0;
		}
		r_out = (name == "hwb") ? hwb_to_rgba(h, x, y, a) : hsl_to_rgba(h, x, y, a);
		return true;
	}
	if (name == "lab" || name == "oklab") {
		const bool ok_ = (name == "oklab");
		double L = 0, A = 0, B = 0;
		if (!color_comp(parts[0], ok_ ? 1.0 : 100.0, L) || !color_comp(parts[1], ok_ ? 0.4 : 125.0, A) || !color_comp(parts[2], ok_ ? 0.4 : 125.0, B)) {
			return false;
		}
		r_out = ok_ ? oklab_to_rgba(L, A, B, a) : lab_to_rgba(L, A, B, a);
		return true;
	}
	if (name == "lch" || name == "oklch") {
		const bool ok_ = (name == "oklch");
		double L = 0, C = 0, H = 0;
		if (!color_comp(parts[0], ok_ ? 1.0 : 100.0, L) || !color_comp(parts[1], ok_ ? 0.4 : 150.0, C) || !color_hue(parts[2], H)) {
			return false;
		}
		const double hr = Math::deg_to_rad(H);
		r_out = ok_ ? oklab_to_rgba(L, C * Math::cos(hr), C * Math::sin(hr), a) : lab_to_rgba(L, C * Math::cos(hr), C * Math::sin(hr), a);
		return true;
	}
	return false;
}

String fmt_color(const RGBA &p_c) {
	auto b8 = [](double v) {
		int x = (int)Math::round(clamp01(v) * 255.0);
		return CLAMP(x, 0, 255);
	};
	const int r = b8(p_c.r), g = b8(p_c.g), b = b8(p_c.b);
	if (p_c.a >= 0.9995) {
		static const char *hex = "0123456789abcdef";
		String s = "#";
		s += String::chr(hex[r >> 4]) + String::chr(hex[r & 15]);
		s += String::chr(hex[g >> 4]) + String::chr(hex[g & 15]);
		s += String::chr(hex[b >> 4]) + String::chr(hex[b & 15]);
		return s;
	}
	return "rgba(" + itos(r) + "," + itos(g) + "," + itos(b) + "," + fmt_num(Math::round(p_c.a * 1000.0) / 1000.0) + ")";
}

// Rewrites every color the rasterizer would not read (hex with alpha, space syntax,
// hwb/lab/lch/oklab/oklch/color()/color-mix()/light-dark()) into rgb()/#rrggbb.
String normalize_colors(const String &p_value, const Env &p_env, Rep *r_rep) {
	static const char *fn_names[] = { "rgba", "rgb", "hsla", "hsl", "hwb", "oklab", "oklch", "lab", "lch", "color-mix", "color", "light-dark", nullptr };
	String out;
	const int n = p_value.length();
	int i = 0;
	while (i < n) {
		const char32_t c = p_value[i];
		// `url(#id)` is a reference, never a color.
		if (is_alpha(c) && call_at(p_value, i, "url")) {
			const int close = find_close_paren(p_value, i + 3);
			if (close != -1) {
				out += p_value.substr(i, close - i + 1);
				i = close + 1;
				continue;
			}
		}
		if (c == '#' && (i == 0 || !is_ident_char(p_value[i - 1]))) {
			int j = i + 1;
			while (j < n && hex_val(p_value[j]) >= 0) {
				j++;
			}
			const int len = j - i - 1;
			if ((len == 4 || len == 8) && (j >= n || !is_ident_char(p_value[j]))) {
				RGBA col;
				if (parse_hex(p_value.substr(i + 1, len), col)) {
					out += fmt_color(col);
					i = j;
					continue;
				}
			}
		}
		if (is_alpha(c) && (i == 0 || !is_ident_char(p_value[i - 1]))) {
			bool handled = false;
			for (int k = 0; fn_names[k]; k++) {
				if (!call_at(p_value, i, fn_names[k])) {
					continue;
				}
				const int open = i + (int)strlen(fn_names[k]);
				const int close = find_close_paren(p_value, open);
				if (close == -1) {
					break;
				}
				const String call = p_value.substr(i, close - i + 1);
				RGBA col;
				if (parse_color_rec(call, col, p_env, 0)) {
					out += fmt_color(col);
				} else {
					out += call;
					if (r_rep) {
						r_rep->add_unsupported("CSS color " + String(fn_names[k]) + "() with this syntax");
					}
				}
				i = close + 1;
				handled = true;
				break;
			}
			if (handled) {
				continue;
			}
			// `currentcolor` is case-insensitive in CSS; the renderer wants camel case.
			if (p_value.substr(i, 12).to_lower() == "currentcolor" && (i + 12 >= n || !is_ident_char(p_value[i + 12]))) {
				out += "currentColor";
				i += 12;
				continue;
			}
		}
		out += String::chr(c);
		i++;
	}
	return out;
}

// ---------------------------------------------------------------------------
// Markup tree
// ---------------------------------------------------------------------------

struct Attr {
	String name;
	String value;
	char32_t quote = '"'; // 0 = bare attribute with no value
};

enum NodeKind {
	N_ELEMENT,
	N_TEXT,
	N_RAW, // comment, CDATA, processing instruction, declaration
};

struct DNode {
	int kind = N_ELEMENT;
	String tag;
	String text;
	Vector<Attr> attrs;
	bool self_closing = false;
	int parent = -1;
	Vector<int> kids;
	bool dead = false;
	// Position among element siblings (for structural pseudo-classes).
	int sib_index = 0;
	int sib_count = 0;
	int type_index = 0;
	int type_count = 0;
};

struct Doc {
	Vector<DNode> n; // n[0] is the document node
	int root = -1; // the outermost <svg>
};

int attr_find(const DNode &p_n, const String &p_name) {
	for (int i = 0; i < p_n.attrs.size(); i++) {
		if (p_n.attrs[i].name == p_name) {
			return i;
		}
	}
	return -1;
}
String attr_get(const DNode &p_n, const String &p_name) {
	const int i = attr_find(p_n, p_name);
	return i == -1 ? String() : p_n.attrs[i].value;
}
void attr_set(DNode &p_n, const String &p_name, const String &p_value) {
	const int i = attr_find(p_n, p_name);
	if (i == -1) {
		Attr a;
		a.name = p_name;
		a.value = p_value;
		p_n.attrs.push_back(a);
	} else {
		p_n.attrs.write[i].value = p_value;
		if (p_n.attrs[i].quote == 0) {
			p_n.attrs.write[i].quote = '"';
		}
	}
}
void attr_remove(DNode &p_n, const String &p_name) {
	const int i = attr_find(p_n, p_name);
	if (i != -1) {
		p_n.attrs.remove_at(i);
	}
}

bool parse_doc(const String &p_src, Doc &r_doc) {
	DNode docnode;
	docnode.tag = "#document";
	r_doc.n.push_back(docnode);
	Vector<int> stack;
	stack.push_back(0);

	const int n = p_src.length();
	int i = 0;
	auto add = [&](const DNode &p_node) {
		DNode nd = p_node;
		nd.parent = stack[stack.size() - 1];
		r_doc.n.push_back(nd);
		const int idx = r_doc.n.size() - 1;
		r_doc.n.write[nd.parent].kids.push_back(idx);
		return idx;
	};
	auto add_text = [&](const String &p_t, int p_kind) {
		DNode t;
		t.kind = p_kind;
		t.text = p_t;
		add(t);
	};

	while (i < n) {
		const int lt = p_src.find_char('<', i);
		if (lt == -1) {
			add_text(p_src.substr(i), N_TEXT);
			break;
		}
		if (lt > i) {
			add_text(p_src.substr(i, lt - i), N_TEXT);
		}
		i = lt;
		if (p_src.substr(i, 4) == "<!--") {
			const int e = p_src.find("-->", i + 4);
			if (e == -1) {
				return false;
			}
			add_text(p_src.substr(i, e + 3 - i), N_RAW);
			i = e + 3;
			continue;
		}
		if (p_src.substr(i, 9) == "<![CDATA[") {
			const int e = p_src.find("]]>", i + 9);
			if (e == -1) {
				return false;
			}
			add_text(p_src.substr(i, e + 3 - i), N_RAW);
			i = e + 3;
			continue;
		}
		if (p_src.substr(i, 2) == "<?") {
			const int e = p_src.find("?>", i + 2);
			if (e == -1) {
				return false;
			}
			add_text(p_src.substr(i, e + 2 - i), N_RAW);
			i = e + 2;
			continue;
		}
		if (p_src.substr(i, 2) == "<!") {
			int e = p_src.find_char('>', i + 2);
			const int br = p_src.find_char('[', i + 2);
			if (br != -1 && br < e) {
				e = p_src.find("]>", br);
				if (e != -1) {
					e += 1;
				}
			}
			if (e == -1) {
				return false;
			}
			add_text(p_src.substr(i, e + 1 - i), N_RAW);
			i = e + 1;
			continue;
		}
		if (i + 1 < n && p_src[i + 1] == '/') {
			const int e = p_src.find_char('>', i + 2);
			if (e == -1) {
				return false;
			}
			const String name = p_src.substr(i + 2, e - i - 2).strip_edges();
			// Pop to the matching open element; stray closers are ignored.
			for (int k = stack.size() - 1; k >= 1; k--) {
				if (r_doc.n[stack[k]].tag == name) {
					stack.resize(k);
					break;
				}
			}
			i = e + 1;
			continue;
		}
		// Start tag.
		int j = i + 1;
		while (j < n && !is_ws(p_src[j]) && p_src[j] != '/' && p_src[j] != '>') {
			j++;
		}
		DNode el;
		el.tag = p_src.substr(i + 1, j - i - 1);
		if (el.tag.is_empty()) {
			add_text("<", N_TEXT);
			i++;
			continue;
		}
		bool closed = false;
		while (j < n) {
			while (j < n && is_ws(p_src[j])) {
				j++;
			}
			if (j >= n) {
				return false;
			}
			if (p_src[j] == '>') {
				j++;
				closed = true;
				break;
			}
			if (p_src[j] == '/') {
				if (j + 1 < n && p_src[j + 1] == '>') {
					el.self_closing = true;
					j += 2;
					closed = true;
					break;
				}
				j++;
				continue;
			}
			int a = j;
			while (j < n && !is_ws(p_src[j]) && p_src[j] != '=' && p_src[j] != '>' && !(p_src[j] == '/' && j + 1 < n && p_src[j + 1] == '>')) {
				j++;
			}
			Attr at;
			at.name = p_src.substr(a, j - a);
			while (j < n && is_ws(p_src[j])) {
				j++;
			}
			if (j < n && p_src[j] == '=') {
				j++;
				while (j < n && is_ws(p_src[j])) {
					j++;
				}
				if (j < n && (p_src[j] == '"' || p_src[j] == '\'')) {
					const char32_t q = p_src[j];
					const int e = p_src.find_char(q, j + 1);
					if (e == -1) {
						return false;
					}
					at.quote = q;
					at.value = p_src.substr(j + 1, e - j - 1);
					j = e + 1;
				} else {
					const int vs = j;
					while (j < n && !is_ws(p_src[j]) && p_src[j] != '>') {
						j++;
					}
					at.quote = '"';
					at.value = p_src.substr(vs, j - vs);
				}
			} else {
				at.quote = 0;
			}
			if (!at.name.is_empty()) {
				el.attrs.push_back(at);
			}
		}
		if (!closed) {
			return false;
		}
		const int idx = add(el);
		i = j;
		if (!el.self_closing) {
			stack.push_back(idx);
			// Raw-text elements: their content is not markup.
			if (el.tag == "style" || el.tag == "script") {
				const String closing = "</" + el.tag;
				const int e = p_src.find(closing, i);
				if (e == -1) {
					return false;
				}
				if (e > i) {
					add_text(p_src.substr(i, e - i), N_TEXT);
				}
				i = e;
			}
		}
	}

	// Root element and sibling positions.
	for (int k = 0; k < r_doc.n.size(); k++) {
		const DNode &nd = r_doc.n[k];
		if (nd.kind != N_ELEMENT) {
			continue;
		}
		if (k != 0 && nd.parent == 0 && nd.tag == "svg" && r_doc.root == -1) {
			r_doc.root = k;
		}
		HashMap<String, int> type_counts;
		int count = 0;
		Vector<int> elem_kids;
		for (int c = 0; c < nd.kids.size(); c++) {
			if (r_doc.n[nd.kids[c]].kind == N_ELEMENT) {
				elem_kids.push_back(nd.kids[c]);
			}
		}
		for (int c = 0; c < elem_kids.size(); c++) {
			DNode &kid = r_doc.n.write[elem_kids[c]];
			kid.sib_index = c;
			kid.sib_count = elem_kids.size();
			kid.type_index = type_counts[kid.tag];
			type_counts[kid.tag] = kid.type_index + 1;
			count++;
		}
		for (int c = 0; c < elem_kids.size(); c++) {
			DNode &kid = r_doc.n.write[elem_kids[c]];
			kid.type_count = type_counts[kid.tag];
		}
	}
	return r_doc.root != -1;
}

String quote_attr_value(const String &p_value, char32_t &r_quote) {
	r_quote = '"';
	if (p_value.find_char('"') == -1) {
		return p_value;
	}
	if (p_value.find_char('\'') == -1) {
		r_quote = '\'';
		return p_value;
	}
	return p_value.replace("\"", "&quot;");
}

void serialize_node(const Doc &p_doc, int p_idx, String &r_out) {
	const DNode &nd = p_doc.n[p_idx];
	if (nd.dead) {
		return;
	}
	if (nd.kind != N_ELEMENT) {
		r_out += nd.text;
		return;
	}
	if (p_idx != 0) {
		r_out += "<" + nd.tag;
		for (int i = 0; i < nd.attrs.size(); i++) {
			const Attr &a = nd.attrs[i];
			r_out += " " + a.name;
			if (a.quote != 0) {
				char32_t q = a.quote;
				String v = a.value;
				// A value this pass rewrote may contain the quote character.
				if (v.find_char(q) != -1) {
					v = quote_attr_value(v, q);
				}
				const String qs = String::chr(q);
				r_out += "=" + qs + v + qs;
			}
		}
		if (nd.self_closing && nd.kids.is_empty()) {
			r_out += "/>";
			return;
		}
		r_out += ">";
	}
	for (int i = 0; i < nd.kids.size(); i++) {
		serialize_node(p_doc, nd.kids[i], r_out);
	}
	if (p_idx != 0) {
		r_out += "</" + nd.tag + ">";
	}
}

// ---------------------------------------------------------------------------
// Selectors
// ---------------------------------------------------------------------------

enum PseudoKind {
	P_ROOT,
	P_FIRST_CHILD,
	P_LAST_CHILD,
	P_ONLY_CHILD,
	P_NTH_CHILD,
	P_NTH_LAST_CHILD,
	P_FIRST_TYPE,
	P_LAST_TYPE,
	P_ONLY_TYPE,
	P_NTH_TYPE,
	P_NTH_LAST_TYPE,
	P_EMPTY,
	P_NOT,
	P_IS,
	P_NEVER, // dynamic state that a static render never has
};

struct Pseudo {
	int kind = P_NEVER;
	int a = 0;
	int b = 0;
	int list = -1;
};

struct AttrSel {
	String name;
	String val;
	int op = 0; // 0 exists, 1 =, 2 ~=, 3 |=, 4 ^=, 5 $=, 6 *=
	bool ci = false;
};

struct Compound {
	String tag;
	Vector<String> ids;
	Vector<String> classes;
	Vector<AttrSel> attrs;
	Vector<Pseudo> pseudos;
	char32_t comb = 0; // combinator that links this compound to the previous one
};

struct Selector {
	Vector<Compound> chain;
	int spec = 0;
	bool dead = false;
};

struct SelList {
	Vector<Selector> sels;
};

struct Decl {
	String prop;
	String value;
	bool important = false;
};

struct Rule {
	int list = -1;
	Vector<Decl> decls;
	int order = 0;
};

struct Sheet {
	Vector<SelList> lists;
	Vector<Rule> rules;
	String residual; // @font-face and friends, kept for the rasterizer
	HashMap<String, String> initials; // @property initial values
	int next_order = 0;
};

inline int spec_pack(int a, int b, int c) {
	return (MIN(a, 1023) << 20) | (MIN(b, 1023) << 10) | MIN(c, 1023);
}

String read_ident(const String &p_s, int &r_i) {
	const int st = r_i;
	while (r_i < p_s.length()) {
		const char32_t c = p_s[r_i];
		if (c == '\\' && r_i + 1 < p_s.length()) {
			r_i += 2;
			continue;
		}
		if (!is_ident_char(c)) {
			break;
		}
		r_i++;
	}
	String id = p_s.substr(st, r_i - st);
	if (id.find_char('\\') != -1) {
		String clean;
		for (int k = 0; k < id.length(); k++) {
			if (id[k] == '\\' && k + 1 < id.length()) {
				k++;
			}
			clean += String::chr(id[k]);
		}
		id = clean;
	}
	return id;
}

bool parse_nth(const String &p_arg, int &r_a, int &r_b) {
	String s = p_arg.strip_edges().to_lower().replace(" ", "");
	if (s == "odd") {
		r_a = 2;
		r_b = 1;
		return true;
	}
	if (s == "even") {
		r_a = 2;
		r_b = 0;
		return true;
	}
	const int npos = s.find_char('n');
	if (npos == -1) {
		if (!s.is_valid_int()) {
			return false;
		}
		r_a = 0;
		r_b = s.to_int();
		return true;
	}
	const String as = s.substr(0, npos);
	if (as.is_empty() || as == "+") {
		r_a = 1;
	} else if (as == "-") {
		r_a = -1;
	} else if (as.is_valid_int()) {
		r_a = as.to_int();
	} else {
		return false;
	}
	const String bs = s.substr(npos + 1);
	if (bs.is_empty()) {
		r_b = 0;
	} else if (bs.is_valid_int()) {
		r_b = bs.to_int();
	} else {
		return false;
	}
	return true;
}

bool parse_selector(const String &p_text, Sheet &p_sheet, Selector &r_out, Rep *r_rep);

// Parses a comma list into a SelList and returns its index (or -1).
int parse_selector_list(const String &p_text, Sheet &p_sheet, int &r_spec, Rep *r_rep) {
	SelList list;
	int spec = 0;
	const Vector<String> items = split_top(p_text, ',');
	for (int i = 0; i < items.size(); i++) {
		Selector s;
		if (parse_selector(items[i], p_sheet, s, r_rep) && !s.dead) {
			spec = MAX(spec, s.spec);
			list.sels.push_back(s);
		}
	}
	r_spec = spec;
	p_sheet.lists.push_back(list);
	return p_sheet.lists.size() - 1;
}

bool parse_selector(const String &p_in, Sheet &p_sheet, Selector &r_out, Rep *r_rep) {
	const String text = p_in.strip_edges();
	if (text.is_empty()) {
		return false;
	}
	int a = 0, b = 0, c = 0;
	int i = 0;
	const int n = text.length();
	char32_t pending = 0;
	Compound cur;
	bool have = false;
	auto flush = [&]() {
		if (have) {
			cur.comb = pending;
			r_out.chain.push_back(cur);
			cur = Compound();
			have = false;
			pending = 0;
		}
	};
	while (i < n) {
		const char32_t ch = text[i];
		if (is_ws(ch)) {
			while (i < n && is_ws(text[i])) {
				i++;
			}
			if (have && i < n && text[i] != '>' && text[i] != '+' && text[i] != '~') {
				flush();
				pending = ' ';
			}
			continue;
		}
		if (ch == '>' || ch == '+' || ch == '~') {
			flush();
			pending = ch;
			i++;
			continue;
		}
		have = true;
		if (ch == '*') {
			cur.tag = "*";
			i++;
		} else if (ch == '#') {
			i++;
			cur.ids.push_back(read_ident(text, i));
			a++;
		} else if (ch == '.') {
			i++;
			cur.classes.push_back(read_ident(text, i));
			b++;
		} else if (ch == '[') {
			const int e = text.find_char(']', i);
			if (e == -1) {
				return false;
			}
			String body = text.substr(i + 1, e - i - 1).strip_edges();
			AttrSel at;
			int k = 0;
			at.name = read_ident(body, k);
			while (k < body.length() && is_ws(body[k])) {
				k++;
			}
			if (k < body.length()) {
				static const char *ops[] = { "~=", "|=", "^=", "$=", "*=", "=", nullptr };
				static const int codes[] = { 2, 3, 4, 5, 6, 1 };
				for (int o = 0; ops[o]; o++) {
					if (body.substr(k, strlen(ops[o])) == ops[o]) {
						at.op = codes[o];
						k += (int)strlen(ops[o]);
						break;
					}
				}
				String v = body.substr(k).strip_edges();
				if (v.ends_with(" i") || v.ends_with(" I")) {
					at.ci = true;
					v = v.substr(0, v.length() - 2).strip_edges();
				}
				if (v.length() >= 2 && (v[0] == '"' || v[0] == '\'') && v[v.length() - 1] == v[0]) {
					v = v.substr(1, v.length() - 2);
				}
				at.val = v;
			}
			cur.attrs.push_back(at);
			b++;
			i = e + 1;
		} else if (ch == ':') {
			if (i + 1 < n && text[i + 1] == ':') {
				// Pseudo-elements do not exist on SVG elements.
				r_out.dead = true;
				return true;
			}
			i++;
			const String name = read_ident(text, i).to_lower();
			String arg;
			bool has_arg = false;
			if (i < n && text[i] == '(') {
				const int e = find_close_paren(text, i);
				if (e == -1) {
					return false;
				}
				arg = text.substr(i + 1, e - i - 1);
				has_arg = true;
				i = e + 1;
			}
			Pseudo ps;
			if (name == "root") {
				ps.kind = P_ROOT;
				b++;
			} else if (name == "first-child") {
				ps.kind = P_FIRST_CHILD;
				b++;
			} else if (name == "last-child") {
				ps.kind = P_LAST_CHILD;
				b++;
			} else if (name == "only-child") {
				ps.kind = P_ONLY_CHILD;
				b++;
			} else if (name == "first-of-type") {
				ps.kind = P_FIRST_TYPE;
				b++;
			} else if (name == "last-of-type") {
				ps.kind = P_LAST_TYPE;
				b++;
			} else if (name == "only-of-type") {
				ps.kind = P_ONLY_TYPE;
				b++;
			} else if (name == "empty") {
				ps.kind = P_EMPTY;
				b++;
			} else if ((name == "nth-child" || name == "nth-last-child") && has_arg) {
				if (arg.to_lower().find(" of ") != -1) {
					if (r_rep) {
						r_rep->add_unsupported("CSS selector :" + name + "(… of <selector>)");
					}
					r_out.dead = true;
					return true;
				}
				if (!parse_nth(arg, ps.a, ps.b)) {
					return false;
				}
				ps.kind = (name == "nth-child") ? P_NTH_CHILD : P_NTH_LAST_CHILD;
				b++;
			} else if ((name == "nth-of-type" || name == "nth-last-of-type") && has_arg) {
				if (!parse_nth(arg, ps.a, ps.b)) {
					return false;
				}
				ps.kind = (name == "nth-of-type") ? P_NTH_TYPE : P_NTH_LAST_TYPE;
				b++;
			} else if ((name == "not" || name == "is" || name == "matches" || name == "where" || name == "-webkit-any") && has_arg) {
				int sp = 0;
				ps.list = parse_selector_list(arg, p_sheet, sp, r_rep);
				ps.kind = (name == "not") ? P_NOT : P_IS;
				if (name == "where") {
					sp = 0;
				}
				// The specificity of the most specific argument.
				a += (sp >> 20) & 1023;
				b += (sp >> 10) & 1023;
				c += sp & 1023;
			} else if (name == "hover" || name == "focus" || name == "focus-within" || name == "focus-visible" || name == "active" || name == "visited" || name == "link" || name == "any-link" || name == "target" || name == "checked" || name == "disabled" || name == "enabled" || name == "required" || name == "optional" || name == "placeholder-shown") {
				ps.kind = P_NEVER;
				b++;
			} else if (name == "scope") {
				ps.kind = P_ROOT;
				b++;
			} else {
				if (r_rep) {
					r_rep->add_unsupported("CSS selector :" + name + (has_arg ? "()" : ""));
				}
				r_out.dead = true;
				return true;
			}
			cur.pseudos.push_back(ps);
		} else if (is_ident_char(ch) || ch == '\\') {
			cur.tag = read_ident(text, i);
			c++;
		} else {
			return false;
		}
	}
	flush();
	if (r_out.chain.is_empty()) {
		return false;
	}
	r_out.spec = spec_pack(a, b, c);
	return true;
}

// ---------------------------------------------------------------------------
// Selector matching
// ---------------------------------------------------------------------------

struct MatchCtx {
	const Doc *doc;
	const Sheet *sheet;
};

bool match_complex(const MatchCtx &m, const Selector &s, int ci, int node);

bool has_class(const DNode &n, const String &cls) {
	const int ai = attr_find(n, "class");
	if (ai == -1) {
		return false;
	}
	const String &v = n.attrs[ai].value;
	int i = 0;
	while (i < v.length()) {
		while (i < v.length() && is_ws(v[i])) {
			i++;
		}
		const int st = i;
		while (i < v.length() && !is_ws(v[i])) {
			i++;
		}
		if (i > st && v.substr(st, i - st) == cls) {
			return true;
		}
	}
	return false;
}

bool match_attr(const DNode &n, const AttrSel &s) {
	const int ai = attr_find(n, s.name);
	if (ai == -1) {
		return false;
	}
	if (s.op == 0) {
		return true;
	}
	String v = n.attrs[ai].value;
	String t = s.val;
	if (s.ci) {
		v = v.to_lower();
		t = t.to_lower();
	}
	switch (s.op) {
		case 1:
			return v == t;
		case 2: {
			if (t.is_empty()) {
				return false;
			}
			const Vector<String> words = split_ws_top(v);
			for (int i = 0; i < words.size(); i++) {
				if (words[i] == t) {
					return true;
				}
			}
			return false;
		}
		case 3:
			return v == t || v.begins_with(t + "-");
		case 4:
			return !t.is_empty() && v.begins_with(t);
		case 5:
			return !t.is_empty() && v.ends_with(t);
		case 6:
			return !t.is_empty() && v.find(t) != -1;
	}
	return false;
}

bool nth_match(int a, int b, int pos) {
	if (a == 0) {
		return pos == b;
	}
	const int d = pos - b;
	return d % a == 0 && d / a >= 0;
}

bool match_compound(const MatchCtx &m, const Compound &c, int ni) {
	const DNode &n = m.doc->n[ni];
	if (!c.tag.is_empty() && c.tag != "*" && c.tag != n.tag) {
		return false;
	}
	for (int i = 0; i < c.ids.size(); i++) {
		if (attr_get(n, "id") != c.ids[i]) {
			return false;
		}
	}
	for (int i = 0; i < c.classes.size(); i++) {
		if (!has_class(n, c.classes[i])) {
			return false;
		}
	}
	for (int i = 0; i < c.attrs.size(); i++) {
		if (!match_attr(n, c.attrs[i])) {
			return false;
		}
	}
	for (int i = 0; i < c.pseudos.size(); i++) {
		const Pseudo &p = c.pseudos[i];
		switch (p.kind) {
			case P_ROOT:
				if (n.parent != 0) {
					return false;
				}
				break;
			case P_FIRST_CHILD:
				if (n.sib_index != 0) {
					return false;
				}
				break;
			case P_LAST_CHILD:
				if (n.sib_index != n.sib_count - 1) {
					return false;
				}
				break;
			case P_ONLY_CHILD:
				if (n.sib_count != 1) {
					return false;
				}
				break;
			case P_NTH_CHILD:
				if (!nth_match(p.a, p.b, n.sib_index + 1)) {
					return false;
				}
				break;
			case P_NTH_LAST_CHILD:
				if (!nth_match(p.a, p.b, n.sib_count - n.sib_index)) {
					return false;
				}
				break;
			case P_FIRST_TYPE:
				if (n.type_index != 0) {
					return false;
				}
				break;
			case P_LAST_TYPE:
				if (n.type_index != n.type_count - 1) {
					return false;
				}
				break;
			case P_ONLY_TYPE:
				if (n.type_count != 1) {
					return false;
				}
				break;
			case P_NTH_TYPE:
				if (!nth_match(p.a, p.b, n.type_index + 1)) {
					return false;
				}
				break;
			case P_NTH_LAST_TYPE:
				if (!nth_match(p.a, p.b, n.type_count - n.type_index)) {
					return false;
				}
				break;
			case P_EMPTY: {
				for (int k = 0; k < n.kids.size(); k++) {
					const DNode &kid = m.doc->n[n.kids[k]];
					if (kid.kind == N_ELEMENT || (kid.kind == N_TEXT && !kid.text.strip_edges().is_empty())) {
						return false;
					}
				}
			} break;
			case P_IS:
			case P_NOT: {
				bool any = false;
				const SelList &list = m.sheet->lists[p.list];
				for (int k = 0; k < list.sels.size() && !any; k++) {
					any = match_complex(m, list.sels[k], list.sels[k].chain.size() - 1, ni);
				}
				if (p.kind == P_IS ? !any : any) {
					return false;
				}
			} break;
			default:
				return false;
		}
	}
	return true;
}

int prev_element_sibling(const Doc &d, int ni) {
	const DNode &n = d.n[ni];
	if (n.parent < 0) {
		return -1;
	}
	const Vector<int> &kids = d.n[n.parent].kids;
	int prev = -1;
	for (int i = 0; i < kids.size(); i++) {
		if (kids[i] == ni) {
			return prev;
		}
		if (d.n[kids[i]].kind == N_ELEMENT) {
			prev = kids[i];
		}
	}
	return -1;
}

bool match_complex(const MatchCtx &m, const Selector &s, int ci, int node) {
	if (!match_compound(m, s.chain[ci], node)) {
		return false;
	}
	if (ci == 0) {
		return true;
	}
	const char32_t comb = s.chain[ci].comb;
	const Doc &d = *m.doc;
	if (comb == '>') {
		const int p = d.n[node].parent;
		return p > 0 && match_complex(m, s, ci - 1, p);
	}
	if (comb == '+') {
		const int p = prev_element_sibling(d, node);
		return p != -1 && match_complex(m, s, ci - 1, p);
	}
	if (comb == '~') {
		int p = prev_element_sibling(d, node);
		while (p != -1) {
			if (match_complex(m, s, ci - 1, p)) {
				return true;
			}
			p = prev_element_sibling(d, p);
		}
		return false;
	}
	int p = d.n[node].parent;
	while (p > 0) {
		if (match_complex(m, s, ci - 1, p)) {
			return true;
		}
		p = d.n[p].parent;
	}
	return false;
}

// ---------------------------------------------------------------------------
// Media queries and @supports
// ---------------------------------------------------------------------------

struct MediaEval {
	String s;
	int i = 0;
	const Env *env = nullptr;
	Rep *rep = nullptr;
	bool supports = false; // @supports: every declaration test passes, as it does in a browser

	void ws() {
		while (i < s.length() && is_ws(s[i])) {
			i++;
		}
	}
	String word() {
		ws();
		const int st = i;
		while (i < s.length() && (is_alpha(s[i]) || s[i] == '-')) {
			i++;
		}
		return s.substr(st, i - st).to_lower();
	}
	bool peek_word(const char *p_w) {
		ws();
		const int len = (int)strlen(p_w);
		if (s.substr(i, len).to_lower() != p_w) {
			return false;
		}
		return i + len >= s.length() || !is_ident_char(s[i + len]);
	}

	bool parse_cond() {
		ws();
		if (peek_word("not")) {
			word();
			return !parse_unit();
		}
		bool result = parse_unit();
		while (true) {
			ws();
			if (peek_word("and")) {
				word();
				const bool rhs = parse_unit();
				result = result && rhs;
			} else if (peek_word("or")) {
				word();
				const bool rhs = parse_unit();
				result = result || rhs;
			} else {
				break;
			}
		}
		return result;
	}

	bool parse_unit() {
		ws();
		if (i >= s.length() || s[i] != '(') {
			if (supports && peek_word("selector")) {
				word();
				ws();
				if (i < s.length() && s[i] == '(') {
					const int e = find_close_paren(s, i);
					i = (e == -1) ? s.length() : e + 1;
				}
				return true;
			}
			i = s.length();
			return false;
		}
		const int e = find_close_paren(s, i);
		if (e == -1) {
			i = s.length();
			return false;
		}
		const String inner = s.substr(i + 1, e - i - 1).strip_edges();
		i = e + 1;
		if (inner.begins_with("(") || inner.to_lower().begins_with("not ")) {
			MediaEval sub;
			sub.s = inner;
			sub.env = env;
			sub.rep = rep;
			sub.supports = supports;
			return sub.parse_cond();
		}
		return supports ? true : feature(inner);
	}

	double length_of(const String &p_v, bool &r_ok) {
		Dim d;
		r_ok = parse_dim(p_v, d, *env, 16);
		if (!r_ok) {
			return 0;
		}
		if (d.kind != D_LEN && !(d.kind == D_NUM && d.v == 0)) {
			r_ok = false;
		}
		return d.v;
	}

	// Numeric value of a range-capable feature, or NaN when it is not one.
	double numeric_feature(const String &p_name) {
		const double w = env->viewport.x, h = env->viewport.y;
		if (p_name == "width") {
			return w;
		}
		if (p_name == "height") {
			return h;
		}
		if (p_name == "aspect-ratio") {
			return h > 0 ? w / h : 0;
		}
		if (p_name == "resolution") {
			return 1.0;
		}
		if (p_name == "color") {
			return 8.0;
		}
		if (p_name == "color-index" || p_name == "monochrome") {
			return 0.0;
		}
		return Math::NaN;
	}

	bool value_of(const String &p_feature, const String &p_text, double &r_v) {
		const String t = p_text.strip_edges();
		if (p_feature == "aspect-ratio") {
			const int sl = t.find_char('/');
			if (sl != -1) {
				const double d = t.substr(sl + 1).to_float();
				r_v = d != 0 ? t.substr(0, sl).to_float() / d : 0;
				return true;
			}
			r_v = t.to_float();
			return true;
		}
		if (p_feature == "resolution") {
			const String lt = t.to_lower();
			if (lt.ends_with("dppx") || lt.ends_with("x")) {
				r_v = lt.to_float();
				return true;
			}
			if (lt.ends_with("dpi")) {
				r_v = lt.to_float() / 96.0;
				return true;
			}
			return false;
		}
		if (p_feature == "width" || p_feature == "height") {
			bool ok = false;
			r_v = length_of(t, ok);
			return ok;
		}
		r_v = t.to_float();
		return true;
	}

	static bool compare(double a, const String &op, double b) {
		if (op == "<") {
			return a < b;
		}
		if (op == "<=") {
			return a <= b;
		}
		if (op == ">") {
			return a > b;
		}
		if (op == ">=") {
			return a >= b;
		}
		return Math::abs(a - b) < 1e-6;
	}

	bool feature(const String &p_inner) {
		const String inner = p_inner.strip_edges().to_lower();
		// Range syntax: (400px <= width <= 800px), (width >= 600px), (600px <= width).
		if (inner.find("<") != -1 || inner.find(">") != -1 || (inner.find("=") != -1 && inner.find(":") == -1)) {
			Vector<String> toks;
			Vector<String> ops;
			String cur;
			for (int k = 0; k < inner.length(); k++) {
				const char32_t c = inner[k];
				if (c == '<' || c == '>' || c == '=') {
					String op = String::chr(c);
					if (k + 1 < inner.length() && inner[k + 1] == '=' && c != '=') {
						op += "=";
						k++;
					}
					toks.push_back(cur.strip_edges());
					cur = String();
					ops.push_back(op);
				} else {
					cur += String::chr(c);
				}
			}
			toks.push_back(cur.strip_edges());
			auto is_feature_name = [&](const String &t) {
				return !Math::is_nan(numeric_feature(t));
			};
			if (toks.size() == 2) {
				const String &l = toks[0];
				const String &r = toks[1];
				if (is_feature_name(l)) {
					double v;
					if (!value_of(l, r, v)) {
						return false;
					}
					return compare(numeric_feature(l), ops[0], v);
				}
				if (is_feature_name(r)) {
					double v;
					if (!value_of(r, l, v)) {
						return false;
					}
					return compare(v, ops[0], numeric_feature(r));
				}
			} else if (toks.size() == 3 && is_feature_name(toks[1])) {
				double lo, hi;
				if (!value_of(toks[1], toks[0], lo) || !value_of(toks[1], toks[2], hi)) {
					return false;
				}
				const double cur_v = numeric_feature(toks[1]);
				return compare(lo, ops[0], cur_v) && compare(cur_v, ops[1], hi);
			}
			if (rep) {
				rep->add_unsupported("@media range: (" + p_inner + ")");
			}
			return false;
		}
		const int colon = inner.find_char(':');
		String name = (colon == -1) ? inner : inner.substr(0, colon).strip_edges();
		const String value = (colon == -1) ? String() : inner.substr(colon + 1).strip_edges();
		if (name == "prefers-color-scheme") {
			return value == (env->dark_color_scheme ? "dark" : "light");
		}
		if (name == "prefers-reduced-motion") {
			return value == "no-preference";
		}
		if (name == "prefers-contrast") {
			return value == "no-preference";
		}
		if (name == "prefers-reduced-transparency") {
			return value == "no-preference";
		}
		if (name == "orientation") {
			const bool landscape = env->viewport.x >= env->viewport.y;
			return value == (landscape ? "landscape" : "portrait");
		}
		if (name == "hover" || name == "any-hover") {
			return colon == -1 ? true : value == "hover";
		}
		if (name == "pointer" || name == "any-pointer") {
			return colon == -1 ? true : value == "fine";
		}
		if (name == "scripting") {
			return value == "none";
		}
		if (name == "update") {
			return value == "none";
		}
		if (name == "forced-colors") {
			return value == "none";
		}
		if (name == "inverted-colors") {
			return value == "none";
		}
		if (name == "display-mode") {
			return value == "browser";
		}
		if (name == "grid") {
			return value == "0";
		}
		String base = name;
		int mode = 0; // 0 exact, -1 max, +1 min
		if (name.begins_with("min-")) {
			base = name.substr(4);
			mode = 1;
		} else if (name.begins_with("max-")) {
			base = name.substr(4);
			mode = -1;
		}
		const double cur = numeric_feature(base);
		if (Math::is_nan(cur)) {
			if (rep) {
				rep->add_unsupported("@media feature (" + name + ")");
			}
			return false;
		}
		if (colon == -1) {
			return cur != 0;
		}
		double v;
		if (!value_of(base, value, v)) {
			return false;
		}
		return mode == 0 ? compare(cur, "=", v) : (mode > 0 ? cur >= v : cur <= v);
	}
};

bool eval_media_list(const String &p_query, const Env &p_env, Rep *r_rep) {
	const String q = p_query.strip_edges();
	if (q.is_empty()) {
		return true;
	}
	const Vector<String> parts = split_top(q, ',');
	for (int k = 0; k < parts.size(); k++) {
		MediaEval m;
		m.s = parts[k].strip_edges();
		m.env = &p_env;
		m.rep = r_rep;
		bool negate = false;
		if (m.peek_word("not")) {
			m.word();
			negate = true;
		} else if (m.peek_word("only")) {
			m.word();
		}
		bool result = true;
		m.ws();
		if (m.i < m.s.length() && m.s[m.i] != '(') {
			const String type = m.word();
			result = (type == "all" || type == "screen");
			m.ws();
			if (m.peek_word("and")) {
				m.word();
				result = m.parse_cond() && result;
			}
		} else {
			result = m.parse_cond();
		}
		if (negate) {
			result = !result;
		}
		if (result) {
			return true;
		}
	}
	return false;
}

bool eval_supports(const String &p_query) {
	MediaEval m;
	m.s = p_query.strip_edges();
	Env env;
	m.env = &env;
	m.supports = true;
	return m.parse_cond();
}

// ---------------------------------------------------------------------------
// Stylesheet parsing
// ---------------------------------------------------------------------------

Vector<Decl> parse_decls(const String &p_text) {
	Vector<Decl> out;
	const Vector<String> items = split_top(strip_css_comments(p_text), ';');
	for (int i = 0; i < items.size(); i++) {
		const String item = items[i].strip_edges();
		const int colon = item.find_char(':');
		if (colon <= 0) {
			continue;
		}
		Decl d;
		d.prop = item.substr(0, colon).strip_edges();
		if (!d.prop.begins_with("--")) {
			d.prop = d.prop.to_lower();
		}
		String v = item.substr(colon + 1).strip_edges();
		const int imp = v.to_lower().rfind("!important");
		if (imp != -1 && v.substr(imp + 10).strip_edges().is_empty()) {
			d.important = true;
			v = v.substr(0, imp).strip_edges();
		}
		d.value = v;
		out.push_back(d);
	}
	return out;
}

struct SheetParser {
	Sheet *sheet;
	const Env *env;
	Rep *rep;
	int depth = 0;

	// `p_parent` is the selector text of the enclosing rule, or empty at top level.
	void parse(const String &p_css, const String &p_parent) {
		if (depth > 12) {
			return;
		}
		depth++;
		const String css = strip_css_comments(p_css);
		const int n = css.length();
		int i = 0;
		while (i < n) {
			while (i < n && is_ws(css[i])) {
				i++;
			}
			if (i >= n) {
				break;
			}
			// Prelude up to `{` or `;`.
			int j = i;
			int paren = 0;
			char32_t quote = 0;
			while (j < n) {
				const char32_t c = css[j];
				if (quote) {
					if (c == '\\') {
						j++;
					} else if (c == quote) {
						quote = 0;
					}
				} else if (c == '"' || c == '\'') {
					quote = c;
				} else if (c == '(' || c == '[') {
					paren++;
				} else if (c == ')' || c == ']') {
					paren--;
				} else if (paren <= 0 && (c == '{' || c == ';')) {
					break;
				}
				j++;
			}
			const String prelude = css.substr(i, j - i).strip_edges();
			if (j >= n || css[j] == ';') {
				handle_statement(prelude);
				i = j + 1;
				continue;
			}
			// Block.
			int depth_b = 0;
			int k = j;
			quote = 0;
			for (; k < n; k++) {
				const char32_t c = css[k];
				if (quote) {
					if (c == '\\') {
						k++;
					} else if (c == quote) {
						quote = 0;
					}
					continue;
				}
				if (c == '"' || c == '\'') {
					quote = c;
				} else if (c == '{') {
					depth_b++;
				} else if (c == '}') {
					depth_b--;
					if (depth_b == 0) {
						break;
					}
				}
			}
			const String block = css.substr(j + 1, MAX(0, k - j - 1));
			i = k + 1;
			if (prelude.begins_with("@")) {
				handle_at_block(prelude, block, p_parent);
			} else {
				handle_rule(prelude, block, p_parent);
			}
		}
		depth--;
	}

	void handle_statement(const String &p_prelude) {
		const String low = p_prelude.to_lower();
		if (low.begins_with("@import")) {
			if (rep) {
				rep->add_unsupported("@import (external stylesheets are not loaded)");
			}
		}
		// @charset, @namespace, @layer a, b; and stray text are ignored.
	}

	void handle_at_block(const String &p_prelude, const String &p_block, const String &p_parent) {
		const int sp = p_prelude.find_char(' ');
		String name = (sp == -1) ? p_prelude.substr(1) : p_prelude.substr(1, sp - 1);
		const int par = name.find_char('(');
		if (par != -1) {
			name = name.substr(0, par);
		}
		name = name.to_lower();
		const String query = (sp == -1) ? String() : p_prelude.substr(sp + 1).strip_edges();
		if (name == "media") {
			if (eval_media_list(query, *env, rep)) {
				parse(p_block, p_parent);
			}
		} else if (name == "supports") {
			if (eval_supports(query)) {
				parse(p_block, p_parent);
			}
		} else if (name == "layer" || name == "scope" || name == "container") {
			if (name == "container" && rep) {
				rep->add_unsupported("@container queries");
			}
			parse(p_block, p_parent);
		} else if (name == "font-face") {
			sheet->residual += "@font-face{" + p_block.strip_edges() + "}";
		} else if (name == "property") {
			String prop = query;
			const Vector<Decl> decls = parse_decls(p_block);
			for (int k = 0; k < decls.size(); k++) {
				if (decls[k].prop == "initial-value" && prop.begins_with("--")) {
					sheet->initials[prop] = decls[k].value;
				}
			}
		} else if (name == "keyframes" || name == "-webkit-keyframes") {
			if (rep) {
				rep->add_unsupported("@keyframes (CSS animation)");
			}
		} else if (rep) {
			rep->add_unsupported("@" + name);
		}
	}

	void handle_rule(const String &p_prelude, const String &p_block, const String &p_parent) {
		// Split the block into declarations and nested rules.
		String decl_text;
		Vector<String> nested_preludes;
		Vector<String> nested_blocks;
		{
			const int n = p_block.length();
			int i = 0;
			int start = 0;
			int paren = 0;
			char32_t quote = 0;
			while (i < n) {
				const char32_t c = p_block[i];
				if (quote) {
					if (c == '\\') {
						i++;
					} else if (c == quote) {
						quote = 0;
					}
					i++;
					continue;
				}
				if (c == '"' || c == '\'') {
					quote = c;
				} else if (c == '(' || c == '[') {
					paren++;
				} else if (c == ')' || c == ']') {
					paren--;
				} else if (c == ';' && paren <= 0) {
					decl_text += p_block.substr(start, i - start + 1);
					start = i + 1;
				} else if (c == '{' && paren <= 0) {
					const String head = p_block.substr(start, i - start).strip_edges();
					int d = 0;
					int k = i;
					for (; k < n; k++) {
						if (p_block[k] == '{') {
							d++;
						} else if (p_block[k] == '}') {
							d--;
							if (d == 0) {
								break;
							}
						}
					}
					nested_preludes.push_back(head);
					nested_blocks.push_back(p_block.substr(i + 1, MAX(0, k - i - 1)));
					i = k;
					start = k + 1;
				}
				i++;
			}
			if (start < n) {
				decl_text += p_block.substr(start);
			}
		}

		// Resolve nesting into a flat selector text.
		String selector = p_prelude;
		if (!p_parent.is_empty()) {
			Vector<String> parts = split_top(p_prelude, ',');
			String joined;
			for (int k = 0; k < parts.size(); k++) {
				String s = parts[k].strip_edges();
				if (s.find_char('&') != -1) {
					s = s.replace("&", ":is(" + p_parent + ")");
				} else {
					s = ":is(" + p_parent + ") " + s;
				}
				joined += (k ? "," : "") + s;
			}
			selector = joined;
		} else if (selector.find_char('&') != -1) {
			selector = selector.replace("&", ":root");
		}

		Rule rule;
		int spec = 0;
		rule.list = parse_selector_list(selector, *sheet, spec, rep);
		rule.decls = parse_decls(decl_text);
		rule.order = sheet->next_order++;
		if (!sheet->lists[rule.list].sels.is_empty() && !rule.decls.is_empty()) {
			sheet->rules.push_back(rule);
		}
		for (int k = 0; k < nested_preludes.size(); k++) {
			if (nested_preludes[k].begins_with("@")) {
				// A conditional group inside a rule: its declarations belong to the parent.
				handle_nested_conditional(nested_preludes[k], nested_blocks[k], selector);
			} else {
				handle_rule(nested_preludes[k], nested_blocks[k], selector);
			}
		}
	}

	void handle_nested_conditional(const String &p_prelude, const String &p_block, const String &p_selector) {
		const int sp = p_prelude.find_char(' ');
		const String name = ((sp == -1) ? p_prelude.substr(1) : p_prelude.substr(1, sp - 1)).to_lower();
		const String query = (sp == -1) ? String() : p_prelude.substr(sp + 1).strip_edges();
		bool pass = false;
		if (name == "media") {
			pass = eval_media_list(query, *env, rep);
		} else if (name == "supports") {
			pass = eval_supports(query);
		} else if (name == "layer") {
			pass = true;
		}
		if (!pass) {
			return;
		}
		Rule rule;
		int spec = 0;
		rule.list = parse_selector_list(p_selector, *sheet, spec, rep);
		rule.decls = parse_decls(p_block);
		rule.order = sheet->next_order++;
		if (!sheet->lists[rule.list].sels.is_empty() && !rule.decls.is_empty()) {
			sheet->rules.push_back(rule);
		}
	}
};

// ---------------------------------------------------------------------------
// var() substitution
// ---------------------------------------------------------------------------

typedef HashMap<String, String> VarMap;

String subst_vars(const String &p_value, const VarMap &p_vars, int p_depth, bool &r_valid) {
	if (p_value.find("var(") == -1) {
		return p_value;
	}
	if (p_depth > 16) {
		r_valid = false;
		return p_value;
	}
	String v = p_value;
	int pos = 0;
	while (true) {
		const int at = v.find("var(", pos);
		if (at == -1) {
			break;
		}
		if (at > 0 && is_ident_char(v[at - 1])) {
			pos = at + 4;
			continue;
		}
		const int close = find_close_paren(v, at + 3);
		if (close == -1) {
			r_valid = false;
			break;
		}
		const String inner = v.substr(at + 4, close - at - 4);
		const Vector<String> pieces = split_top(inner, ',');
		const String name = pieces[0].strip_edges();
		String fallback;
		bool has_fallback = false;
		if (pieces.size() > 1) {
			has_fallback = true;
			fallback = inner.substr(inner.find_char(',') + 1).strip_edges();
		}
		String rep;
		if (p_vars.has(name)) {
			rep = subst_vars(p_vars[name], p_vars, p_depth + 1, r_valid);
		} else if (has_fallback) {
			rep = subst_vars(fallback, p_vars, p_depth + 1, r_valid);
		} else {
			r_valid = false;
			return p_value;
		}
		v = v.substr(0, at) + rep + v.substr(close + 1);
		pos = at + rep.length();
		if (!r_valid) {
			return p_value;
		}
	}
	return v;
}

// ---------------------------------------------------------------------------
// Transforms
// ---------------------------------------------------------------------------

struct Mat {
	double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
};

Mat mat_mul(const Mat &m, const Mat &n) {
	Mat r;
	r.a = m.a * n.a + m.c * n.b;
	r.b = m.b * n.a + m.d * n.b;
	r.c = m.a * n.c + m.c * n.d;
	r.d = m.b * n.c + m.d * n.d;
	r.e = m.a * n.e + m.c * n.f + m.e;
	r.f = m.b * n.e + m.d * n.f + m.f;
	return r;
}
Mat mat_translate(double x, double y) {
	Mat m;
	m.e = x;
	m.f = y;
	return m;
}
Mat mat_scale(double x, double y) {
	Mat m;
	m.a = x;
	m.d = y;
	return m;
}
Mat mat_rotate(double deg) {
	const double r = Math::deg_to_rad(deg);
	Mat m;
	m.a = Math::cos(r);
	m.b = Math::sin(r);
	m.c = -Math::sin(r);
	m.d = Math::cos(r);
	return m;
}
Mat mat_skew(double ax_deg, double ay_deg) {
	Mat m;
	m.c = Math::tan(Math::deg_to_rad(ax_deg));
	m.b = Math::tan(Math::deg_to_rad(ay_deg));
	return m;
}
String mat_to_string(const Mat &m) {
	return "matrix(" + fmt_num(m.a) + " " + fmt_num(m.b) + " " + fmt_num(m.c) + " " + fmt_num(m.d) + " " + fmt_num(m.e) + " " + fmt_num(m.f) + ")";
}

struct TransformCtx {
	const Env *env;
	double font;
	Rep *rep;
};

bool arg_length(const String &p_t, double &r_px, const TransformCtx &c) {
	Dim d;
	if (!parse_dim(p_t, d, *c.env, c.font)) {
		return false;
	}
	if (d.kind == D_LEN || d.kind == D_NUM) {
		r_px = d.v;
		return true;
	}
	return false;
}
bool arg_angle(const String &p_t, double &r_deg, const TransformCtx &c) {
	Dim d;
	if (!parse_dim(p_t, d, *c.env, c.font)) {
		return false;
	}
	if (d.kind == D_ANGLE || d.kind == D_NUM) {
		r_deg = d.v;
		return true;
	}
	return false;
}
bool arg_number(const String &p_t, double &r_v, const TransformCtx &c) {
	Dim d;
	if (!parse_dim(p_t, d, *c.env, c.font)) {
		return false;
	}
	if (d.kind == D_NUM) {
		r_v = d.v;
		return true;
	}
	if (d.kind == D_PCT) {
		r_v = d.v / 100.0;
		return true;
	}
	return false;
}

// Splits a function argument list on commas or whitespace.
Vector<String> split_args(const String &p_args) {
	Vector<String> out;
	const Vector<String> commas = split_top(p_args, ',');
	for (int i = 0; i < commas.size(); i++) {
		const Vector<String> sub = split_ws_top(commas[i]);
		for (int k = 0; k < sub.size(); k++) {
			out.push_back(sub[k]);
		}
	}
	return out;
}

bool parse_transform_list(const String &p_value, Mat &r_out, const TransformCtx &c) {
	r_out = Mat();
	const String v = p_value.strip_edges();
	int i = 0;
	const int n = v.length();
	while (i < n) {
		while (i < n && (is_ws(v[i]) || v[i] == ',')) {
			i++;
		}
		if (i >= n) {
			break;
		}
		const int st = i;
		while (i < n && (is_alpha(v[i]) || is_digit(v[i]))) {
			i++;
		}
		if (i >= n || v[i] != '(' || i == st) {
			return false;
		}
		const String name = v.substr(st, i - st);
		const int close = find_close_paren(v, i);
		if (close == -1) {
			return false;
		}
		const Vector<String> args = split_args(v.substr(i + 1, close - i - 1));
		i = close + 1;
		const String ln = name.to_lower();
		Mat m;
		double x = 0, y = 0, z = 0;
		const int na = args.size();
		if (ln == "translate" && (na == 1 || na == 2)) {
			if (!arg_length(args[0], x, c) || (na == 2 && !arg_length(args[1], y, c))) {
				return false;
			}
			m = mat_translate(x, y);
		} else if (ln == "translatex" && na == 1) {
			if (!arg_length(args[0], x, c)) {
				return false;
			}
			m = mat_translate(x, 0);
		} else if (ln == "translatey" && na == 1) {
			if (!arg_length(args[0], y, c)) {
				return false;
			}
			m = mat_translate(0, y);
		} else if ((ln == "translatez" && na == 1) || (ln == "translate3d" && na == 3)) {
			if (ln == "translate3d") {
				if (!arg_length(args[0], x, c) || !arg_length(args[1], y, c) || !arg_length(args[2], z, c)) {
					return false;
				}
			} else if (!arg_length(args[0], z, c)) {
				return false;
			}
			m = mat_translate(x, y);
		} else if (ln == "scale" && (na == 1 || na == 2)) {
			if (!arg_number(args[0], x, c)) {
				return false;
			}
			y = x;
			if (na == 2 && !arg_number(args[1], y, c)) {
				return false;
			}
			m = mat_scale(x, y);
		} else if (ln == "scalex" && na == 1) {
			if (!arg_number(args[0], x, c)) {
				return false;
			}
			m = mat_scale(x, 1);
		} else if (ln == "scaley" && na == 1) {
			if (!arg_number(args[0], y, c)) {
				return false;
			}
			m = mat_scale(1, y);
		} else if (ln == "scale3d" && na == 3) {
			if (!arg_number(args[0], x, c) || !arg_number(args[1], y, c)) {
				return false;
			}
			m = mat_scale(x, y);
		} else if ((ln == "rotate" || ln == "rotatez") && (na == 1 || na == 3)) {
			if (!arg_angle(args[0], x, c)) {
				return false;
			}
			m = mat_rotate(x);
			if (na == 3) {
				// SVG's rotate(a cx cy).
				double cx = 0, cy = 0;
				if (!arg_length(args[1], cx, c) || !arg_length(args[2], cy, c)) {
					return false;
				}
				m = mat_mul(mat_translate(cx, cy), mat_mul(m, mat_translate(-cx, -cy)));
			}
		} else if (ln == "skewx" && na == 1) {
			if (!arg_angle(args[0], x, c)) {
				return false;
			}
			m = mat_skew(x, 0);
		} else if (ln == "skewy" && na == 1) {
			if (!arg_angle(args[0], y, c)) {
				return false;
			}
			m = mat_skew(0, y);
		} else if (ln == "skew" && (na == 1 || na == 2)) {
			if (!arg_angle(args[0], x, c) || (na == 2 && !arg_angle(args[1], y, c))) {
				return false;
			}
			m = mat_skew(x, y);
		} else if (ln == "matrix" && na == 6) {
			double q[6];
			for (int k = 0; k < 6; k++) {
				if (!arg_number(args[k], q[k], c)) {
					return false;
				}
			}
			m.a = q[0];
			m.b = q[1];
			m.c = q[2];
			m.d = q[3];
			m.e = q[4];
			m.f = q[5];
		} else if (ln == "matrix3d" && na == 16) {
			double q[16];
			for (int k = 0; k < 16; k++) {
				if (!arg_number(args[k], q[k], c)) {
					return false;
				}
			}
			m.a = q[0];
			m.b = q[1];
			m.c = q[4];
			m.d = q[5];
			m.e = q[12];
			m.f = q[13];
		} else {
			if (c.rep) {
				c.rep->add_unsupported("CSS transform " + name + "()");
			}
			return false;
		}
		r_out = mat_mul(r_out, m);
	}
	return true;
}

// ---------------------------------------------------------------------------
// Property tables
// ---------------------------------------------------------------------------

const HashSet<String> &color_props() {
	static HashSet<String> s;
	if (s.is_empty()) {
		const char *names[] = { "fill", "stroke", "color", "stop-color", "flood-color", "lighting-color", "background-color", nullptr };
		for (int i = 0; names[i]; i++) {
			s.insert(names[i]);
		}
	}
	return s;
}

const HashSet<String> &opacity_props() {
	static HashSet<String> s;
	if (s.is_empty()) {
		const char *names[] = { "opacity", "fill-opacity", "stroke-opacity", "stop-opacity", "flood-opacity", nullptr };
		for (int i = 0; names[i]; i++) {
			s.insert(names[i]);
		}
	}
	return s;
}

const HashSet<String> &length_props() {
	static HashSet<String> s;
	if (s.is_empty()) {
		const char *names[] = { "stroke-width", "stroke-dashoffset", "stroke-miterlimit", nullptr };
		for (int i = 0; names[i]; i++) {
			s.insert(names[i]);
		}
	}
	return s;
}

const HashSet<String> &presentation_attrs() {
	static HashSet<String> s;
	if (s.is_empty()) {
		const char *names[] = { "fill", "stroke", "color", "stop-color", "flood-color", "lighting-color", "opacity", "fill-opacity", "stroke-opacity", "stop-opacity", "flood-opacity", "stroke-width", "stroke-dashoffset", "stroke-dasharray", "filter", nullptr };
		for (int i = 0; names[i]; i++) {
			s.insert(names[i]);
		}
	}
	return s;
}

const HashSet<String> &graphics_tags() {
	static HashSet<String> s;
	if (s.is_empty()) {
		const char *names[] = { "path", "rect", "circle", "ellipse", "line", "polyline", "polygon", "text", "image", "use", nullptr };
		for (int i = 0; names[i]; i++) {
			s.insert(names[i]);
		}
	}
	return s;
}

// CSS properties that SVG exposes as geometry attributes, and the elements that take them.
bool geometry_prop_applies(const String &p_prop, const String &p_tag) {
	if (p_prop == "x" || p_prop == "y" || p_prop == "width" || p_prop == "height") {
		return p_tag == "rect" || p_tag == "image" || p_tag == "svg" || p_tag == "use" || p_tag == "foreignObject";
	}
	if (p_prop == "cx" || p_prop == "cy") {
		return p_tag == "circle" || p_tag == "ellipse";
	}
	if (p_prop == "r") {
		return p_tag == "circle";
	}
	if (p_prop == "rx" || p_prop == "ry") {
		return p_tag == "rect" || p_tag == "ellipse";
	}
	return false;
}

bool is_unsupported_property(const String &p_prop, const String &p_value, String &r_label) {
	static const char *names[] = { "backdrop-filter", "animation", "animation-name", "animation-duration", "mask-image", "mask-border", "offset-path", "vector-effect", "shape-inside", "mask-composite", "mask-mode", nullptr };
	for (int i = 0; names[i]; i++) {
		if (p_prop == names[i]) {
			if (p_prop.begins_with("animation")) {
				r_label = "CSS animation (" + p_prop + ")";
			} else if (p_prop == "vector-effect") {
				if (p_value.strip_edges() == "none") {
					return false;
				}
				r_label = "CSS vector-effect";
			} else {
				r_label = "CSS property " + p_prop;
			}
			return true;
		}
	}
	if (p_prop == "clip-path") {
		const String v = p_value.strip_edges().to_lower();
		if (v.begins_with("circle(") || v.begins_with("ellipse(") || v.begins_with("inset(") || v.begins_with("polygon(") || v.begins_with("path(") || v.begins_with("rect(") || v.begins_with("xywh(") || v.begins_with("shape(")) {
			r_label = "CSS clip-path basic shapes (only url(#id) is supported)";
			return true;
		}
	}
	return false;
}

// ---------------------------------------------------------------------------
// The resolver
// ---------------------------------------------------------------------------

struct State {
	int vars = 0;
	double font = 16;
	bool hidden = false;
};

struct Cand {
	int imp = 0;
	int spec = 0;
	int order = 0;
	int seq = 0;
	String prop;
	String value;
};

struct CandLess {
	bool operator()(const Cand &a, const Cand &b) const {
		if (a.imp != b.imp) {
			return a.imp < b.imp;
		}
		if (a.spec != b.spec) {
			return a.spec < b.spec;
		}
		if (a.order != b.order) {
			return a.order < b.order;
		}
		return a.seq < b.seq;
	}
};

struct RuleSel {
	int rule;
	int sel;
};

struct Resolver {
	Doc doc;
	Sheet sheet;
	Env env;
	Rep *rep = nullptr;
	Vector<VarMap *> var_sets; // owned
	~Resolver() {
		for (int i = 0; i < var_sets.size(); i++) {
			memdelete(var_sets[i]);
		}
	}
	int add_vars(const VarMap &p_vars) {
		VarMap *m = memnew(VarMap);
		*m = p_vars;
		var_sets.push_back(m);
		return var_sets.size() - 1;
	}
	HashMap<String, Vector<RuleSel>> index;
	Vector<String> filter_defs;
	HashMap<String, String> filter_ids;
	Rect2 view_box;
	int seq = 0;

	String selector_key(const Selector &s) {
		const Compound &last = s.chain[s.chain.size() - 1];
		if (!last.ids.is_empty()) {
			return "#" + last.ids[0];
		}
		if (!last.classes.is_empty()) {
			return "." + last.classes[0];
		}
		if (!last.tag.is_empty() && last.tag != "*") {
			return last.tag;
		}
		return "*";
	}

	void build_index() {
		for (int r = 0; r < sheet.rules.size(); r++) {
			const SelList &list = sheet.lists[sheet.rules[r].list];
			for (int s = 0; s < list.sels.size(); s++) {
				RuleSel rs;
				rs.rule = r;
				rs.sel = s;
				index[selector_key(list.sels[s])].push_back(rs);
			}
		}
	}

	void compute_view_box() {
		const DNode &root = doc.n[doc.root];
		Rect2 vb(0, 0, env.viewport.x, env.viewport.y);
		const String vbs = attr_get(root, "viewBox");
		if (!vbs.is_empty()) {
			const Vector<String> p = split_args(vbs);
			if (p.size() == 4) {
				vb = Rect2(p[0].to_float(), p[1].to_float(), p[2].to_float(), p[3].to_float());
			}
		} else {
			Dim w, h;
			Env e;
			if (parse_dim(attr_get(root, "width"), w, e, 16) && parse_dim(attr_get(root, "height"), h, e, 16) && w.kind != D_PCT && h.kind != D_PCT) {
				vb = Rect2(0, 0, w.v, h.v);
			}
		}
		view_box = vb;
	}

	// Cascades every matching rule and the inline style into one ordered declaration list.
	void cascade(int ni, Vector<String> &r_order, HashMap<String, String> &r_values, bool &r_had_rules) {
		const MatchCtx mc = { &doc, &sheet };
		const DNode &el = doc.n[ni];
		Vector<Cand> cands;
		Vector<const Vector<RuleSel> *> buckets;
		auto add_bucket = [&](const String &key) {
			HashMap<String, Vector<RuleSel>>::Iterator it = index.find(key);
			if (it) {
				buckets.push_back(&it->value);
			}
		};
		add_bucket("*");
		add_bucket(el.tag);
		const String id = attr_get(el, "id");
		if (!id.is_empty()) {
			add_bucket("#" + id);
		}
		const Vector<String> classes = split_ws_top(attr_get(el, "class"));
		for (int i = 0; i < classes.size(); i++) {
			add_bucket("." + classes[i]);
		}
		r_had_rules = false;
		for (int b = 0; b < buckets.size(); b++) {
			const Vector<RuleSel> &bucket = *buckets[b];
			for (int k = 0; k < bucket.size(); k++) {
				const Rule &rule = sheet.rules[bucket[k].rule];
				const Selector &sel = sheet.lists[rule.list].sels[bucket[k].sel];
				if (!match_complex(mc, sel, sel.chain.size() - 1, ni)) {
					continue;
				}
				r_had_rules = true;
				for (int d = 0; d < rule.decls.size(); d++) {
					Cand c;
					c.imp = rule.decls[d].important ? 1 : 0;
					c.spec = sel.spec;
					c.order = rule.order;
					c.seq = seq++;
					c.prop = rule.decls[d].prop;
					c.value = rule.decls[d].value;
					cands.push_back(c);
				}
			}
		}
		const String inline_style = attr_get(el, "style");
		if (!inline_style.is_empty()) {
			const Vector<Decl> decls = parse_decls(xml_unescape(inline_style));
			for (int d = 0; d < decls.size(); d++) {
				Cand c;
				c.imp = decls[d].important ? 1 : 0;
				c.spec = 1 << 30;
				c.order = 0;
				c.seq = seq++;
				c.prop = decls[d].prop;
				c.value = decls[d].value;
				cands.push_back(c);
			}
		}
		cands.sort_custom<CandLess>();
		for (int i = 0; i < cands.size(); i++) {
			if (!r_values.has(cands[i].prop)) {
				r_order.push_back(cands[i].prop);
			}
			r_values[cands[i].prop] = cands[i].value;
		}
	}

	// var() -> math -> colors, for one value. False when the value is invalid.
	bool finalize_value(const String &p_prop, const String &p_in, const VarMap &p_vars, const State &p_state, String &r_out) {
		bool valid = true;
		String v = subst_vars(p_in, p_vars, 0, valid);
		if (!valid) {
			if (rep) {
				rep->add_warning("var() without a value in " + p_prop + ": " + p_in);
			}
			return false;
		}
		v = replace_math(v, env, p_state.font, rep);
		v = normalize_colors(v, env, rep);
		r_out = v.strip_edges();
		return true;
	}

	String normalize_opacity(const String &p_v) {
		Dim d;
		if (parse_dim(p_v, d, env, 16) && d.kind == D_PCT) {
			return fmt_num(d.v / 100.0);
		}
		return p_v;
	}

	String normalize_length(const String &p_v, double p_font) {
		const String t = p_v.strip_edges();
		Dim d;
		if (parse_dim(t, d, env, p_font) && d.kind == D_LEN) {
			return fmt_num(d.v);
		}
		return p_v;
	}

	String normalize_dasharray(const String &p_v, double p_font) {
		if (p_v.strip_edges().to_lower() == "none") {
			return p_v;
		}
		String out;
		const Vector<String> items = split_args(p_v);
		for (int i = 0; i < items.size(); i++) {
			Dim d;
			if (parse_dim(items[i], d, env, p_font) && d.kind == D_LEN) {
				out += (i ? " " : "") + fmt_num(d.v);
			} else {
				out += (i ? " " : "") + items[i];
			}
		}
		return out;
	}

	bool simple_bbox(const DNode &el, Rect2 &r_box) {
		auto num = [&](const char *n, double &v) {
			const String s = attr_get(el, n);
			if (s.is_empty()) {
				v = 0;
				return true;
			}
			Dim d;
			Env e;
			if (!parse_dim(s, d, e, 16) || d.kind == D_PCT) {
				return false;
			}
			v = d.v;
			return true;
		};
		double x = 0, y = 0, w = 0, h = 0, cx = 0, cy = 0, r = 0, rx = 0, ry = 0, x1 = 0, y1 = 0, x2 = 0, y2 = 0;
		if (el.tag == "rect" || el.tag == "image") {
			if (!num("x", x) || !num("y", y) || !num("width", w) || !num("height", h)) {
				return false;
			}
			r_box = Rect2(x, y, w, h);
			return true;
		}
		if (el.tag == "circle") {
			if (!num("cx", cx) || !num("cy", cy) || !num("r", r)) {
				return false;
			}
			r_box = Rect2(cx - r, cy - r, 2 * r, 2 * r);
			return true;
		}
		if (el.tag == "ellipse") {
			if (!num("cx", cx) || !num("cy", cy) || !num("rx", rx) || !num("ry", ry)) {
				return false;
			}
			r_box = Rect2(cx - rx, cy - ry, 2 * rx, 2 * ry);
			return true;
		}
		if (el.tag == "line") {
			if (!num("x1", x1) || !num("y1", y1) || !num("x2", x2) || !num("y2", y2)) {
				return false;
			}
			r_box = Rect2(MIN(x1, x2), MIN(y1, y2), Math::abs(x2 - x1), Math::abs(y2 - y1));
			return true;
		}
		return false;
	}

	bool resolve_origin(const String &p_value, const Rect2 &p_box, const State &p_state, double &r_x, double &r_y) {
		Vector<String> toks = split_ws_top(p_value.strip_edges().to_lower());
		if (toks.is_empty() || toks.size() > 3) {
			return false;
		}
		if (toks.size() == 3) {
			toks.resize(2); // z offset is irrelevant in 2D
		}
		if (toks.size() == 1) {
			const String t = toks[0];
			toks.push_back((t == "top" || t == "bottom") ? "center" : "center");
			if (t == "top" || t == "bottom") {
				toks.write[1] = toks[0];
				toks.write[0] = "center";
			}
		}
		if (toks[0] == "top" || toks[0] == "bottom" || toks[1] == "left" || toks[1] == "right") {
			const String t = toks[0];
			toks.write[0] = toks[1];
			toks.write[1] = t;
		}
		double out[2];
		for (int k = 0; k < 2; k++) {
			const String &t = toks[k];
			const double ext = (k == 0) ? p_box.size.x : p_box.size.y;
			const double org = (k == 0) ? p_box.position.x : p_box.position.y;
			if (t == "left" || t == "top") {
				out[k] = org;
			} else if (t == "center") {
				out[k] = org + ext / 2.0;
			} else if (t == "right" || t == "bottom") {
				out[k] = org + ext;
			} else {
				Dim d;
				if (!parse_dim(t, d, env, p_state.font)) {
					return false;
				}
				if (d.kind == D_PCT) {
					out[k] = org + ext * d.v / 100.0;
				} else if (d.kind == D_LEN || d.kind == D_NUM) {
					out[k] = org + d.v;
				} else {
					return false;
				}
			}
		}
		r_x = out[0];
		r_y = out[1];
		return true;
	}

	// Rewrites `filter: blur(..) ...` as a reference to an <feGaussianBlur> filter.
	// Returns the new value and any opacity factor the list contributed.
	String process_filter(const String &p_value, double p_font, double &r_opacity_mul) {
		r_opacity_mul = 1.0;
		const String v = p_value.strip_edges();
		if (v.is_empty() || v.to_lower() == "none" || v.to_lower().begins_with("url(")) {
			return v;
		}
		double variance = 0;
		bool any_blur = false;
		String keep_url;
		int i = 0;
		const int n = v.length();
		while (i < n) {
			while (i < n && is_ws(v[i])) {
				i++;
			}
			if (i >= n) {
				break;
			}
			const int st = i;
			while (i < n && (is_alpha(v[i]) || v[i] == '-')) {
				i++;
			}
			if (i >= n || v[i] != '(' || i == st) {
				if (rep) {
					rep->add_unsupported("CSS filter: " + v);
				}
				return String();
			}
			const String name = v.substr(st, i - st).to_lower();
			const int close = find_close_paren(v, i);
			if (close == -1) {
				return String();
			}
			const String arg = v.substr(i + 1, close - i - 1).strip_edges();
			i = close + 1;
			if (name == "blur") {
				Dim d;
				if (arg.is_empty()) {
					continue;
				}
				if (parse_dim(arg, d, env, p_font) && (d.kind == D_LEN || (d.kind == D_NUM && d.v == 0))) {
					variance += d.v * d.v;
					any_blur = true;
				} else if (rep) {
					rep->add_unsupported("CSS filter blur(" + arg + ")");
				}
			} else if (name == "opacity") {
				Dim d;
				if (parse_dim(arg, d, env, 16) && (d.kind == D_PCT || d.kind == D_NUM)) {
					r_opacity_mul *= (d.kind == D_PCT) ? d.v / 100.0 : d.v;
				}
			} else if (name == "url") {
				keep_url = v.substr(st, close - st + 1);
			} else if (rep) {
				rep->add_unsupported("CSS filter " + name + "() (only blur() and opacity() are supported)");
			}
		}
		if (!any_blur) {
			return keep_url;
		}
		const String sd = fmt_num(Math::sqrt(variance));
		if (!filter_ids.has(sd)) {
			const String id = "__wsf" + itos(filter_ids.size());
			filter_ids[sd] = id;
			// CSS filter functions are not clipped to the element's box the way an
			// SVG filter region is, so give the blur generous room to spread.
			filter_defs.push_back("<filter id=\"" + id + "\" x=\"-100%\" y=\"-100%\" width=\"300%\" height=\"300%\"><feGaussianBlur stdDeviation=\"" + sd + "\"/></filter>");
		}
		return "url(#" + filter_ids[sd] + ")";
	}

	void process(int ni, const State &p_parent) {
		DNode &el0 = doc.n.write[ni];
		if (el0.kind != N_ELEMENT || el0.dead) {
			return;
		}
		const String tag = el0.tag;

		Vector<String> order;
		HashMap<String, String> win;
		bool had_rules = false;
		cascade(ni, order, win, had_rules);

		// Custom properties first: they feed everything else on this element.
		State st = p_parent;
		bool new_vars = false;
		VarMap local;
		for (int i = 0; i < order.size(); i++) {
			if (order[i].begins_with("--")) {
				if (!new_vars) {
					local = *var_sets[st.vars];
					new_vars = true;
				}
				local[order[i]] = win[order[i]];
			}
		}
		if (new_vars) {
			st.vars = add_vars(local);
		}
		const VarMap &vars = *var_sets[st.vars];

		// font-size sets the em for this element and its descendants.
		{
			String fs = win.has("font-size") ? win["font-size"] : attr_get(doc.n[ni], "font-size");
			if (!fs.is_empty()) {
				String fv;
				if (finalize_value("font-size", fs, vars, st, fv)) {
					Dim d;
					if (parse_dim(fv, d, env, p_parent.font)) {
						if (d.kind == D_PCT) {
							st.font = p_parent.font * d.v / 100.0;
						} else if (d.kind == D_LEN || d.kind == D_NUM) {
							st.font = d.v;
						}
					}
				}
			}
		}

		Vector<Decl> out_style;
		HashMap<String, String> attr_out; // geometry and transform written as attributes
		HashSet<String> attr_remove_set;
		bool changed = had_rules || new_vars;
		double opacity_mul = 1.0;

		String t_transform, t_translate, t_rotate, t_scale, t_origin, t_box;
		bool has_t = false;

		for (int i = 0; i < order.size(); i++) {
			const String &prop = order[i];
			if (prop.begins_with("--")) {
				continue;
			}
			String value;
			if (!finalize_value(prop, win[prop], vars, st, value)) {
				changed = true;
				continue;
			}
			String label;
			if (is_unsupported_property(prop, value, label) && rep) {
				rep->add_unsupported(label);
			}
			if (prop == "transform") {
				t_transform = value;
				has_t = true;
				continue;
			}
			if (prop == "translate") {
				t_translate = value;
				has_t = true;
				continue;
			}
			if (prop == "rotate") {
				t_rotate = value;
				has_t = true;
				continue;
			}
			if (prop == "scale") {
				t_scale = value;
				has_t = true;
				continue;
			}
			if (prop == "transform-origin") {
				t_origin = value;
				continue;
			}
			if (prop == "transform-box") {
				t_box = value.to_lower();
				continue;
			}
			if (prop == "visibility") {
				const String lv = value.to_lower();
				if (lv == "hidden" || lv == "collapse") {
					st.hidden = true;
				} else if (lv == "visible") {
					st.hidden = false;
				}
				changed = true;
				continue;
			}
			if (prop == "filter") {
				double om = 1.0;
				const String nf = process_filter(value, st.font, om);
				opacity_mul *= om;
				if (!nf.is_empty()) {
					Decl d;
					d.prop = "filter";
					d.value = nf;
					out_style.push_back(d);
				} else {
					attr_remove_set.insert("filter");
				}
				changed = true;
				continue;
			}
			if (prop == "d" && value.to_lower().begins_with("path(") && tag == "path") {
				const int open = value.find_char('(');
				const int close = find_close_paren(value, open);
				String path = value.substr(open + 1, close - open - 1).strip_edges();
				if (path.length() >= 2 && (path[0] == '"' || path[0] == '\'')) {
					path = path.substr(1, path.length() - 2);
				}
				attr_out["d"] = path;
				changed = true;
				continue;
			}
			// The outer <svg> is sized by the host (WebSVG writes its width and height);
			// CSS geometry there is box layout, not an attribute.
			if (ni != doc.root && geometry_prop_applies(prop, tag)) {
				attr_out[prop] = normalize_length(value, st.font);
				changed = true;
				continue;
			}
			if (opacity_props().has(prop)) {
				value = normalize_opacity(value);
			} else if (length_props().has(prop)) {
				value = normalize_length(value, st.font);
			} else if (prop == "stroke-dasharray") {
				value = normalize_dasharray(value, st.font);
			}
			Decl d;
			d.prop = prop;
			d.value = value;
			out_style.push_back(d);
		}

		// Presentation attributes may carry the same functions as CSS does.
		for (int a = 0; a < doc.n[ni].attrs.size(); a++) {
			const Attr at = doc.n[ni].attrs[a];
			if (!presentation_attrs().has(at.name) || win.has(at.name)) {
				continue;
			}
			const String raw = xml_unescape(at.value);
			if (raw.find("(") == -1 && !(raw.begins_with("#") && (raw.length() == 5 || raw.length() == 9)) && raw.to_lower() != "currentcolor") {
				if (at.name == "visibility") {
					continue;
				}
				if (opacity_props().has(at.name) && raw.ends_with("%")) {
					attr_out[at.name] = normalize_opacity(raw);
				}
				continue;
			}
			if (raw.to_lower().begins_with("url(") && raw.find(" ") == -1) {
				continue;
			}
			String value;
			if (!finalize_value(at.name, raw, vars, st, value)) {
				continue;
			}
			if (at.name == "filter") {
				double om = 1.0;
				const String nf = process_filter(value, st.font, om);
				opacity_mul *= om;
				value = nf;
				if (value.is_empty()) {
					attr_remove_set.insert("filter");
					continue;
				}
			}
			if (opacity_props().has(at.name)) {
				value = normalize_opacity(value);
			}
			attr_out[at.name] = value;
		}
		{
			const String vis = attr_get(doc.n[ni], "visibility").to_lower();
			if (!win.has("visibility") && !vis.is_empty()) {
				if (vis == "hidden" || vis == "collapse") {
					st.hidden = true;
				} else if (vis == "visible") {
					st.hidden = false;
				}
			}
		}
		if (st.hidden && graphics_tags().has(tag)) {
			Decl d;
			d.prop = "display";
			d.value = "none";
			out_style.push_back(d);
			changed = true;
		}
		if (opacity_mul != 1.0) {
			double base = 1.0;
			for (int k = 0; k < out_style.size(); k++) {
				if (out_style[k].prop == "opacity") {
					base = out_style[k].value.to_float();
					out_style.remove_at(k);
					break;
				}
			}
			Decl d;
			d.prop = "opacity";
			d.value = fmt_num(base * opacity_mul);
			out_style.push_back(d);
		}

		// CSS transforms become a single SVG matrix(), around the right origin.
		if (has_t) {
			TransformCtx tc = { &env, st.font, rep };
			Mat total;
			bool ok = true;
			auto apply = [&](const String &v, int which) {
				if (v.is_empty() || v.to_lower() == "none") {
					return;
				}
				Mat m;
				if (which == 0) {
					if (!parse_transform_list(v, m, tc)) {
						ok = false;
						return;
					}
				} else {
					const Vector<String> a = split_args(v);
					double x = 0, y = 0;
					if (which == 1) {
						if (a.is_empty() || a.size() > 3 || !arg_length(a[0], x, tc) || (a.size() > 1 && !arg_length(a[1], y, tc))) {
							ok = false;
							return;
						}
						m = mat_translate(x, y);
					} else if (which == 2) {
						if (a.size() != 1 || !arg_angle(a[0], x, tc)) {
							ok = false;
							if (rep) {
								rep->add_unsupported("CSS rotate with an axis");
							}
							return;
						}
						m = mat_rotate(x);
					} else {
						if (a.is_empty() || a.size() > 3 || !arg_number(a[0], x, tc)) {
							ok = false;
							return;
						}
						y = x;
						if (a.size() > 1 && !arg_number(a[1], y, tc)) {
							ok = false;
							return;
						}
						m = mat_scale(x, y);
					}
				}
				total = mat_mul(total, m);
			};
			// Order is fixed by the spec: translate, rotate, scale, then transform.
			apply(t_translate, 1);
			apply(t_rotate, 2);
			apply(t_scale, 3);
			apply(t_transform, 0);
			if (ok) {
				double ox = 0, oy = 0;
				if (!t_origin.is_empty()) {
					Rect2 box = view_box;
					bool box_ok = true;
					if (t_box == "fill-box" || t_box == "stroke-box" || t_box == "content-box" || t_box == "border-box") {
						DNode tmp = doc.n[ni];
						for (HashMap<String, String>::Iterator it = attr_out.begin(); it != attr_out.end(); ++it) {
							attr_set(tmp, it->key, it->value);
						}
						box_ok = simple_bbox(tmp, box);
						if (!box_ok && rep) {
							rep->add_unsupported("CSS transform-box: " + t_box + " on <" + tag + "> (bounding box is not computed)");
						}
					}
					if (!box_ok || !resolve_origin(t_origin, box, st, ox, oy)) {
						if (box_ok && rep) {
							rep->add_unsupported("CSS transform-origin: " + t_origin);
						}
						ox = 0;
						oy = 0;
					}
				}
				if (ox != 0 || oy != 0) {
					total = mat_mul(mat_translate(ox, oy), mat_mul(total, mat_translate(-ox, -oy)));
				}
				const bool is_none = (t_transform.to_lower() == "none" || t_transform.is_empty()) && t_translate.is_empty() && t_rotate.is_empty() && t_scale.is_empty();
				if (is_none) {
					attr_remove_set.insert("transform");
				} else {
					attr_out["transform"] = mat_to_string(total);
				}
			} else {
				// Leave it for the rasterizer's own parser.
				if (!t_transform.is_empty()) {
					Decl d;
					d.prop = "transform";
					d.value = t_transform;
					out_style.push_back(d);
				}
			}
			changed = true;
		}

		// Write the result back.
		DNode &el = doc.n.write[ni];
		if (!attr_out.is_empty()) {
			changed = true;
		}
		for (HashMap<String, String>::Iterator it = attr_out.begin(); it != attr_out.end(); ++it) {
			attr_set(el, it->key, it->value);
		}
		for (HashSet<String>::Iterator it = attr_remove_set.begin(); it != attr_remove_set.end(); ++it) {
			attr_remove(el, *it);
		}
		String style_text;
		for (int i = 0; i < out_style.size(); i++) {
			style_text += out_style[i].prop + ":" + out_style[i].value + ";";
		}
		const String orig_style = attr_get(el, "style");
		bool rewrite = changed;
		if (!rewrite && !orig_style.is_empty()) {
			String orig_norm;
			const Vector<Decl> od = parse_decls(xml_unescape(orig_style));
			for (int i = 0; i < od.size(); i++) {
				orig_norm += od[i].prop + ":" + od[i].value + ";";
			}
			rewrite = (orig_norm != style_text);
		}
		if (rewrite) {
			if (style_text.is_empty()) {
				attr_remove(el, "style");
			} else {
				attr_set(el, "style", style_text);
			}
		}

		const Vector<int> kids = el.kids;
		for (int k = 0; k < kids.size(); k++) {
			process(kids[k], st);
		}
	}
};

bool style_type_is_css(const DNode &n) {
	const String t = attr_get(n, "type").strip_edges().to_lower();
	return t.is_empty() || t == "text/css";
}

String style_text_of(const Doc &d, const DNode &n) {
	String out;
	for (int i = 0; i < n.kids.size(); i++) {
		const DNode &k = d.n[n.kids[i]];
		if (k.kind == N_TEXT) {
			out += k.text;
		} else if (k.kind == N_RAW && k.text.begins_with("<![CDATA[")) {
			out += k.text.substr(9, k.text.length() - 12);
		}
	}
	return xml_unescape(out);
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool WebSVGStyle::needs_resolution(const String &p_markup) {
	static const char *needles[] = {
		"<style", "style=", "var(", "calc(", "min(", "max(", "clamp(", "color-mix(", "light-dark(",
		"oklch(", "oklab(", "lab(", "lch(", "hwb(", "color(", "rgb(", "rgba(", "hsl(", "hsla(",
		"filter=", nullptr
	};
	for (int i = 0; needles[i]; i++) {
		if (p_markup.find(needles[i]) != -1) {
			return true;
		}
	}
	return false;
}

bool WebSVGStyle::parse_color(const String &p_value, Color &r_color, const WebSVGStyleEnv &p_env) {
	RGBA c;
	if (!parse_color_rec(p_value, c, p_env, 0)) {
		return false;
	}
	r_color = Color(c.r, c.g, c.b, c.a);
	return true;
}

String WebSVGStyle::resolve(const String &p_markup, const WebSVGStyleEnv &p_env, WebSVGStyleReport *r_report) {
	Resolver rs;
	rs.env = p_env;
	rs.rep = r_report;
	if (!parse_doc(p_markup, rs.doc)) {
		if (r_report) {
			r_report->add_warning("CSS was not resolved: the document is not well-formed enough to analyze.");
		}
		return p_markup;
	}
	rs.compute_view_box();

	// Gather and remove every stylesheet.
	SheetParser sp;
	sp.sheet = &rs.sheet;
	sp.env = &rs.env;
	sp.rep = r_report;
	int first_style = -1;
	for (int i = 1; i < rs.doc.n.size(); i++) {
		DNode &n = rs.doc.n.write[i];
		if (n.kind != N_ELEMENT || n.tag != "style" || n.dead) {
			continue;
		}
		if (!style_type_is_css(n)) {
			continue;
		}
		const String media = attr_get(n, "media");
		if (!media.is_empty() && !eval_media_list(media, rs.env, r_report)) {
			n.dead = true;
			continue;
		}
		sp.parse(style_text_of(rs.doc, n), String());
		n.dead = true;
		if (first_style == -1) {
			first_style = i;
		}
	}
	rs.build_index();

	// Initial custom properties: @property defaults, then the host's own.
	VarMap root_vars;
	for (HashMap<String, String>::Iterator it = rs.sheet.initials.begin(); it != rs.sheet.initials.end(); ++it) {
		root_vars[it->key] = it->value;
	}
	for (HashMap<String, String>::Iterator it = rs.env.custom_properties.begin(); it != rs.env.custom_properties.end(); ++it) {
		root_vars[it->key] = it->value;
	}
	rs.add_vars(root_vars);

	State root_state;
	root_state.font = rs.env.root_font_size;
	rs.process(rs.doc.root, root_state);

	// Filter definitions and any kept at-rules go in front of the artwork.
	String prefix;
	for (int i = 0; i < rs.filter_defs.size(); i++) {
		prefix += rs.filter_defs[i];
	}
	if (!prefix.is_empty()) {
		prefix = "<defs>" + prefix + "</defs>";
	}
	if (!rs.sheet.residual.is_empty()) {
		prefix += "<style>" + rs.sheet.residual + "</style>";
	}
	if (!prefix.is_empty()) {
		DNode raw;
		raw.kind = N_RAW;
		raw.text = prefix;
		raw.parent = rs.doc.root;
		rs.doc.n.push_back(raw);
		const int idx = rs.doc.n.size() - 1;
		rs.doc.n.write[rs.doc.root].kids.insert(0, idx);
	}

	String out;
	serialize_node(rs.doc, 0, out);
	return out;
}
