/**************************************************************************/
/*  svg_text_path_converter.cpp                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "svg_text_path_converter.h"

#include "core/io/xml_parser.h"
#include "scene/resources/font.h"
#include "scene/theme/theme_db.h"
#include "servers/text/text_server.h"

namespace {

struct TextState {
	HashMap<String, String> properties;
	real_t x = 0.0;
	real_t y = 0.0;
	bool x_explicit = false;
	bool y_explicit = false;
};

struct TextRun {
	TextState state;
	String text;
};

static String _number(real_t p_value) {
	if (Math::is_zero_approx(p_value)) {
		return "0";
	}
	return String::num(p_value, 5);
}

static real_t _length(const String &p_value, real_t p_default = 0.0) {
	String value = p_value.strip_edges();
	if (value.is_empty()) {
		return p_default;
	}
	int separator = value.find_char(',');
	int whitespace = value.find_char(' ');
	if (separator == -1 || (whitespace != -1 && whitespace < separator)) {
		separator = whitespace;
	}
	if (separator != -1) {
		value = value.left(separator);
	}
	if (value.ends_with("pt")) {
		return value.left(-2).to_float() * (96.0 / 72.0);
	}
	return value.to_float();
}

static String _property(const TextState &p_state, const String &p_name, const String &p_default = String()) {
	const String *value = p_state.properties.getptr(p_name);
	return value ? *value : p_default;
}

static void _parse_style(const String &p_style, HashMap<String, String> &r_properties) {
	Vector<String> declarations = p_style.split(";", false);
	for (const String &declaration : declarations) {
		const int colon = declaration.find_char(':');
		if (colon <= 0) {
			continue;
		}
		const String key = declaration.left(colon).strip_edges().to_lower();
		const String value = declaration.substr(colon + 1).strip_edges();
		if (!key.is_empty() && !value.is_empty()) {
			r_properties[key] = value;
		}
	}
}

static void _apply_attributes(const Ref<XMLParser> &p_parser, TextState &r_state, String *r_group_attributes = nullptr) {
	String inline_style;
	for (int i = 0; i < p_parser->get_attribute_count(); i++) {
		const String name = p_parser->get_attribute_name(i);
		const String key = name.to_lower();
		const String value = p_parser->get_attribute_value(i);
		if (key == "x") {
			r_state.x = _length(value, r_state.x);
			r_state.x_explicit = true;
		} else if (key == "y") {
			r_state.y = _length(value, r_state.y);
			r_state.y_explicit = true;
		} else if (key == "dx") {
			r_state.x += _length(value);
		} else if (key == "dy") {
			r_state.y += _length(value);
		} else if (key == "style") {
			inline_style = value;
		} else if (key == "font-family" || key == "font-size" || key == "font-style" || key == "font-weight" || key == "font-stretch" || key == "text-anchor" || key == "fill" || key == "fill-opacity" || key == "stroke" || key == "stroke-opacity" || key == "stroke-width" || key == "stroke-linecap" || key == "stroke-linejoin" || key == "stroke-dasharray" || key == "opacity" || key == "paint-order" || key == "display" || key == "visibility") {
			r_state.properties[key] = value;
		}

		if (r_group_attributes && (key == "transform" || key == "clip-path" || key == "mask" || key == "filter")) {
			*r_group_attributes += " " + name + "=\"" + value.xml_escape(true) + "\"";
		}
	}
	_parse_style(inline_style, r_state.properties);
}

static PackedStringArray _font_names(const String &p_family) {
	PackedStringArray names;
	Vector<String> parts = p_family.split(",", false);
	for (String name : parts) {
		name = name.strip_edges();
		if ((name.begins_with("\"") && name.ends_with("\"")) || (name.begins_with("'") && name.ends_with("'"))) {
			name = name.substr(1, name.length() - 2);
		}
		const String generic = name.to_lower();
		if (generic != "sans-serif" && generic != "serif" && generic != "monospace" && !name.is_empty()) {
			names.push_back(name);
		}
	}
	return names;
}

static int _font_weight(const String &p_value) {
	const String value = p_value.strip_edges().to_lower();
	if (value == "bold" || value == "bolder") {
		return 700;
	}
	if (value == "normal" || value == "lighter" || value.is_empty()) {
		return 400;
	}
	return CLAMP(value.to_int(), 100, 999);
}

static String _point(const Vector2 &p_point, const Vector2 &p_origin, real_t p_scale) {
	return _number(p_origin.x + p_point.x * p_scale) + " " + _number(p_origin.y + p_point.y * p_scale);
}

static String _contour_to_path(const PackedVector3Array &p_points, int p_start, int p_end, const Vector2 &p_origin, real_t p_scale) {
	const int count = p_end - p_start + 1;
	if (count < 2) {
		return String();
	}
	auto tag = [&](int p_local) -> int {
		const int index = ((p_local % count) + count) % count;
		return (int)p_points[p_start + index].z;
	};
	auto point = [&](int p_local) -> Vector2 {
		const int index = ((p_local % count) + count) % count;
		const Vector3 value = p_points[p_start + index];
		return Vector2(value.x, value.y);
	};

	Vector2 start_point;
	int cursor = 0;
	int remaining = count;
	if (tag(0) == TextServer::CONTOUR_CURVE_TAG_ON) {
		start_point = point(0);
		cursor = 1;
		remaining--;
	} else if (tag(count - 1) == TextServer::CONTOUR_CURVE_TAG_ON) {
		start_point = point(count - 1);
		remaining--;
	} else if (tag(0) == TextServer::CONTOUR_CURVE_TAG_OFF_CONIC && tag(count - 1) == TextServer::CONTOUR_CURVE_TAG_OFF_CONIC) {
		start_point = (point(count - 1) + point(0)) * 0.5;
	} else {
		return String();
	}

	String path = "M " + _point(start_point, p_origin, p_scale);
	while (remaining > 0) {
		const int current_tag = tag(cursor);
		if (current_tag == TextServer::CONTOUR_CURVE_TAG_ON) {
			path += " L " + _point(point(cursor), p_origin, p_scale);
			cursor++;
			remaining--;
		} else if (current_tag == TextServer::CONTOUR_CURVE_TAG_OFF_CONIC) {
			const Vector2 control = point(cursor);
			const int next_tag = remaining > 1 ? tag(cursor + 1) : TextServer::CONTOUR_CURVE_TAG_ON;
			Vector2 end_point;
			if (next_tag == TextServer::CONTOUR_CURVE_TAG_ON) {
				end_point = remaining > 1 ? point(cursor + 1) : start_point;
				cursor += remaining > 1 ? 2 : 1;
				remaining -= remaining > 1 ? 2 : 1;
			} else if (next_tag == TextServer::CONTOUR_CURVE_TAG_OFF_CONIC) {
				end_point = (control + point(cursor + 1)) * 0.5;
				cursor++;
				remaining--;
			} else {
				return String();
			}
			path += " Q " + _point(control, p_origin, p_scale) + " " + _point(end_point, p_origin, p_scale);
		} else if (current_tag == TextServer::CONTOUR_CURVE_TAG_OFF_CUBIC && remaining >= 2 && tag(cursor + 1) == TextServer::CONTOUR_CURVE_TAG_OFF_CUBIC) {
			const Vector2 control_1 = point(cursor);
			const Vector2 control_2 = point(cursor + 1);
			const bool has_endpoint = remaining >= 3 && tag(cursor + 2) == TextServer::CONTOUR_CURVE_TAG_ON;
			const Vector2 end_point = has_endpoint ? point(cursor + 2) : start_point;
			path += " C " + _point(control_1, p_origin, p_scale) + " " + _point(control_2, p_origin, p_scale) + " " + _point(end_point, p_origin, p_scale);
			cursor += has_endpoint ? 3 : 2;
			remaining -= has_endpoint ? 3 : 2;
		} else {
			return String();
		}
	}
	return path + " Z ";
}

static String _glyph_path(const Glyph &p_glyph, const Vector2 &p_origin, real_t p_scale) {
	Dictionary contours = TS->font_get_glyph_contours(p_glyph.font_rid, p_glyph.font_size, p_glyph.index);
	if (!contours.has("points") || !contours.has("contours")) {
		return String();
	}
	PackedVector3Array points = contours["points"];
	PackedInt32Array ends = contours["contours"];
	String path;
	for (int i = 0; i < ends.size(); i++) {
		const int start = i == 0 ? 0 : ends[i - 1] + 1;
		path += _contour_to_path(points, start, ends[i], p_origin, p_scale);
	}
	return path;
}

static String _path_style(const TextState &p_state) {
	static const char *properties[] = {
		"fill", "fill-opacity", "stroke", "stroke-opacity", "stroke-width", "stroke-linecap", "stroke-linejoin", "stroke-dasharray", "opacity", "paint-order", "visibility"
	};
	String style = "fill:" + _property(p_state, "fill", "black") + ";";
	for (const char *property : properties) {
		if (String(property) == "fill") {
			continue;
		}
		const String value = _property(p_state, property);
		if (!value.is_empty()) {
			style += String(property) + ":" + value + ";";
		}
	}
	return style;
}

static String _run_to_path(const TextRun &p_run, HashMap<String, Ref<SystemFont>> &r_font_cache, real_t &r_advance) {
	r_advance = 0.0;
	if (p_run.text.is_empty() || _property(p_run.state, "display").to_lower() == "none" || _property(p_run.state, "visibility").to_lower() == "hidden") {
		return String();
	}

	const real_t requested_size = MAX((real_t)1.0, _length(_property(p_run.state, "font-size", "16"), 16.0));
	const int font_size = MAX(1, (int)Math::round(requested_size));
	const real_t outline_scale = requested_size / font_size;
	const String family = _property(p_run.state, "font-family", "sans-serif");
	const int weight = _font_weight(_property(p_run.state, "font-weight", "400"));
	const String font_style = _property(p_run.state, "font-style").to_lower();
	const bool italic = font_style == "italic" || font_style == "oblique";
	const String font_key = family + "|" + itos(weight) + "|" + (italic ? "1" : "0");
	Ref<SystemFont> font;
	const Ref<SystemFont> *cached_font = r_font_cache.getptr(font_key);
	if (cached_font) {
		font = *cached_font;
	} else {
		font.instantiate();
		font->set_font_names(_font_names(family));
		font->set_font_weight(weight);
		font->set_font_italic(italic);
		Ref<Font> fallback = ThemeDB::get_singleton()->get_fallback_font();
		if (fallback.is_valid()) {
			TypedArray<Font> fallbacks;
			fallbacks.push_back(fallback);
			font->set_fallbacks(fallbacks);
		}
		r_font_cache[font_key] = font;
	}

	RID shaped = TS->create_shaped_text(TextServer::DIRECTION_AUTO, TextServer::ORIENTATION_HORIZONTAL);
	if (!TS->shaped_text_add_string(shaped, p_run.text, font->get_rids(), font_size, font->get_opentype_features())) {
		TS->free_rid(shaped);
		return String();
	}
	TS->shaped_text_shape(shaped);
	const Glyph *glyphs = TS->shaped_text_get_glyphs(shaped);
	const int glyph_count = TS->shaped_text_get_glyph_count(shaped);
	for (int i = 0; i < glyph_count; i++) {
		r_advance += glyphs[i].advance * glyphs[i].repeat * outline_scale;
	}

	real_t anchor_offset = 0.0;
	const String anchor = _property(p_run.state, "text-anchor").to_lower();
	if (anchor == "middle") {
		anchor_offset = -r_advance * 0.5;
	} else if (anchor == "end") {
		anchor_offset = -r_advance;
	}

	String path;
	real_t pen = anchor_offset;
	for (int i = 0; i < glyph_count; i++) {
		for (int repeat = 0; repeat < glyphs[i].repeat; repeat++) {
			const Vector2 origin(p_run.state.x + pen + glyphs[i].x_off * outline_scale, p_run.state.y + glyphs[i].y_off * outline_scale);
			path += _glyph_path(glyphs[i], origin, outline_scale);
			pen += glyphs[i].advance * outline_scale;
		}
	}
	TS->free_rid(shaped);
	if (path.is_empty()) {
		return String();
	}
	return "<path d=\"" + path + "\" style=\"" + _path_style(p_run.state).xml_escape(true) + "\"/>";
}

static String _convert_text_fragment(const String &p_fragment, HashMap<String, Ref<SystemFont>> &r_font_cache) {
	Ref<XMLParser> parser;
	parser.instantiate();
	PackedByteArray bytes = p_fragment.to_utf8_buffer();
	if (parser->open_buffer(bytes) != OK) {
		return p_fragment;
	}

	Vector<TextState> stack;
	Vector<TextRun> runs;
	String group_attributes;
	bool saw_text = false;
	while (parser->read() == OK) {
		const XMLParser::NodeType type = parser->get_node_type();
		if (type == XMLParser::NODE_ELEMENT) {
			const String name = parser->get_node_name().to_lower();
			if (name != "text" && name != "tspan") {
				continue;
			}
			TextState state = stack.is_empty() ? TextState() : stack[stack.size() - 1];
			state.x_explicit = false;
			state.y_explicit = false;
			_apply_attributes(parser, state, name == "text" ? &group_attributes : nullptr);
			stack.push_back(state);
			if (name == "text") {
				saw_text = true;
			}
			if (parser->is_empty()) {
				stack.remove_at(stack.size() - 1);
			}
		} else if ((type == XMLParser::NODE_TEXT || type == XMLParser::NODE_CDATA) && !stack.is_empty()) {
			const String value = parser->get_node_data();
			if (!value.strip_edges().is_empty()) {
				runs.push_back({ stack[stack.size() - 1], value });
			}
		} else if (type == XMLParser::NODE_ELEMENT_END) {
			const String name = parser->get_node_name().to_lower();
			if ((name == "text" || name == "tspan") && !stack.is_empty()) {
				stack.remove_at(stack.size() - 1);
			}
		}
	}
	if (!saw_text) {
		return p_fragment;
	}

	String result = "<g" + group_attributes + ">";
	real_t cursor_x = 0.0;
	real_t cursor_y = 0.0;
	for (int i = 0; i < runs.size(); i++) {
		if (i == 0 || runs[i].state.x_explicit) {
			cursor_x = runs[i].state.x;
		} else {
			runs.write[i].state.x = cursor_x;
		}
		if (i == 0 || runs[i].state.y_explicit) {
			cursor_y = runs[i].state.y;
		} else {
			runs.write[i].state.y = cursor_y;
		}
		real_t advance = 0.0;
		result += _run_to_path(runs[i], r_font_cache, advance);
		cursor_x = runs[i].state.x + advance;
	}
	return result + "</g>";
}

static int _tag_end(const String &p_svg, int p_start) {
	bool quoted = false;
	char32_t quote = 0;
	for (int i = p_start; i < p_svg.length(); i++) {
		const char32_t c = p_svg[i];
		if (quoted) {
			if (c == quote) {
				quoted = false;
			}
		} else if (c == '\'' || c == '"') {
			quoted = true;
			quote = c;
		} else if (c == '>') {
			return i;
		}
	}
	return -1;
}

} // namespace

String SVGTextPathConverter::convert(const String &p_svg) {
	if (!TextServerManager::get_singleton() || TS.is_null() || !ThemeDB::get_singleton()) {
		return p_svg;
	}

	const String lower = p_svg.to_lower();
	String output;
	HashMap<String, Ref<SystemFont>> font_cache;
	int cursor = 0;
	while (true) {
		int start = lower.find("<text", cursor);
		while (start != -1 && start + 5 < lower.length() && lower[start + 5] != '>' && lower[start + 5] != '/' && !is_whitespace(lower[start + 5])) {
			start = lower.find("<text", start + 5);
		}
		if (start == -1) {
			output += p_svg.substr(cursor);
			break;
		}
		output += p_svg.substr(cursor, start - cursor);
		const int open_end = _tag_end(p_svg, start);
		if (open_end == -1) {
			output += p_svg.substr(start);
			break;
		}
		if (p_svg[open_end - 1] == '/') {
			cursor = open_end + 1;
			continue;
		}
		const int close_start = lower.find("</text", open_end + 1);
		if (close_start == -1) {
			output += p_svg.substr(start);
			break;
		}
		const int close_end = _tag_end(p_svg, close_start);
		if (close_end == -1) {
			output += p_svg.substr(start);
			break;
		}
		const String fragment = p_svg.substr(start, close_end - start + 1);
		output += _convert_text_fragment(fragment, font_cache);
		cursor = close_end + 1;
	}
	return output;
}
