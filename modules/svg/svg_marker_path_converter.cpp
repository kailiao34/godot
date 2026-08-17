/**************************************************************************/
/*  svg_marker_path_converter.cpp                                         */
/**************************************************************************/

#include "svg_marker_path_converter.h"

#include "core/io/xml_parser.h"
#include "core/math/math_funcs.h"
#include "core/templates/hash_map.h"

namespace {

struct MarkerDefinition {
	HashMap<String, String> attributes;
	String body;
};

struct PathVertex {
	Vector2 point;
	Vector2 incoming;
	Vector2 outgoing;
	bool has_incoming = false;
	bool has_outgoing = false;
};

static int _tag_end(const String &p_source, int p_start) {
	bool quoted = false;
	char32_t quote = 0;
	for (int i = p_start; i < p_source.length(); i++) {
		const char32_t c = p_source[i];
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

static HashMap<String, String> _tag_attributes(const String &p_tag) {
	String tag = p_tag;
	if (!tag.ends_with("/>")) {
		tag = tag.left(-1) + "/>";
	}
	Ref<XMLParser> parser;
	parser.instantiate();
	HashMap<String, String> result;
	if (parser->open_buffer(tag.to_utf8_buffer()) != OK || parser->read() != OK || parser->get_node_type() != XMLParser::NODE_ELEMENT) {
		return result;
	}
	for (int i = 0; i < parser->get_attribute_count(); i++) {
		result[parser->get_attribute_name(i).to_lower()] = parser->get_attribute_value(i);
	}
	return result;
}

static void _parse_style(const String &p_style, HashMap<String, String> &r_properties) {
	for (const String &declaration : p_style.split(";", false)) {
		const int colon = declaration.find_char(':');
		if (colon > 0) {
			r_properties[declaration.left(colon).strip_edges().to_lower()] = declaration.substr(colon + 1).strip_edges();
		}
	}
}

static String _value(const HashMap<String, String> &p_values, const String &p_name, const String &p_default = String()) {
	const String *value = p_values.getptr(p_name);
	return value ? *value : p_default;
}

static real_t _number(const String &p_value, real_t p_default = 0.0) {
	return p_value.strip_edges().is_empty() ? p_default : p_value.to_float();
}

static String _fmt(real_t p_value) {
	return Math::is_zero_approx(p_value) ? "0" : String::num(p_value, 5);
}

static bool _read_number(const String &p_data, int &r_cursor, real_t &r_value) {
	while (r_cursor < p_data.length() && (is_whitespace(p_data[r_cursor]) || p_data[r_cursor] == ',')) {
		r_cursor++;
	}
	const int start = r_cursor;
	if (r_cursor < p_data.length() && (p_data[r_cursor] == '+' || p_data[r_cursor] == '-')) {
		r_cursor++;
	}
	bool digit = false;
	while (r_cursor < p_data.length() && ((p_data[r_cursor] >= '0' && p_data[r_cursor] <= '9') || p_data[r_cursor] == '.')) {
		digit = true;
		r_cursor++;
	}
	if (r_cursor < p_data.length() && (p_data[r_cursor] == 'e' || p_data[r_cursor] == 'E')) {
		r_cursor++;
		if (r_cursor < p_data.length() && (p_data[r_cursor] == '+' || p_data[r_cursor] == '-')) {
			r_cursor++;
		}
		while (r_cursor < p_data.length() && p_data[r_cursor] >= '0' && p_data[r_cursor] <= '9') {
			r_cursor++;
		}
	}
	if (!digit) {
		r_cursor = start;
		return false;
	}
	r_value = p_data.substr(start, r_cursor - start).to_float();
	return true;
}

static void _add_segment(Vector<PathVertex> &r_vertices, const Vector2 &p_end, const Vector2 &p_outgoing, const Vector2 &p_incoming) {
	if (r_vertices.is_empty()) {
		return;
	}
	r_vertices.write[r_vertices.size() - 1].outgoing = p_outgoing;
	r_vertices.write[r_vertices.size() - 1].has_outgoing = !p_outgoing.is_zero_approx();
	PathVertex vertex;
	vertex.point = p_end;
	vertex.incoming = p_incoming;
	vertex.has_incoming = !p_incoming.is_zero_approx();
	r_vertices.push_back(vertex);
}

static Vector<PathVertex> _path_vertices(const String &p_data) {
	Vector<PathVertex> vertices;
	int cursor = 0;
	char32_t command = 0;
	Vector2 current;
	Vector2 subpath;
	Vector2 previous_control;
	bool previous_curve = false;
	while (cursor < p_data.length()) {
		while (cursor < p_data.length() && (is_whitespace(p_data[cursor]) || p_data[cursor] == ',')) {
			cursor++;
		}
		if (cursor >= p_data.length()) {
			break;
		}
		if ((p_data[cursor] >= 'A' && p_data[cursor] <= 'Z') || (p_data[cursor] >= 'a' && p_data[cursor] <= 'z')) {
			command = p_data[cursor++];
		} else if (command == 0) {
			break;
		}
		const bool relative = command >= 'a' && command <= 'z';
		const char32_t upper = relative ? command - ('a' - 'A') : command;
		if (upper == 'Z') {
			_add_segment(vertices, subpath, subpath - current, subpath - current);
			current = subpath;
			command = 0;
			continue;
		}
		const int parameter_count = upper == 'H' || upper == 'V' ? 1 : upper == 'M' || upper == 'L' || upper == 'T' ? 2 : upper == 'S' || upper == 'Q' ? 4 : upper == 'C' ? 6 : upper == 'A' ? 7 : 0;
		if (parameter_count == 0) {
			break;
		}
		real_t values[7] = {};
		const int before = cursor;
		bool valid = true;
		for (int i = 0; i < parameter_count; i++) {
			valid = valid && _read_number(p_data, cursor, values[i]);
		}
		if (!valid) {
			cursor = before;
			command = 0;
			continue;
		}
		Vector2 end = current;
		Vector2 outgoing;
		Vector2 incoming;
		if (upper == 'M' || upper == 'L' || upper == 'T') {
			end = Vector2(values[0], values[1]) + (relative ? current : Vector2());
			if (upper == 'T' && previous_curve) {
				const Vector2 control = current * 2.0 - previous_control;
				outgoing = control - current;
				incoming = end - control;
				previous_control = control;
			} else {
				outgoing = incoming = end - current;
			}
		} else if (upper == 'H') {
			end.x = values[0] + (relative ? current.x : 0.0);
			outgoing = incoming = end - current;
		} else if (upper == 'V') {
			end.y = values[0] + (relative ? current.y : 0.0);
			outgoing = incoming = end - current;
		} else if (upper == 'C') {
			const Vector2 c1 = Vector2(values[0], values[1]) + (relative ? current : Vector2());
			const Vector2 c2 = Vector2(values[2], values[3]) + (relative ? current : Vector2());
			end = Vector2(values[4], values[5]) + (relative ? current : Vector2());
			outgoing = c1 - current;
			incoming = end - c2;
			previous_control = c2;
		} else if (upper == 'S') {
			const Vector2 c1 = previous_curve ? current * 2.0 - previous_control : current;
			const Vector2 c2 = Vector2(values[0], values[1]) + (relative ? current : Vector2());
			end = Vector2(values[2], values[3]) + (relative ? current : Vector2());
			outgoing = c1 - current;
			incoming = end - c2;
			previous_control = c2;
		} else if (upper == 'Q') {
			const Vector2 control = Vector2(values[0], values[1]) + (relative ? current : Vector2());
			end = Vector2(values[2], values[3]) + (relative ? current : Vector2());
			outgoing = control - current;
			incoming = end - control;
			previous_control = control;
		} else if (upper == 'A') {
			end = Vector2(values[5], values[6]) + (relative ? current : Vector2());
			outgoing = incoming = end - current; // Endpoint chord is a safe marker tangent fallback.
		}
		if (upper == 'M') {
			PathVertex vertex;
			vertex.point = end;
			vertices.push_back(vertex);
			subpath = end;
			command = relative ? 'l' : 'L';
		} else {
			_add_segment(vertices, end, outgoing, incoming);
		}
		current = end;
		previous_curve = upper == 'C' || upper == 'S' || upper == 'Q' || upper == 'T';
	}
	return vertices;
}

static String _url_id(const String &p_value) {
	const int hash = p_value.find_char('#');
	const int close = p_value.find_char(')', hash + 1);
	return hash == -1 ? String() : p_value.substr(hash + 1, (close == -1 ? p_value.length() : close) - hash - 1).strip_edges();
}

static String _marker_instance(const MarkerDefinition &p_marker, const Vector2 &p_position, real_t p_angle, bool p_start, real_t p_stroke_width, const String &p_stroke, const String &p_fill) {
	String orient = _value(p_marker.attributes, "orient", "0").to_lower();
	if (orient != "auto" && orient != "auto-start-reverse") {
		p_angle = orient.to_float();
	} else if (p_start && orient == "auto-start-reverse") {
		p_angle += 180.0;
	}
	const real_t ref_x = _number(_value(p_marker.attributes, "refx"));
	const real_t ref_y = _number(_value(p_marker.attributes, "refy"));
	real_t view_scale = 1.0;
	Vector<String> view_box = _value(p_marker.attributes, "viewbox").replace(",", " ").split(" ", false);
	if (view_box.size() == 4) {
		const real_t vb_w = view_box[2].to_float();
		const real_t vb_h = view_box[3].to_float();
		if (!Math::is_zero_approx(vb_w) && !Math::is_zero_approx(vb_h)) {
			view_scale = MIN(_number(_value(p_marker.attributes, "markerwidth", "3"), 3.0) / vb_w, _number(_value(p_marker.attributes, "markerheight", "3"), 3.0) / vb_h);
		}
	}
	const real_t unit_scale = _value(p_marker.attributes, "markerunits", "strokeWidth").to_lower() == "userspaceonuse" ? 1.0 : p_stroke_width;
	String body = p_marker.body.replace("context-stroke", p_stroke).replace("context-fill", p_fill);
	return "<g transform=\"translate(" + _fmt(p_position.x) + " " + _fmt(p_position.y) + ") rotate(" + _fmt(p_angle) + ") scale(" + _fmt(unit_scale * view_scale) + ") translate(" + _fmt(-ref_x) + " " + _fmt(-ref_y) + ")\">" + body + "</g>";
}

static HashMap<String, MarkerDefinition> _markers(const String &p_svg) {
	HashMap<String, MarkerDefinition> markers;
	const String lower = p_svg.to_lower();
	int cursor = 0;
	while ((cursor = lower.find("<marker", cursor)) != -1) {
		const int open_end = _tag_end(p_svg, cursor);
		const int close_start = lower.find("</marker", open_end + 1);
		if (open_end == -1 || close_start == -1) {
			break;
		}
		MarkerDefinition marker;
		marker.attributes = _tag_attributes(p_svg.substr(cursor, open_end - cursor + 1));
		marker.body = p_svg.substr(open_end + 1, close_start - open_end - 1);
		const String id = _value(marker.attributes, "id");
		if (!id.is_empty()) {
			markers[id] = marker;
		}
		cursor = close_start + 8;
	}
	return markers;
}

} // namespace

String SVGMarkerPathConverter::convert(const String &p_svg) {
	const HashMap<String, MarkerDefinition> markers = _markers(p_svg);
	if (markers.is_empty()) {
		return p_svg;
	}
	const String lower = p_svg.to_lower();
	String output;
	int cursor = 0;
	while (true) {
		int start = lower.find("<path", cursor);
		while (start != -1 && start + 5 < lower.length() && lower[start + 5] != '>' && lower[start + 5] != '/' && !is_whitespace(lower[start + 5])) {
			start = lower.find("<path", start + 5);
		}
		if (start == -1) {
			output += p_svg.substr(cursor);
			break;
		}
		const int end = _tag_end(p_svg, start);
		if (end == -1) {
			output += p_svg.substr(cursor);
			break;
		}
		output += p_svg.substr(cursor, end - cursor + 1);
		HashMap<String, String> attributes = _tag_attributes(p_svg.substr(start, end - start + 1));
		HashMap<String, String> properties(attributes);
		_parse_style(_value(attributes, "style"), properties);
		const String start_id = _url_id(_value(properties, "marker-start"));
		const String mid_id = _url_id(_value(properties, "marker-mid"));
		const String end_id = _url_id(_value(properties, "marker-end"));
		if (!start_id.is_empty() || !mid_id.is_empty() || !end_id.is_empty()) {
			const Vector<PathVertex> vertices = _path_vertices(_value(attributes, "d"));
			const real_t stroke_width = MAX((real_t)0.0001, _number(_value(properties, "stroke-width", "1"), 1.0));
			const String stroke = _value(properties, "stroke", "black");
			const String fill = _value(properties, "fill", "black");
			String instances;
			if (!vertices.is_empty() && markers.has(start_id)) {
				const Vector2 tangent = vertices[0].has_outgoing ? vertices[0].outgoing : Vector2(1, 0);
				instances += _marker_instance(markers[start_id], vertices[0].point, Math::rad_to_deg(tangent.angle()), true, stroke_width, stroke, fill);
			}
			if (!mid_id.is_empty() && markers.has(mid_id)) {
				for (int i = 1; i + 1 < vertices.size(); i++) {
					Vector2 tangent = vertices[i].incoming.normalized() + vertices[i].outgoing.normalized();
					if (tangent.is_zero_approx()) {
						tangent = vertices[i].has_outgoing ? vertices[i].outgoing : vertices[i].incoming;
					}
					instances += _marker_instance(markers[mid_id], vertices[i].point, Math::rad_to_deg(tangent.angle()), false, stroke_width, stroke, fill);
				}
			}
			if (!vertices.is_empty() && markers.has(end_id)) {
				const PathVertex &last = vertices[vertices.size() - 1];
				const Vector2 tangent = last.has_incoming ? last.incoming : Vector2(1, 0);
				instances += _marker_instance(markers[end_id], last.point, Math::rad_to_deg(tangent.angle()), false, stroke_width, stroke, fill);
			}
			const String transform = _value(attributes, "transform");
			output += transform.is_empty() ? instances : "<g transform=\"" + transform.xml_escape(true) + "\">" + instances + "</g>";
		}
		cursor = end + 1;
	}
	return output;
}
