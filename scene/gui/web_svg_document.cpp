/**************************************************************************/
/*  web_svg_document.cpp                                                  */
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

#include "web_svg_document.h"

#include "core/crypto/crypto_core.h"
#include "core/io/compression.h"
#include "core/io/file_access.h"
#include "core/io/image.h"

// --- Small helpers ---

static _FORCE_INLINE_ bool is_xml_space(char32_t p_c) {
	return p_c == ' ' || p_c == '\t' || p_c == '\n' || p_c == '\r';
}

static void push_unique(PackedStringArray &r_list, const String &p_value) {
	if (r_list.has(p_value)) {
		return;
	}
	r_list.push_back(p_value);
}

// Collapses the escapes an attacker can hide a scheme behind (`&#106;avascript:`,
// embedded tabs and newlines) so the scheme check below cannot be bypassed.
static String normalize_uri_for_check(const String &p_value) {
	String out;
	const int n = p_value.length();
	for (int i = 0; i < n; i++) {
		const char32_t c = p_value[i];
		if (c <= 0x20 || c == 0x7f) {
			continue; // Control characters and whitespace are ignored by URL parsers.
		}
		if (c == '&' && i + 2 < n && p_value[i + 1] == '#') {
			int j = i + 2;
			bool hex = false;
			if (j < n && (p_value[j] == 'x' || p_value[j] == 'X')) {
				hex = true;
				j++;
			}
			const int digits_start = j;
			while (j < n && p_value[j] != ';') {
				j++;
			}
			if (j < n && j > digits_start) {
				const String digits = p_value.substr(digits_start, j - digits_start);
				const int64_t code = hex ? digits.hex_to_int() : digits.to_int();
				if (code > 0 && code < 0x110000) {
					out += String::chr((char32_t)code);
					i = j;
					continue;
				}
			}
		}
		out += String::chr(c);
	}
	return out.to_lower();
}

bool WebSVGDocument::is_safe_href(const String &p_value) {
	const String v = normalize_uri_for_check(p_value.strip_edges());
	if (v.is_empty() || v.begins_with("#")) {
		return true; // Internal reference.
	}
	if (v.begins_with("data:image/")) {
		return true; // Already-inlined artwork.
	}
	const int colon = v.find_char(':');
	if (colon == -1) {
		return true; // Scheme-less, i.e. a relative local path.
	}
	if (colon == 1) {
		return true; // A Windows drive letter, not a scheme.
	}
	// A `/`, `?` or `#` before the colon means the colon is inside a path segment.
	const int slash = v.find_char('/');
	const int query = v.find_char('?');
	const int frag = v.find_char('#');
	if ((slash != -1 && slash < colon) || (query != -1 && query < colon) || (frag != -1 && frag < colon)) {
		return true;
	}
	return false; // A real scheme: javascript:, http:, file: ...
}

float WebSVGDocument::parse_length(const String &p_value, bool *r_ok, bool *r_percent) {
	if (r_ok) {
		*r_ok = false;
	}
	if (r_percent) {
		*r_percent = false;
	}
	String v = p_value.strip_edges();
	if (v.is_empty()) {
		return 0.0f;
	}

	// CSS absolute lengths, all relative to the 96dpi reference pixel. `em`, `rem`,
	// `ex` and `ch` have no cascade to consult here, so they assume the 16px
	// default font size.
	struct Unit {
		const char *suffix;
		float scale;
	};
	static const Unit units[] = {
		{ "px", 1.0f },
		{ "pt", 96.0f / 72.0f },
		{ "pc", 16.0f },
		{ "in", 96.0f },
		{ "cm", 96.0f / 2.54f },
		{ "mm", 96.0f / 25.4f },
		{ "rem", 16.0f },
		{ "em", 16.0f },
		{ "ex", 8.0f },
		{ "ch", 8.0f },
		{ "q", 96.0f / 101.6f },
	};

	float scale = 1.0f;
	const String lower = v.to_lower();
	if (lower.ends_with("%")) {
		if (r_percent) {
			*r_percent = true;
		}
		v = v.substr(0, v.length() - 1);
	} else {
		for (const Unit &u : units) {
			const String suffix = u.suffix;
			if (lower.ends_with(suffix)) {
				scale = u.scale;
				v = v.substr(0, v.length() - suffix.length());
				break;
			}
		}
	}

	v = v.strip_edges();
	if (v.is_empty() || !v.is_valid_float()) {
		return 0.0f;
	}
	if (r_ok) {
		*r_ok = true;
	}
	return v.to_float() * scale;
}

String WebSVGDocument::strip_attributes(const String &p_attrs, const Vector<String> &p_names) {
	String out;
	int i = 0;
	const int n = p_attrs.length();
	while (i < n) {
		while (i < n && is_xml_space(p_attrs[i])) {
			i++;
		}
		if (i >= n) {
			break;
		}
		const int start = i;
		while (i < n && !is_xml_space(p_attrs[i]) && p_attrs[i] != '=') {
			i++;
		}
		const String key = p_attrs.substr(start, i - start);
		const int after_key = i;
		while (i < n && is_xml_space(p_attrs[i])) {
			i++;
		}
		if (i < n && p_attrs[i] == '=') {
			i++;
			while (i < n && is_xml_space(p_attrs[i])) {
				i++;
			}
			if (i < n && (p_attrs[i] == '"' || p_attrs[i] == '\'')) {
				const char32_t q = p_attrs[i++];
				while (i < n && p_attrs[i] != q) {
					i++;
				}
				if (i < n) {
					i++;
				}
			} else {
				while (i < n && !is_xml_space(p_attrs[i])) {
					i++;
				}
			}
		} else {
			i = after_key; // Boolean attribute (no value).
		}
		if (start == i) {
			i++; // Never stall on malformed input.
			continue;
		}
		if (p_names.has(key.to_lower())) {
			continue;
		}
		if (!out.is_empty()) {
			out += " ";
		}
		out += p_attrs.substr(start, i - start);
	}
	return out;
}

// --- Byte decoding ---

Error WebSVGDocument::decode(const Vector<uint8_t> &p_bytes, String &r_markup) {
	Vector<uint8_t> data = p_bytes;
	if (data.size() >= 2 && data[0] == 0x1f && data[1] == 0x8b) {
		// `.svgz`, or a `.svg` that happens to be gzipped.
		Vector<uint8_t> inflated;
		const int64_t size = Compression::decompress_dynamic(&inflated, MAX_DOCUMENT_BYTES, data.ptr(), data.size(), Compression::MODE_GZIP);
		ERR_FAIL_COND_V_MSG(size < 0, ERR_FILE_CORRUPT, "WebSVG: failed to decompress gzipped SVG data.");
		data = inflated;
	}
	ERR_FAIL_COND_V_MSG(data.size() > MAX_DOCUMENT_BYTES, ERR_FILE_CORRUPT,
			vformat("WebSVG: SVG document is %d bytes, over the %d byte limit.", data.size(), MAX_DOCUMENT_BYTES));
	if (data.is_empty()) {
		r_markup = String();
		return OK;
	}

	const uint8_t *p = data.ptr();
	const int64_t n = data.size();
	String out;

	if (n >= 2 && ((p[0] == 0xff && p[1] == 0xfe) || (p[0] == 0xfe && p[1] == 0xff))) {
		const bool little_endian = p[0] == 0xff;
		const int64_t count = (n - 2) / 2;
		Vector<char16_t> units;
		units.resize(count);
		char16_t *w = units.ptrw();
		for (int64_t i = 0; i < count; i++) {
			const uint8_t lo = p[2 + i * 2];
			const uint8_t hi = p[2 + i * 2 + 1];
			w[i] = little_endian ? (char16_t)(lo | (hi << 8)) : (char16_t)(hi | (lo << 8));
		}
		const Error err = out.append_utf16(units.ptr(), units.size());
		ERR_FAIL_COND_V_MSG(err != OK, err, "WebSVG: SVG document is not valid UTF-16.");
	} else {
		const int64_t offset = (n >= 3 && p[0] == 0xef && p[1] == 0xbb && p[2] == 0xbf) ? 3 : 0;
		const Error err = out.append_utf8((const char *)(p + offset), n - offset);
		ERR_FAIL_COND_V_MSG(err != OK, err, "WebSVG: SVG document is not valid UTF-8.");
	}

	r_markup = out;
	return OK;
}

// --- Markup scanning ---
//
// One character-indexed scanner backs sanitizing, resource inlining, feature
// scanning and root extraction. Deliberately not XMLParser: its node offsets are
// byte offsets that can point at leading whitespace, which makes byte-exact
// splicing wrong for any document containing non-ASCII text. Rewrites touch only
// the tags a policy actually changes; every other byte is copied verbatim, so
// comments, CDATA, entity spellings and attribute quoting all survive.

namespace {

struct TagAttr {
	String name;
	String value;
	char32_t quote = '"';
	bool has_value = true;
	bool dropped = false;
};

enum ConstructKind {
	CK_COMMENT,
	CK_CDATA,
	CK_PI, // <? ... ?>
	CK_DECL, // <! ... >, including DOCTYPE with an internal subset.
	CK_START,
	CK_END,
	CK_STRAY, // A `<` that begins nothing parseable.
};

struct Construct {
	ConstructKind kind = CK_STRAY;
	int start = 0; // Index of '<'.
	int end = 0; // One past the construct.
	String name; // CK_START / CK_END.
	int attrs_start = 0;
	int attrs_end = 0; // CK_START: the raw attribute span.
	Vector<TagAttr> attrs;
	bool self_closing = false;
};

// `p_pos` must index a '<'.
void read_construct(const String &p_src, int p_pos, Construct &r_c) {
	const int n = p_src.length();
	r_c = Construct();
	r_c.start = p_pos;

	if (p_src.substr(p_pos, 4) == "<!--") {
		int e = p_src.find("-->", p_pos);
		r_c.kind = CK_COMMENT;
		r_c.end = (e == -1) ? n : e + 3;
		return;
	}
	if (p_src.substr(p_pos, 9) == "<![CDATA[") {
		int e = p_src.find("]]>", p_pos);
		r_c.kind = CK_CDATA;
		r_c.end = (e == -1) ? n : e + 3;
		return;
	}
	if (p_pos + 1 < n && p_src[p_pos + 1] == '?') {
		int e = p_src.find("?>", p_pos);
		r_c.kind = CK_PI;
		r_c.end = (e == -1) ? n : e + 2;
		return;
	}
	if (p_pos + 1 < n && p_src[p_pos + 1] == '!') {
		// A DOCTYPE's internal subset may itself contain '>'.
		int j = p_pos + 2;
		int bracket = 0;
		while (j < n) {
			const char32_t c = p_src[j];
			if (c == '[') {
				bracket++;
			} else if (c == ']') {
				bracket--;
			} else if (c == '>' && bracket <= 0) {
				break;
			}
			j++;
		}
		r_c.kind = CK_DECL;
		r_c.end = MIN(j + 1, n);
		return;
	}

	int j = p_pos + 1;
	bool closing = false;
	if (j < n && p_src[j] == '/') {
		closing = true;
		j++;
	}
	const int name_start = j;
	while (j < n && !is_xml_space(p_src[j]) && p_src[j] != '>' && p_src[j] != '/') {
		j++;
	}
	r_c.name = p_src.substr(name_start, j - name_start);
	if (r_c.name.is_empty()) {
		r_c.kind = CK_STRAY;
		r_c.end = p_pos + 1;
		return;
	}
	r_c.kind = closing ? CK_END : CK_START;
	r_c.attrs_start = j;

	while (j < n) {
		while (j < n && is_xml_space(p_src[j])) {
			j++;
		}
		if (j >= n) {
			break;
		}
		if (p_src[j] == '/') {
			r_c.self_closing = true;
			j++;
			continue;
		}
		if (p_src[j] == '>') {
			break;
		}
		const int as = j;
		while (j < n && !is_xml_space(p_src[j]) && p_src[j] != '=' && p_src[j] != '>' && p_src[j] != '/') {
			j++;
		}
		TagAttr attr;
		attr.name = p_src.substr(as, j - as);
		const int after_name = j;
		while (j < n && is_xml_space(p_src[j])) {
			j++;
		}
		if (j < n && p_src[j] == '=') {
			j++;
			while (j < n && is_xml_space(p_src[j])) {
				j++;
			}
			if (j < n && (p_src[j] == '"' || p_src[j] == '\'')) {
				attr.quote = p_src[j++];
				const int vs = j;
				while (j < n && p_src[j] != attr.quote) {
					j++;
				}
				attr.value = p_src.substr(vs, j - vs);
				if (j < n) {
					j++;
				}
			} else {
				const int vs = j;
				while (j < n && !is_xml_space(p_src[j]) && p_src[j] != '>') {
					j++;
				}
				attr.value = p_src.substr(vs, j - vs);
			}
		} else {
			attr.has_value = false;
			j = after_name;
		}
		if (attr.name.is_empty()) {
			if (j == as) {
				j++; // Never stall on malformed markup.
			}
			continue;
		}
		r_c.attrs.push_back(attr);
	}

	r_c.attrs_end = MIN(j, n);
	// `attrs_end` stops on '>' (or end of input); `end` steps past it.
	r_c.end = (j < n && p_src[j] == '>') ? j + 1 : MIN(j, n);
	// A trailing `/` belongs to the tag, not the attributes.
	while (r_c.attrs_end > r_c.attrs_start && (is_xml_space(p_src[r_c.attrs_end - 1]) || p_src[r_c.attrs_end - 1] == '/')) {
		r_c.attrs_end--;
	}
}

String attr_value(const Construct &p_c, const String &p_name) {
	const String want = p_name.to_lower();
	for (const TagAttr &a : p_c.attrs) {
		if (a.name.to_lower() == want) {
			return a.value;
		}
	}
	return String();
}

struct MarkupPolicy {
	bool sanitize = false;
	bool inline_resources = false;
	String base_dir;
	int depth = 0;
	PackedStringArray *removed = nullptr;
	PackedStringArray *warnings = nullptr;
	WebSVGDocumentInfo *info = nullptr;
};

// True only for real event handlers: `on` followed by letters, after dropping any
// namespace prefix. The strictness matters -- Inkscape writes `only_selected`,
// which also begins with "on" but is inert, and silently deleting an author's
// attributes (then calling it "active content") corrupts the document.
bool is_event_handler_attribute(const String &p_lname) {
	const int colon = p_lname.rfind_char(':');
	const String local = (colon == -1) ? p_lname : p_lname.substr(colon + 1);
	if (local.length() <= 2 || !local.begins_with("on")) {
		return false;
	}
	for (int i = 2; i < local.length(); i++) {
		const char32_t c = local[i];
		if (c < 'a' || c > 'z') {
			return false;
		}
	}
	return true;
}

bool is_unsupported_element(const String &p_lname, String &r_label) {
	// ThorVG 1.0.3 renders none of these. `<tspan>`, `<textPath>` and `<marker>`
	// are absent on purpose: modules/svg converts all three to paths before
	// ThorVG ever sees them.
	static const char *names[] = { "pattern", "switch", "foreignobject", "set", "mpath", "animatemotion", nullptr };
	for (int i = 0; names[i]; i++) {
		if (p_lname == names[i]) {
			r_label = "<" + p_lname + ">";
			return true;
		}
	}
	if (p_lname.begins_with("fe") && p_lname != "fegaussianblur") {
		r_label = "<" + p_lname + "> (only feGaussianBlur is supported)";
		return true;
	}
	return false;
}

String encode_data_uri(const String &p_mime, const Vector<uint8_t> &p_bytes) {
	return "data:" + p_mime + ";base64," + CryptoCore::b64_encode_str(p_bytes.ptr(), p_bytes.size());
}

// Reads a referenced file and returns it as a `data:` URI, transcoding the formats
// ThorVG is not compiled with (JPEG/WebP) to PNG so both renderers agree.
bool make_data_uri(const String &p_ref, const String &p_base_dir, int p_depth, PackedStringArray *r_warnings, String &r_uri);

String rebuild_tag(const Construct &p_c) {
	String out = "<" + p_c.name;
	for (const TagAttr &attr : p_c.attrs) {
		if (attr.dropped) {
			continue;
		}
		out += " " + attr.name;
		if (attr.has_value) {
			const String q = String::chr(attr.quote);
			out += "=" + q + attr.value.replace(q, attr.quote == '"' ? "&quot;" : "&apos;") + q;
		}
	}
	out += p_c.self_closing ? "/>" : ">";
	return out;
}

String process_markup(const String &p_src, const MarkupPolicy &p_policy) {
	String out;
	const int n = p_src.length();
	int i = 0;

	// While dropping, everything up to the matching end tag is discarded.
	bool dropping = false;
	String drop_tag;
	int drop_level = 0;

	while (i < n) {
		const int lt = p_src.find_char('<', i);
		if (lt == -1) {
			if (!dropping) {
				out += p_src.substr(i);
			}
			break;
		}
		if (!dropping && lt > i) {
			out += p_src.substr(i, lt - i);
		}

		Construct c;
		read_construct(p_src, lt, c);
		const String lname = c.name.to_lower();

		switch (c.kind) {
			case CK_COMMENT:
			case CK_CDATA:
			case CK_STRAY: {
				if (!dropping) {
					out += p_src.substr(c.start, c.end - c.start);
				}
			} break;

			case CK_PI: {
				// An external stylesheet would restyle the document from a file we
				// neither control nor can inline.
				const String raw = p_src.substr(c.start, c.end - c.start);
				const bool drop = p_policy.sanitize && raw.findn("xml-stylesheet") != -1;
				if (drop && p_policy.removed) {
					push_unique(*p_policy.removed, "<?xml-stylesheet?>");
				}
				if (!dropping && !drop) {
					out += raw;
				}
			} break;

			case CK_DECL: {
				const String raw = p_src.substr(c.start, c.end - c.start);
				// Dropped wholesale: an internal subset can declare entities, which
				// is how XXE and billion-laughs attacks arrive.
				const bool drop = p_policy.sanitize && raw.to_lower().begins_with("<!doctype");
				if (drop && p_policy.removed) {
					push_unique(*p_policy.removed, "<!DOCTYPE>");
				}
				if (!dropping && !drop) {
					out += raw;
				}
			} break;

			case CK_END: {
				if (dropping) {
					if (lname == drop_tag) {
						drop_level--;
						if (drop_level <= 0) {
							dropping = false;
						}
					}
				} else {
					out += p_src.substr(c.start, c.end - c.start);
				}
			} break;

			case CK_START: {
				if (dropping) {
					if (lname == drop_tag && !c.self_closing) {
						drop_level++;
					}
					break;
				}

				if (p_policy.sanitize && (lname == "script" || lname == "foreignobject")) {
					if (p_policy.removed) {
						push_unique(*p_policy.removed, "<" + lname + ">");
					}
					if (!c.self_closing) {
						dropping = true;
						drop_tag = lname;
						drop_level = 1;
					}
					break;
				}

				if (p_policy.info) {
					p_policy.info->element_count++;
					if (lname == "animate" || lname == "animatetransform") {
						p_policy.info->has_animations = true;
					}
					String label;
					if (is_unsupported_element(lname, label)) {
						push_unique(p_policy.info->unsupported, label);
					}
				}

				bool modified = false;
				if (p_policy.sanitize) {
					for (TagAttr &attr : c.attrs) {
						const String la = attr.name.to_lower();
						if (is_event_handler_attribute(la)) {
							attr.dropped = true;
							modified = true;
							if (p_policy.removed) {
								push_unique(*p_policy.removed, "on* handlers");
							}
							continue;
						}
						if (la == "href" || la.ends_with(":href")) {
							if (!WebSVGDocument::is_safe_href(attr.value)) {
								attr.dropped = true;
								modified = true;
								if (p_policy.removed) {
									push_unique(*p_policy.removed, "unsafe href");
								}
							}
							continue;
						}
						if (la == "style" || la == "filter" || la == "fill" || la == "stroke" || la == "clip-path" || la == "mask") {
							const String lv = attr.value.to_lower();
							if (lv.find("javascript:") != -1 || lv.find("expression(") != -1) {
								attr.dropped = true;
								modified = true;
								if (p_policy.removed) {
									push_unique(*p_policy.removed, "script in " + la);
								}
							}
						}
					}
				}

				if (p_policy.inline_resources && lname == "image") {
					TagAttr *target = nullptr;
					for (TagAttr &attr : c.attrs) {
						const String la = attr.name.to_lower();
						if (la == "href") {
							target = &attr;
							break;
						}
						if (la.ends_with(":href") && !target) {
							target = &attr;
						}
					}
					if (target && !target->dropped) {
						const String lv = target->value.strip_edges().to_lower();
						if (!lv.is_empty() && !lv.begins_with("data:") && !lv.begins_with("#") && WebSVGDocument::is_safe_href(target->value)) {
							String uri;
							if (make_data_uri(target->value, p_policy.base_dir, p_policy.depth, p_policy.warnings, uri)) {
								target->value = uri;
								target->quote = '"';
								modified = true;
							}
						}
					}
				}

				out += modified ? rebuild_tag(c) : p_src.substr(c.start, c.end - c.start);
			} break;
		}

		i = MAX(c.end, lt + 1);
	}

	return out;
}

bool make_data_uri(const String &p_ref, const String &p_base_dir, int p_depth, PackedStringArray *r_warnings, String &r_uri) {
	String ref = p_ref.strip_edges();
	const int frag = ref.find_char('#');
	if (frag != -1) {
		ref = ref.substr(0, frag);
	}
	ref = ref.uri_decode();
	if (ref.is_empty()) {
		return false;
	}

	String path = ref;
	if (!path.is_absolute_path()) {
		if (p_base_dir.is_empty()) {
			if (r_warnings) {
				push_unique(*r_warnings, vformat("Cannot resolve \"%s\": the document has no base directory.", ref));
			}
			return false;
		}
		path = p_base_dir.path_join(ref).simplify_path();
	}

	Error err = OK;
	const Vector<uint8_t> bytes = FileAccess::get_file_as_bytes(path, &err);
	if (err != OK || bytes.is_empty()) {
		if (r_warnings) {
			push_unique(*r_warnings, vformat("Referenced file not found: \"%s\".", path));
		}
		return false;
	}

	const String ext = path.get_extension().to_lower();
	if (ext == "png") {
		r_uri = encode_data_uri("image/png", bytes);
		return true;
	}
	if (ext == "svg" || ext == "svgz") {
		String nested;
		if (WebSVGDocument::decode(bytes, nested) != OK) {
			return false;
		}
		// A referenced document is no more trusted than the one that names it.
		PackedStringArray nested_removed;
		nested = WebSVGDocument::sanitize(nested, nested_removed);
		if (r_warnings) {
			for (const String &item : nested_removed) {
				push_unique(*r_warnings, vformat("Sanitized %s out of \"%s\".", item, path));
			}
		}
		PackedStringArray discard;
		nested = WebSVGDocument::inline_resources(nested, path.get_base_dir(), r_warnings ? *r_warnings : discard, p_depth + 1);
		r_uri = encode_data_uri("image/svg+xml", nested.to_utf8_buffer());
		return true;
	}
	if (ext == "jpg" || ext == "jpeg" || ext == "webp") {
		// ThorVG is built here with only the PNG loader, so re-encode rather than
		// hand it a format it would silently skip.
		Ref<Image> img;
		img.instantiate();
		const Error derr = (ext == "webp") ? img->load_webp_from_buffer(bytes) : img->load_jpg_from_buffer(bytes);
		if (derr != OK || img->is_empty()) {
			if (r_warnings) {
				push_unique(*r_warnings, vformat("Could not decode \"%s\".", path));
			}
			return false;
		}
		r_uri = encode_data_uri("image/png", img->save_png_to_buffer());
		return true;
	}

	if (r_warnings) {
		push_unique(*r_warnings, vformat("Unsupported image format \"%s\" referenced by the document.", ext));
	}
	return false;
}

} // namespace

String WebSVGDocument::sanitize(const String &p_markup, PackedStringArray &r_removed) {
	MarkupPolicy policy;
	policy.sanitize = true;
	policy.removed = &r_removed;
	return process_markup(p_markup, policy);
}

String WebSVGDocument::inline_resources(const String &p_markup, const String &p_base_dir, PackedStringArray &r_warnings, int p_depth) {
	if (p_depth >= MAX_INLINE_DEPTH) {
		push_unique(r_warnings, "Nested SVG references exceeded the inlining depth limit.");
		return p_markup;
	}
	MarkupPolicy policy;
	policy.inline_resources = true;
	policy.base_dir = p_base_dir;
	policy.depth = p_depth;
	policy.warnings = &r_warnings;
	return process_markup(p_markup, policy);
}

void WebSVGDocument::scan(const String &p_markup, WebSVGDocumentInfo &r_info) {
	MarkupPolicy policy;
	policy.info = &r_info;
	process_markup(p_markup, policy);
	r_info.has_current_color = p_markup.find("currentColor") != -1;
}

Vector<Vector2i> WebSVGDocument::element_spans(const String &p_markup) {
	Vector<Vector2i> spans;
	// Indices into `spans` of elements whose end tag has not been seen yet.
	Vector<int> open;
	Vector<String> open_names;

	const int n = p_markup.length();
	int i = 0;
	while (i < n) {
		const int lt = p_markup.find_char('<', i);
		if (lt == -1) {
			break;
		}
		Construct c;
		read_construct(p_markup, lt, c);
		if (c.kind == CK_START) {
			spans.push_back(Vector2i(c.start, c.end));
			if (!c.self_closing) {
				open.push_back(spans.size() - 1);
				open_names.push_back(c.name);
			}
		} else if (c.kind == CK_END && !open.is_empty()) {
			// Close the innermost element with a matching name, tolerating the
			// stray unmatched end tags real-world files contain.
			for (int k = open.size() - 1; k >= 0; k--) {
				if (open_names[k] == c.name) {
					for (int m = open.size() - 1; m >= k; m--) {
						spans.write[open[m]].y = c.end;
						open.remove_at(m);
						open_names.remove_at(m);
					}
					break;
				}
			}
		}
		i = MAX(c.end, lt + 1);
	}
	// Anything still open runs to the end of the document.
	for (int k = 0; k < open.size(); k++) {
		spans.write[open[k]].y = n;
	}
	return spans;
}

// --- Root element ---

bool WebSVGDocument::split_root(const String &p_markup, WebSVGDocumentInfo &r_info) {
	r_info.valid = false;
	r_info.root_attributes = String();
	r_info.body = String();

	const int n = p_markup.length();
	int i = 0;
	Construct root;
	bool found = false;

	while (i < n) {
		const int lt = p_markup.find_char('<', i);
		if (lt == -1) {
			break;
		}
		Construct c;
		read_construct(p_markup, lt, c);
		if (c.kind == CK_START) {
			const String lname = c.name.to_lower();
			if (lname == "svg" || lname.ends_with(":svg")) {
				root = c;
				found = true;
				break;
			}
		}
		i = MAX(c.end, lt + 1);
	}
	if (!found) {
		return false;
	}

	r_info.preserve_aspect_ratio = attr_value(root, "preserveAspectRatio").strip_edges();

	const String vb = attr_value(root, "viewBox");
	r_info.has_view_box = false;
	if (!vb.is_empty()) {
		const Vector<String> parts = vb.replace(",", " ").split(" ", false);
		if (parts.size() == 4) {
			r_info.view_box = Rect2(parts[0].to_float(), parts[1].to_float(), parts[2].to_float(), parts[3].to_float());
			r_info.has_view_box = r_info.view_box.size.x > 0 && r_info.view_box.size.y > 0;
		}
	}

	// CSS sizing of replaced elements: explicit width/height, else the viewBox,
	// else the 300x150 default.
	bool w_ok = false, h_ok = false, w_pct = false, h_pct = false;
	const float wv = parse_length(attr_value(root, "width"), &w_ok, &w_pct);
	const float hv = parse_length(attr_value(root, "height"), &h_ok, &h_pct);
	if (w_ok && !w_pct && h_ok && !h_pct && wv > 0 && hv > 0) {
		r_info.intrinsic_size = Vector2(wv, hv);
		r_info.has_intrinsic_size = true;
	} else if (r_info.has_view_box) {
		r_info.intrinsic_size = r_info.view_box.size;
		r_info.has_intrinsic_size = true;
	} else {
		r_info.intrinsic_size = Vector2(300, 150);
		r_info.has_intrinsic_size = false;
	}

	// width/height are re-emitted from the node's box, and xmlns is always written.
	Vector<String> reserved;
	reserved.push_back("width");
	reserved.push_back("height");
	reserved.push_back("xmlns");
	r_info.root_attributes = strip_attributes(p_markup.substr(root.attrs_start, root.attrs_end - root.attrs_start), reserved);

	if (root.self_closing) {
		r_info.valid = true;
		return true;
	}

	// Walk to the matching end tag, tracking depth so a nested `<svg>` cannot end
	// the document early.
	int depth = 1;
	int body_start = root.end;
	int body_end = n;
	i = root.end;
	while (i < n) {
		const int lt = p_markup.find_char('<', i);
		if (lt == -1) {
			break;
		}
		Construct c;
		read_construct(p_markup, lt, c);
		if (c.kind == CK_START && c.name == root.name && !c.self_closing) {
			depth++;
		} else if (c.kind == CK_END && c.name == root.name) {
			depth--;
			if (depth == 0) {
				body_end = c.start;
				break;
			}
		}
		i = MAX(c.end, lt + 1);
	}

	r_info.body = p_markup.substr(body_start, MAX(0, body_end - body_start));
	r_info.valid = true;
	return true;
}
