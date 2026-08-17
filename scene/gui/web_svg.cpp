/**************************************************************************/
/*  web_svg.cpp                                                          */
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

#include "web_svg.h"

#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/io/xml_parser.h"
#include "core/object/class_db.h"
#include "scene/gui/web_svg_element.h"
#include "modules/modules_enabled.gen.h" // For svg.
#include "scene/resources/dpi_texture.h"

#ifdef MODULE_SVG_ENABLED
#include "modules/svg/svg_marker_path_converter.h"
#include "modules/svg/svg_text_path_converter.h"
#endif
#include "scene/theme/theme_db.h"
#include "servers/rendering/rendering_server.h"

Ref<StyleBoxCSS> WebSVG::_get_css_style() const {
	return theme_cache.style;
}

StyleBoxCSS::BoxSizing WebSVG::_resolved_box_sizing() const {
	return theme_cache.box_sizing == StyleBoxCSS::BOX_SIZING_BORDER_BOX ? StyleBoxCSS::BOX_SIZING_BORDER_BOX : StyleBoxCSS::BOX_SIZING_CONTENT_BOX;
}

real_t WebSVG::_border_margin(Side p_side) const {
	Ref<StyleBoxCSS> css = _get_css_style();
	return css.is_valid() ? css->get_effective_border_width(p_side) : 0.0;
}

float WebSVG::_padding(Side p_side) const {
	Ref<StyleBoxCSS> css = _get_css_style();
	if (css.is_valid()) {
		return css->get_padding(p_side);
	}
	switch (p_side) {
		case SIDE_LEFT:
			return theme_cache.padding_left;
		case SIDE_TOP:
			return theme_cache.padding_top;
		case SIDE_RIGHT:
			return theme_cache.padding_right;
		default:
			return theme_cache.padding_bottom;
	}
}

// --- Geometry. The node's rect (get_size) is the SVG width/height; box-sizing
// decides whether border + padding grow outward (content-box) or eat inward
// (border-box), exactly like an HTML <svg>. ---

Size2 WebSVG::get_minimum_size() const {
	const real_t mw = _border_margin(SIDE_LEFT) + _padding(SIDE_LEFT) + _border_margin(SIDE_RIGHT) + _padding(SIDE_RIGHT);
	const real_t mh = _border_margin(SIDE_TOP) + _padding(SIDE_TOP) + _border_margin(SIDE_BOTTOM) + _padding(SIDE_BOTTOM);
	return Size2(mw, mh);
}

Rect2 WebSVG::get_border_box() const {
	const Size2 sz = get_size();
	if (_resolved_box_sizing() == StyleBoxCSS::BOX_SIZING_BORDER_BOX) {
		return Rect2(Point2(), sz);
	}
	// content-box: the node size is the content; border + padding grow outward.
	const real_t mw = _border_margin(SIDE_LEFT) + _padding(SIDE_LEFT) + _border_margin(SIDE_RIGHT) + _padding(SIDE_RIGHT);
	const real_t mh = _border_margin(SIDE_TOP) + _padding(SIDE_TOP) + _border_margin(SIDE_BOTTOM) + _padding(SIDE_BOTTOM);
	return Rect2(Point2(), sz + Size2(mw, mh));
}

Rect2 WebSVG::get_padding_box() const {
	const Rect2 bb = get_border_box();
	return bb.grow_individual(-_border_margin(SIDE_LEFT), -_border_margin(SIDE_TOP), -_border_margin(SIDE_RIGHT), -_border_margin(SIDE_BOTTOM));
}

Rect2 WebSVG::get_content_box() const {
	const Rect2 pb = get_padding_box();
	Rect2 cb = pb.grow_individual(-_padding(SIDE_LEFT), -_padding(SIDE_TOP), -_padding(SIDE_RIGHT), -_padding(SIDE_BOTTOM));
	cb.size = cb.size.maxf(0.0);
	return cb;
}

// --- Content rendering: rasterize at the content-box size (1:1) and draw it
// without scaling, so resizing the node clips rather than stretches. ---

String WebSVG::_document_body(bool p_for_render) const {
	// Animation playback always serializes the typed DOM: the raw body is a
	// static snapshot and cannot reflect per-frame animated values.
	if (!svg_body.is_empty() && !dom_authoritative && !animation_active) {
		// The flattened body is valid under exactly the same conditions as the raw
		// one, since it was produced from it.
		if (p_for_render && !render_body.is_empty()) {
			return render_body;
		}
		return svg_body;
	}
	return WebSVGElement::serialize_children(this);
}

// Runs the text and marker conversions once, so rasterizing does not redo them.
// Both converters return their input untouched when there is nothing to convert,
// which is also what makes running them again during rasterization cheap.
void WebSVG::_update_render_body() {
	render_body = String();
#ifdef MODULE_SVG_ENABLED
	if (svg_body.is_empty() || svg_body.findn("<text") == -1) {
		// Markers alone are rare and cheap; not worth a second document build.
		return;
	}
	// Converted at the document's own size: glyph outlines are emitted in user
	// space, so the result does not depend on the node's current viewport.
	const String probe = _build_svg_document(doc_info.intrinsic_size, false);
	String converted = SVGTextPathConverter::convert(probe);
	converted = SVGMarkerPathConverter::convert(converted);
	if (converted == probe) {
		return; // Nothing was converted.
	}
	WebSVGDocumentInfo converted_info;
	if (WebSVGDocument::split_root(converted, converted_info)) {
		render_body = converted_info.body;
	}
#endif
}

// Maps `fit` onto the SVG attribute pair a browser would use. Returning an empty
// string means "leave the document's own preserveAspectRatio alone".
String WebSVG::_fit_preserve_aspect_ratio(const Size2 &p_viewport) const {
	Fit effective = fit;
	if (effective == FIT_SCALE_DOWN) {
		// `scale-down` is the smaller of `none` and `contain`.
		const Vector2 intrinsic = get_intrinsic_size();
		effective = (p_viewport.x >= intrinsic.x && p_viewport.y >= intrinsic.y) ? FIT_NONE : FIT_CONTAIN;
	}
	if (effective == FIT_NONE) {
		return String();
	}
	if (effective == FIT_FILL) {
		return "none";
	}

	static const char *align_tokens[] = {
		"xMinYMin", "xMidYMin", "xMaxYMin",
		"xMinYMid", "xMidYMid", "xMaxYMid",
		"xMinYMax", "xMidYMax", "xMaxYMax"
	};
	const int idx = CLAMP((int)fit_align, 0, 8);
	return String(align_tokens[idx]) + (effective == FIT_COVER ? " slice" : " meet");
}

String WebSVG::_build_svg_document(const Size2 &p_viewport, bool p_for_render) const {
	// Reuse the source viewBox and inherited presentation attributes so the
	// document scales and paints like the browser; width/height are overridden
	// to the node's box so the viewBox maps onto it.
	String attrs = svg_open_attrs;
	const String par = _fit_preserve_aspect_ratio(p_viewport);
	if (!par.is_empty()) {
		// `fit` overrides whatever the document asked for, and needs a viewBox to
		// act on -- synthesize one from the intrinsic size when the file has none.
		Vector<String> replaced;
		replaced.push_back("viewbox");
		replaced.push_back("preserveaspectratio");
		attrs = WebSVGDocument::strip_attributes(attrs, replaced);

		const Rect2 vb = doc_info.has_view_box ? doc_info.view_box : Rect2(Point2(), get_intrinsic_size());
		if (!attrs.is_empty()) {
			attrs += " ";
		}
		// `num_real(.., false)` rather than rtos(): a synthesized viewBox reads
		// "0 0 80 40" like a hand-written one, instead of "0.0 0.0 80.0 40.0".
		attrs += vformat("viewBox=\"%s %s %s %s\"",
				String::num_real(vb.position.x, false), String::num_real(vb.position.y, false),
				String::num_real(vb.size.x, false), String::num_real(vb.size.y, false));
		attrs += " preserveAspectRatio=\"" + par + "\"";
	}

	String svg = "<svg xmlns=\"http://www.w3.org/2000/svg\"";
	if (!attrs.is_empty()) {
		svg += " " + attrs;
	}
	svg += " width=\"" + rtos(p_viewport.x) + "\" height=\"" + rtos(p_viewport.y) + "\">";
	svg += _document_body(p_for_render);
	svg += "</svg>";
	// CSS `currentColor` resolves to the inherited text color. The flag comes from
	// the document scan so a large static document skips the search entirely.
	if (doc_info.has_current_color) {
		svg = svg.replace("currentColor", "#" + current_color.to_html(false));
	}
	return svg;
}

void WebSVG::_rebuild_texture_if_needed() {
	const Rect2 cb = get_content_box();
	const Size2 vp = cb.size;
	// Rebuilding the document string is O(document), so a static, unresized node
	// must not pay for it every frame. Animation is the one case where the
	// document genuinely changes without anything marking the content dirty.
	if (!content_dirty && !animation_active && vp == last_rendered_content.size) {
		return;
	}

	const String doc = _build_svg_document(vp, true);
	const uint64_t doc_hash = doc.hash64();
	if (!content_dirty && doc_hash == last_rendered_doc_hash && vp == last_rendered_content.size) {
		return;
	}
	content_dirty = false;
	last_rendered_doc_hash = doc_hash;
	last_rendered_content = cb;

	if (vp.x < 1.0 || vp.y < 1.0) {
		content_texture.unref();
		return;
	}

	// DPITexture rasterizes the same ThorVG document but re-rasterizes per DPI
	// scaling level, so the drawing stays sharp on HiDPI displays. The instance is
	// reused rather than recreated: an animated document lands here every frame.
	if (content_texture.is_null()) {
		content_texture.instantiate();
	}
	// Deliberately not probing get_rid() here: it would rasterize at scale 1.0
	// while draw_rect rasterizes again at the viewport's oversampling, doubling
	// the cost of every document change. A document that fails to parse is
	// already reported through document_status.
	content_texture->set_source(doc);
}

void WebSVG::notify_dom_changed() {
	if (building_dom) {
		return;
	}
	dom_authoritative = true;
	content_dirty = true;
	_update_animation_state();
	queue_redraw();
}

void WebSVG::mark_content_dirty() {
	content_dirty = true;
	queue_redraw();
}

// --- SMIL animation playback ---

bool WebSVG::_has_animation_elements(const Node *p_parent) {
	for (int i = 0; i < p_parent->get_child_count(); i++) {
		const Node *child = p_parent->get_child(i);
		if (Object::cast_to<WebSVGAnimation>(child)) {
			return true;
		}
		if (Object::cast_to<WebSVGElement>(child) && _has_animation_elements(child)) {
			return true;
		}
	}
	return false;
}

void WebSVG::_update_animation_state() {
	const bool active = animations_enabled && is_inside_tree() && _has_animation_elements(this);
	if (active == animation_active) {
		return;
	}
	animation_active = active;
	set_process_internal(active);
	if (!active) {
		// Drop stale overrides so the static document serializes base values.
		_apply_animation_frame(this);
		mark_content_dirty();
	}
}

void WebSVG::_apply_animation_frame(Node *p_parent) {
	for (int i = 0; i < p_parent->get_child_count(); i++) {
		Node *child = p_parent->get_child(i);
		WebSVGElement *element = Object::cast_to<WebSVGElement>(child);
		if (!element) {
			continue;
		}
		element->clear_animation_overrides();
		if (animation_active) {
			for (int j = 0; j < element->get_child_count(); j++) {
				WebSVGAnimation *anim = Object::cast_to<WebSVGAnimation>(element->get_child(j));
				if (!anim) {
					continue;
				}
				String value;
				if (anim->evaluate(animation_time, value)) {
					element->set_animation_override(anim->target_attribute(), value);
				}
			}
		}
		_apply_animation_frame(child);
	}
}

void WebSVG::set_current_color(const Color &p_color) {
	if (current_color == p_color) {
		return;
	}
	current_color = p_color;
	mark_content_dirty();
	queue_redraw();
}

Color WebSVG::get_current_color() const {
	return current_color;
}

void WebSVG::set_animations_enabled(bool p_enabled) {
	if (animations_enabled == p_enabled) {
		return;
	}
	animations_enabled = p_enabled;
	animation_time = 0.0;
	_update_animation_state();
	queue_redraw();
}

bool WebSVG::are_animations_enabled() const {
	return animations_enabled;
}

void WebSVG::set_animation_time(double p_seconds) {
	animation_time = MAX(0.0, p_seconds);
	if (animation_active) {
		_apply_animation_frame(this);
	}
	queue_redraw();
}

double WebSVG::get_animation_time() const {
	return animation_time;
}

void WebSVG::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_DRAW: {
			if (theme_cache.style.is_valid()) {
				theme_cache.style->draw(get_canvas_item(), get_border_box());
			}
			_rebuild_texture_if_needed();
			RenderingServer *rs = RenderingServer::get_singleton();
			const Rect2 cb = get_content_box();
			rs->canvas_item_clear(clipped_content_ci);
			rs->canvas_item_set_transform(clipped_content_ci, Transform2D(0.0, cb.position));
			rs->canvas_item_set_custom_rect(clipped_content_ci, !is_visibility_clip_disabled(), Rect2(Point2(), cb.size));
			rs->canvas_item_set_clip(clipped_content_ci, true);
			rs->canvas_item_set_visibility_layer(clipped_content_ci, get_visibility_layer());
			rs->canvas_item_set_default_texture_filter(clipped_content_ci, RSE::CanvasItemTextureFilter(get_texture_filter_in_tree()));
			if (content_texture.is_valid()) {
				// Texture is rasterized at the content-box size, so this draws 1:1.
				content_texture->draw_rect(clipped_content_ci, Rect2(Point2(), content_texture->get_size()), false);
			}
		} break;

		case NOTIFICATION_RESIZED: {
			mark_content_dirty();
		} break;

		case NOTIFICATION_THEME_CHANGED: {
			update_minimum_size();
			mark_content_dirty();
		} break;

		case NOTIFICATION_CHILD_ORDER_CHANGED: {
			if (raw_svg_markup.is_empty()) {
				notify_dom_changed();
			} else {
				_update_animation_state();
			}
		} break;

		case NOTIFICATION_ENTER_TREE: {
			_update_animation_state();
		} break;

		case NOTIFICATION_EXIT_TREE: {
			_update_animation_state();
		} break;

		case NOTIFICATION_INTERNAL_PROCESS: {
			animation_time += get_process_delta_time();
			_apply_animation_frame(this);
			// The document is compared against the last rasterized one at draw
			// time, so unchanged frames (e.g. discrete steps) skip re-rasterizing.
			queue_redraw();
		} break;
	}
}

// --- SVG markup import/export ---

String WebSVG::to_svg_string() const {
	return _build_svg_document(get_content_box().size);
}

void WebSVG::set_svg_markup(const String &p_markup) {
	if (!loading_source) {
		// Editing the markup by hand is a choice of the other input method, so the
		// file reference is dropped rather than silently overwriting the edit on
		// the next reload.
		if (svg_source.is_valid() || !svg_file_path.is_empty()) {
			svg_source.unref();
			svg_file_path = String();
			source_warning = String();
			notify_property_list_changed();
		}
		source_mode = SOURCE_MARKUP;
		source_base_dir = String();
	}
	parse_svg(p_markup);
}

String WebSVG::get_svg_markup() const {
	if (!raw_svg_markup.is_empty() && !dom_authoritative) {
		return raw_svg_markup;
	}
	return to_svg_string();
}

// --- File loading ---

bool WebSVG::_should_build_dom() const {
	switch (dom_mode) {
		case DOM_ALWAYS:
			return true;
		case DOM_NEVER:
			return false;
		default:
			break;
	}
	// Markup is authored to be scripted; an imported file usually is not. The
	// exception is SMIL animation, which ThorVG cannot play -- only the typed DOM
	// and WebSVGAnimation can, so the DOM has to exist for it.
	return source_mode == SOURCE_MARKUP || doc_info.has_animations;
}

Error WebSVG::load_svg_bytes(const PackedByteArray &p_bytes, const String &p_base_dir) {
	String markup;
	const Error err = WebSVGDocument::decode(p_bytes, markup);
	if (err != OK) {
		return err;
	}
	source_mode = SOURCE_FILE;
	return _load_document(markup, p_base_dir);
}

Error WebSVG::load_svg_file(const String &p_path) {
	const String path = p_path.strip_edges();
	ERR_FAIL_COND_V_MSG(path.is_empty(), ERR_INVALID_PARAMETER, "WebSVG: empty SVG file path.");

	// In an exported project the original `.svg` is not packed; its imported
	// DPITexture is, and that resource still carries the full source. Try the
	// resource first so the same path works in the editor and in an export.
	if (path.begins_with("res://")) {
		Ref<Resource> res = ResourceLoader::load(path, "", ResourceFormatLoader::CACHE_MODE_REUSE);
		DPITexture *dpi = Object::cast_to<DPITexture>(res.ptr());
		if (dpi && !dpi->get_source().is_empty()) {
			source_mode = SOURCE_FILE;
			return _load_document(dpi->get_source(), path.get_base_dir());
		}
	}

	Error err = OK;
	const Vector<uint8_t> bytes = FileAccess::get_file_as_bytes(path, &err);
	ERR_FAIL_COND_V_MSG(err != OK, err, vformat("WebSVG: cannot read SVG file \"%s\".", path));

	return load_svg_bytes(bytes, path.get_base_dir());
}

// Pulls the markup out of whichever source is set. `.svg` files import to
// DPITexture in this engine, and DPITexture keeps the source string, so a
// dragged-in file needs no separate importer.
void WebSVG::_refresh_source() {
	source_warning = String();
	if (svg_source.is_null() && svg_file_path.is_empty()) {
		// Two very different situations reach this point, and they must not be
		// confused: the source was *removed*, or there never was one and an option
		// changed. Checking the mode before overwriting it is what tells them
		// apart -- reloading a cleared document would leave the canvas showing
		// artwork the node no longer references.
		const bool source_removed = (source_mode == SOURCE_FILE);
		source_mode = SOURCE_MARKUP;
		loading_source = true;
		if (source_removed) {
			original_markup = String();
			embedded_source = String();
			source_base_dir = String();
			_load_document(String(), String());
		} else if (!original_markup.is_empty()) {
			// Markup mode: re-derive from the untouched original, so toggling
			// `sanitize` or `inline_external_resources` takes effect both ways.
			_load_document(original_markup, String());
		} else {
			source_base_dir = String();
		}
		loading_source = false;
		return;
	}

	loading_source = true;
	source_mode = SOURCE_FILE;

	Error err = ERR_UNAVAILABLE;
	source_warning = String();
	if (svg_source.is_valid()) {
		DPITexture *dpi = Object::cast_to<DPITexture>(svg_source.ptr());
		const String res_path = svg_source->get_path();
		const String ext = res_path.get_extension().to_lower();
		if (dpi && !dpi->get_source().is_empty()) {
			// Imported as DPITexture, which keeps the markup inside the resource.
			err = _load_document(dpi->get_source(), res_path.get_base_dir());
		} else if (ext == "svg" || ext == "svgz") {
			// Imported as a raster texture (the default for `.svg`), so the markup
			// has to come from the file itself. That file is not packed into an
			// export, hence the warning.
			err = load_svg_file(res_path);
			// Embedding already solves the export problem, so there is nothing left
			// to warn about in that case.
			if (err == OK && !embed_source) {
				source_warning = vformat(
						"%s is imported as %s, so its markup comes from the file and will be missing after export.\nFix: set Import As to DPITexture, or enable Embed Source.",
						res_path.get_file(), svg_source->get_class());
			}
		} else {
			source_warning = vformat("\"%s\" is not an SVG file, so there is nothing to render.", res_path);
			WARN_PRINT("WebSVG: " + source_warning);
		}
	} else {
		err = load_svg_file(svg_file_path);
	}

	if (err != OK && !embedded_source.is_empty()) {
		// The source is unreachable (a missing file, or an export that did not
		// include it), but an embedded snapshot can still render.
		_load_document(embedded_source, source_base_dir);
	}
	loading_source = false;
}

Error WebSVG::reload_source() {
	if (svg_source.is_null() && svg_file_path.is_empty()) {
		return ERR_UNCONFIGURED;
	}
	if (svg_source.is_valid()) {
		// Drop the cached resource so an edit made outside the editor is picked up.
		const String res_path = svg_source->get_path();
		if (!res_path.is_empty()) {
			svg_source = ResourceLoader::load(res_path, "", ResourceFormatLoader::CACHE_MODE_IGNORE);
		}
	}
	_refresh_source();
	return OK;
}

void WebSVG::clear() {
	svg_source.unref();
	svg_file_path = String();
	embedded_source = String();
	source_base_dir = String();
	original_markup = String();
	source_warning = String();
	source_mode = SOURCE_MARKUP;
	loading_source = true;
	_load_document(String(), String());
	loading_source = false;
	notify_property_list_changed();
}

void WebSVG::set_svg_source(const Ref<Texture2D> &p_source) {
	if (svg_source == p_source) {
		return;
	}
	svg_source = p_source;
	if (svg_source.is_valid() && !svg_file_path.is_empty()) {
		svg_file_path = String(); // The two inputs are alternatives, not a stack.
	}
	_refresh_source();
	notify_property_list_changed();
}

Ref<Texture2D> WebSVG::get_svg_source() const {
	return svg_source;
}

void WebSVG::set_svg_file_path(const String &p_path) {
	if (svg_file_path == p_path) {
		return;
	}
	svg_file_path = p_path;
	if (!svg_file_path.is_empty() && svg_source.is_valid()) {
		svg_source.unref();
	}
	_refresh_source();
	notify_property_list_changed();
}

String WebSVG::get_svg_file_path() const {
	return svg_file_path;
}

WebSVG::SourceMode WebSVG::get_source_mode() const {
	return source_mode;
}

void WebSVG::set_dom_mode(DomMode p_mode) {
	if (dom_mode == p_mode) {
		return;
	}
	dom_mode = p_mode;
	// The current document has to be re-read: the DOM either appears or goes away.
	// Re-parsing the processed markup keeps this cheap, so the sanitize and inline
	// reports have to be carried across by hand.
	if (!raw_svg_markup.is_empty()) {
		const PackedStringArray removed = doc_info.removed;
		const PackedStringArray warnings = doc_info.warnings;
		_parse_document(raw_svg_markup);
		doc_info.removed = removed;
		doc_info.warnings = warnings;
	}
}

WebSVG::DomMode WebSVG::get_dom_mode() const {
	return dom_mode;
}

void WebSVG::set_embed_source(bool p_embed) {
	if (embed_source == p_embed) {
		return;
	}
	embed_source = p_embed;
	embedded_source = p_embed ? raw_svg_markup : String();
	// Turning embedding on resolves the "markup is missing after export" warning,
	// and turning it off brings it back.
	if (source_mode == SOURCE_FILE) {
		_refresh_source();
	}
	notify_property_list_changed();
}

bool WebSVG::is_embed_source() const {
	return embed_source;
}

void WebSVG::set_sanitize_enabled(bool p_enabled) {
	if (sanitize_enabled == p_enabled) {
		return;
	}
	sanitize_enabled = p_enabled;
	_refresh_source();
}

bool WebSVG::is_sanitize_enabled() const {
	return sanitize_enabled;
}

void WebSVG::set_inline_resources_enabled(bool p_enabled) {
	if (inline_resources_enabled == p_enabled) {
		return;
	}
	inline_resources_enabled = p_enabled;
	_refresh_source();
}

bool WebSVG::is_inline_resources_enabled() const {
	return inline_resources_enabled;
}

void WebSVG::set_embedded_source(const String &p_markup) {
	embedded_source = p_markup;
	// Restored from a scene before the source properties are applied; render it so
	// the node is correct even when the original file cannot be reached.
	if (!p_markup.is_empty() && raw_svg_markup.is_empty()) {
		loading_source = true;
		_load_document(p_markup, source_base_dir);
		loading_source = false;
	}
}

String WebSVG::get_embedded_source() const {
	return embed_source ? embedded_source : String();
}

void WebSVG::set_fit(Fit p_fit) {
	if (fit == p_fit) {
		return;
	}
	fit = p_fit;
	mark_content_dirty();
}

WebSVG::Fit WebSVG::get_fit() const {
	return fit;
}

void WebSVG::set_fit_align(FitAlign p_align) {
	if (fit_align == p_align) {
		return;
	}
	fit_align = p_align;
	mark_content_dirty();
}

WebSVG::FitAlign WebSVG::get_fit_align() const {
	return fit_align;
}

Vector2 WebSVG::get_intrinsic_size() const {
	return doc_info.intrinsic_size;
}

Rect2 WebSVG::get_view_box() const {
	return doc_info.view_box;
}

bool WebSVG::has_view_box() const {
	return doc_info.has_view_box;
}

PackedStringArray WebSVG::get_unsupported_features() const {
	return doc_info.unsupported;
}

String WebSVG::get_document_status() const {
	if (raw_svg_markup.is_empty()) {
		return "No document.";
	}
	String s = vformat("%d x %d, %d elements", (int)doc_info.intrinsic_size.x, (int)doc_info.intrinsic_size.y, doc_info.element_count);
	if (doc_info.has_view_box) {
		s += vformat(", viewBox %s", doc_info.view_box);
	}
	if (doc_info.has_animations) {
		s += ", animated";
	}
	if (!doc_info.unsupported.is_empty()) {
		s += "\nUnsupported: " + String(", ").join(doc_info.unsupported);
	}
	if (!doc_info.removed.is_empty()) {
		s += "\nRemoved: " + String(", ").join(doc_info.removed);
	}
	if (!doc_info.warnings.is_empty()) {
		s += "\nWarnings: " + String(", ").join(doc_info.warnings);
	}
	return s;
}

Dictionary WebSVG::get_document_info() const {
	Dictionary d;
	d["intrinsic_size"] = doc_info.intrinsic_size;
	d["has_intrinsic_size"] = doc_info.has_intrinsic_size;
	d["view_box"] = doc_info.view_box;
	d["has_view_box"] = doc_info.has_view_box;
	d["preserve_aspect_ratio"] = doc_info.preserve_aspect_ratio;
	d["element_count"] = doc_info.element_count;
	d["has_animations"] = doc_info.has_animations;
	d["source_mode"] = (int)source_mode;
	d["dom_built"] = _should_build_dom();
	d["unsupported"] = doc_info.unsupported;
	d["removed"] = doc_info.removed;
	d["warnings"] = doc_info.warnings;
	return d;
}

void WebSVG::_clear_elements() {
	Vector<Node *> to_remove;
	for (int i = 0; i < get_child_count(); i++) {
		if (Object::cast_to<WebSVGElement>(get_child(i))) {
			to_remove.push_back(get_child(i));
		}
	}
	for (Node *n : to_remove) {
		remove_child(n);
		memdelete(n);
	}
}

float WebSVG::_parse_length(const String &p_value) {
	return WebSVGDocument::parse_length(p_value);
}

bool WebSVG::parse_css_color_value(const String &p_value, Color &r_color) {
	return _parse_css_color(p_value, r_color);
}

// Parses a SMIL clock value ("2s", "750ms", "1.5") to seconds.
static float parse_clock_seconds(const String &p_value) {
	String v = p_value.strip_edges().to_lower();
	if (v.is_empty()) {
		return 0.0f;
	}
	if (v.ends_with("ms")) {
		return v.substr(0, v.length() - 2).to_float() / 1000.0f;
	}
	if (v.ends_with("s")) {
		v = v.substr(0, v.length() - 1);
	}
	return v.to_float();
}

bool WebSVG::_parse_css_color(const String &p_value, Color &r_color) {
	String v = p_value.strip_edges();
	if (v.to_lower() == "none" || v.to_lower() == "transparent") {
		return false;
	}
	if (v.to_lower().begins_with("url(")) {
		return false;
	}
	if (v.to_lower().begins_with("rgb")) {
		const int l = v.find_char('(');
		const int r = v.find_char(')');
		if (l != -1 && r > l) {
			const String inner = v.substr(l + 1, r - l - 1);
			Vector<String> parts = inner.split(",", false);
			if (parts.size() >= 3) {
				const float rr = parts[0].strip_edges().to_float() / 255.0;
				const float gg = parts[1].strip_edges().to_float() / 255.0;
				const float bb = parts[2].strip_edges().to_float() / 255.0;
				const float aa = parts.size() >= 4 ? parts[3].strip_edges().to_float() : 1.0;
				r_color = Color(rr, gg, bb, aa);
				return true;
			}
		}
		return true;
	}
	// CSS redefined the 16 basic VGA color names away from their X11 values that
	// Godot's named-color table uses. Override those to match the browser; the
	// extended palette matches X11 so it falls through to Color::named().
	const String lname = v.to_lower();
	struct CSSColor {
		const char *name;
		uint32_t rgb;
	};
	static const CSSColor css_basics[] = {
		{ "black", 0x000000 }, { "silver", 0xC0C0C0 }, { "gray", 0x808080 }, { "grey", 0x808080 },
		{ "white", 0xFFFFFF }, { "maroon", 0x800000 }, { "red", 0xFF0000 }, { "purple", 0x800080 },
		{ "fuchsia", 0xFF00FF }, { "green", 0x008000 }, { "lime", 0x00FF00 }, { "olive", 0x808000 },
		{ "yellow", 0xFFFF00 }, { "navy", 0x000080 }, { "blue", 0x0000FF }, { "teal", 0x008080 },
		{ "aqua", 0x00FFFF }
	};
	for (const CSSColor &cc : css_basics) {
		if (lname == cc.name) {
			r_color = Color::hex((cc.rgb << 8) | 0xFF);
			return true;
		}
	}

	if (Color::html_is_valid(v)) {
		r_color = Color::html(v);
	} else {
		r_color = Color::named(v, Color(0, 0, 0, 1));
	}
	return true;
}

void WebSVG::_apply_presentation_prop(WebSVGElement *p_element, const String &p_key, const String &p_value) const {
	const String key = p_key.strip_edges().to_lower();
	const String val = p_value.strip_edges();
	if (key == "fill") {
		if (val.to_lower().begins_with("url(")) {
			p_element->set_extra_style(p_element->get_extra_style() + key + ":" + val + ";");
			return;
		}
		Color c;
		if (_parse_css_color(val, c)) {
			p_element->set_fill(c);
		} else {
			p_element->set_fill_none(true);
		}
	} else if (key == "fill-opacity") {
		p_element->set_fill_opacity(val.to_float());
	} else if (key == "stroke") {
		if (val.to_lower().begins_with("url(")) {
			p_element->set_stroke_enabled(true);
			p_element->set_extra_style(p_element->get_extra_style() + key + ":" + val + ";");
			return;
		}
		Color c;
		if (_parse_css_color(val, c)) {
			p_element->set_stroke(c);
		} else {
			p_element->set_stroke_enabled(false);
		}
	} else if (key == "stroke-width") {
		p_element->set_stroke_width(_parse_length(val));
	} else if (key == "stroke-opacity") {
		p_element->set_stroke_opacity(val.to_float());
	} else if (key == "opacity") {
		p_element->set_opacity(val.to_float());
	} else if (key == "transform") {
		p_element->set_transform_string(val);
	} else {
		p_element->set_extra_style(p_element->get_extra_style() + key + ":" + val + ";");
	}
}

void WebSVG::_apply_presentation(WebSVGElement *p_element, const String &p_style) const {
	Vector<String> decls = p_style.split(";", false);
	for (const String &decl : decls) {
		const int c = decl.find_char(':');
		if (c == -1) {
			continue;
		}
		_apply_presentation_prop(p_element, decl.substr(0, c), decl.substr(c + 1));
	}
}

void WebSVG::_apply_box_css(const Ref<StyleBoxCSS> &p_sb, const String &p_style) {
	auto parse_lengths = [](const String &v) {
		Vector<float> r;
		Vector<String> t = v.strip_edges().split(" ", false);
		for (const String &s : t) {
			r.push_back(_parse_length(s));
		}
		return r;
	};
	auto style_from_name = [](const String &n) -> StyleBoxCSS::BorderStyle {
		const String s = n.to_lower();
		if (s == "solid") {
			return StyleBoxCSS::BORDER_STYLE_SOLID;
		} else if (s == "dashed") {
			return StyleBoxCSS::BORDER_STYLE_DASHED;
		} else if (s == "dotted") {
			return StyleBoxCSS::BORDER_STYLE_DOTTED;
		} else if (s == "double") {
			return StyleBoxCSS::BORDER_STYLE_DOUBLE;
		}
		return StyleBoxCSS::BORDER_STYLE_NONE;
	};

	Vector<String> decls = p_style.split(";", false);
	for (const String &decl : decls) {
		const int ci = decl.find_char(':');
		if (ci == -1) {
			continue;
		}
		const String key = decl.substr(0, ci).strip_edges().to_lower();
		const String val = decl.substr(ci + 1).strip_edges();

		if (key == "background-color" || key == "background") {
			Color c;
			if (_parse_css_color(val, c)) {
				p_sb->set_background_color(c);
			}
		} else if (key == "background-clip") {
			if (val == "padding-box") {
				p_sb->set_background_clip(StyleBoxCSS::BACKGROUND_CLIP_PADDING_BOX);
			} else if (val == "content-box") {
				p_sb->set_background_clip(StyleBoxCSS::BACKGROUND_CLIP_CONTENT_BOX);
			} else {
				p_sb->set_background_clip(StyleBoxCSS::BACKGROUND_CLIP_BORDER_BOX);
			}
		} else if (key == "border") {
			Vector<String> t = val.split(" ", false);
			for (const String &tok : t) {
				StyleBoxCSS::BorderStyle bs = style_from_name(tok);
				Color c;
				if (tok == "solid" || tok == "dashed" || tok == "dotted" || tok == "double" || tok == "none") {
					p_sb->set_border_style_all(bs);
				} else if (tok.ends_with("px") || tok.is_valid_float()) {
					p_sb->set_border_width_all(_parse_length(tok));
				} else if (_parse_css_color(tok, c)) {
					p_sb->set_border_color_all(c);
				}
			}
		} else if (key == "border-width") {
			Vector<float> w = parse_lengths(val);
			if (w.size() == 1) {
				p_sb->set_border_width_all(w[0]);
			} else if (w.size() >= 4) {
				p_sb->set_border_width(SIDE_TOP, w[0]);
				p_sb->set_border_width(SIDE_RIGHT, w[1]);
				p_sb->set_border_width(SIDE_BOTTOM, w[2]);
				p_sb->set_border_width(SIDE_LEFT, w[3]);
			}
		} else if (key == "border-style") {
			p_sb->set_border_style_all(style_from_name(val));
		} else if (key == "border-color") {
			Color c;
			if (_parse_css_color(val, c)) {
				p_sb->set_border_color_all(c);
			}
		} else if (key == "border-left-width") {
			p_sb->set_border_width(SIDE_LEFT, _parse_length(val));
		} else if (key == "border-top-width") {
			p_sb->set_border_width(SIDE_TOP, _parse_length(val));
		} else if (key == "border-right-width") {
			p_sb->set_border_width(SIDE_RIGHT, _parse_length(val));
		} else if (key == "border-bottom-width") {
			p_sb->set_border_width(SIDE_BOTTOM, _parse_length(val));
		} else if (key == "border-left-color" || key == "border-top-color" || key == "border-right-color" || key == "border-bottom-color") {
			Color c;
			if (_parse_css_color(val, c)) {
				const Side side = key.begins_with("border-left") ? SIDE_LEFT : key.begins_with("border-top") ? SIDE_TOP
						: key.begins_with("border-right")                                                     ? SIDE_RIGHT
																											   : SIDE_BOTTOM;
				p_sb->set_border_color(side, c);
			}
		} else if (key == "border-left-style" || key == "border-top-style" || key == "border-right-style" || key == "border-bottom-style") {
			const Side side = key.begins_with("border-left") ? SIDE_LEFT : key.begins_with("border-top") ? SIDE_TOP
					: key.begins_with("border-right")                                                     ? SIDE_RIGHT
																										   : SIDE_BOTTOM;
			p_sb->set_border_style(side, style_from_name(val));
		} else if (key == "border-left" || key == "border-top" || key == "border-right" || key == "border-bottom") {
			const Side side = key == "border-left" ? SIDE_LEFT : key == "border-top" ? SIDE_TOP
					: key == "border-right"                                           ? SIDE_RIGHT
																					  : SIDE_BOTTOM;
			Vector<String> t = val.split(" ", false);
			for (const String &tok : t) {
				Color c;
				if (tok == "solid" || tok == "dashed" || tok == "dotted" || tok == "double" || tok == "none") {
					p_sb->set_border_style(side, style_from_name(tok));
				} else if (tok.ends_with("px") || tok.is_valid_float()) {
					p_sb->set_border_width(side, _parse_length(tok));
				} else if (_parse_css_color(tok, c)) {
					p_sb->set_border_color(side, c);
				}
			}
		} else if (key == "border-radius") {
			Vector<float> r = parse_lengths(val);
			if (r.size() == 1) {
				p_sb->set_corner_radius_all(Vector2(r[0], r[0]));
			} else if (r.size() == 2) {
				p_sb->set_corner_radius(CORNER_TOP_LEFT, Vector2(r[0], r[0]));
				p_sb->set_corner_radius(CORNER_BOTTOM_RIGHT, Vector2(r[0], r[0]));
				p_sb->set_corner_radius(CORNER_TOP_RIGHT, Vector2(r[1], r[1]));
				p_sb->set_corner_radius(CORNER_BOTTOM_LEFT, Vector2(r[1], r[1]));
			} else if (r.size() >= 4) {
				p_sb->set_corner_radius(CORNER_TOP_LEFT, Vector2(r[0], r[0]));
				p_sb->set_corner_radius(CORNER_TOP_RIGHT, Vector2(r[1], r[1]));
				p_sb->set_corner_radius(CORNER_BOTTOM_RIGHT, Vector2(r[2], r[2]));
				p_sb->set_corner_radius(CORNER_BOTTOM_LEFT, Vector2(r[3], r[3]));
			}
		} else if (key == "padding") {
			Vector<float> p = parse_lengths(val);
			if (p.size() == 1) {
				p_sb->set_padding_all(p[0]);
			} else if (p.size() == 2) {
				p_sb->set_padding(SIDE_TOP, p[0]);
				p_sb->set_padding(SIDE_BOTTOM, p[0]);
				p_sb->set_padding(SIDE_LEFT, p[1]);
				p_sb->set_padding(SIDE_RIGHT, p[1]);
			} else if (p.size() == 3) {
				p_sb->set_padding(SIDE_TOP, p[0]);
				p_sb->set_padding(SIDE_LEFT, p[1]);
				p_sb->set_padding(SIDE_RIGHT, p[1]);
				p_sb->set_padding(SIDE_BOTTOM, p[2]);
			} else if (p.size() >= 4) {
				p_sb->set_padding(SIDE_TOP, p[0]);
				p_sb->set_padding(SIDE_RIGHT, p[1]);
				p_sb->set_padding(SIDE_BOTTOM, p[2]);
				p_sb->set_padding(SIDE_LEFT, p[3]);
			}
		} else if (key == "padding-left") {
			p_sb->set_padding(SIDE_LEFT, _parse_length(val));
		} else if (key == "padding-top") {
			p_sb->set_padding(SIDE_TOP, _parse_length(val));
		} else if (key == "padding-right") {
			p_sb->set_padding(SIDE_RIGHT, _parse_length(val));
		} else if (key == "padding-bottom") {
			p_sb->set_padding(SIDE_BOTTOM, _parse_length(val));
		} else if (key == "box-sizing") {
			p_sb->set_box_sizing(val == "border-box" ? StyleBoxCSS::BOX_SIZING_BORDER_BOX : StyleBoxCSS::BOX_SIZING_CONTENT_BOX);
		} else if (key == "box-shadow") {
			auto split_top_commas = [](const String &v) {
				Vector<String> out;
				int depth = 0;
				int start = 0;
				for (int i = 0; i < v.length(); i++) {
					const char32_t ch = v[i];
					if (ch == '(') {
						depth++;
					} else if (ch == ')') {
						depth = MAX(0, depth - 1);
					} else if (ch == ',' && depth == 0) {
						out.push_back(v.substr(start, i - start));
						start = i + 1;
					}
				}
				out.push_back(v.substr(start));
				return out;
			};
			TypedArray<WebBoxShadow> shadows;
			Vector<String> layers = split_top_commas(val);
			for (const String &layer : layers) {
				if (layer.strip_edges().is_empty()) {
					continue;
				}
				Vector<String> t = layer.strip_edges().split(" ", false);
				Ref<WebBoxShadow> sh;
				sh.instantiate();
				Vector<float> nums;
				bool inset = false;
				bool has_color = false;
				for (const String &tok : t) {
					if (tok.to_lower() == "inset") {
						inset = true;
					} else if (tok.ends_with("px") || tok.is_valid_float() || tok.lstrip("-").is_valid_float()) {
						nums.push_back(_parse_length(tok));
					} else {
						Color c;
						if (_parse_css_color(tok, c)) {
							sh->set_color(c);
							has_color = true;
						}
					}
				}
				if (nums.size() >= 2) {
					sh->set_offset(Vector2(nums[0], nums[1]));
				}
				if (nums.size() >= 3) {
					sh->set_blur_radius(nums[2]);
				}
				if (nums.size() >= 4) {
					sh->set_spread(nums[3]);
				}
				sh->set_inset(inset);
				if (!has_color) {
					sh->set_color(Color(0, 0, 0, 1));
				}
				shadows.push_back(sh);
			}
			p_sb->set_box_shadows(shadows);
		}
	}
}

// The public entry point for every document, whatever its origin: markup typed
// into the Inspector, a dragged-in `.svg`, or bytes handed over at runtime.
Error WebSVG::parse_svg(const String &p_markup) {
	return _load_document(p_markup, source_base_dir);
}

Error WebSVG::_load_document(const String &p_markup, const String &p_base_dir) {
	source_base_dir = p_base_dir;
	original_markup = p_markup;

	PackedStringArray removed;
	PackedStringArray warnings;
	String markup = p_markup;

	// An imported document is untrusted input, and this markup is forwarded to the
	// HTML renderer and served as a page, so sanitizing is not optional there.
	if (sanitize_enabled) {
		markup = WebSVGDocument::sanitize(markup, removed);
	}
	// Inlining referenced artwork makes the document self-contained, which is what
	// both ThorVG (it resolves no external paths) and the server need.
	if (inline_resources_enabled) {
		markup = WebSVGDocument::inline_resources(markup, p_base_dir, warnings);
	}

	const Error err = _parse_document(markup);
	doc_info.removed = removed;
	doc_info.warnings = warnings;
	if (embed_source) {
		embedded_source = markup;
	}
	update_configuration_warnings();
	return err;
}

Error WebSVG::_parse_document(const String &p_markup) {
	raw_svg_markup = p_markup;

	if (p_markup.strip_edges().is_empty()) {
		// An empty document still has to tear down whatever was rendered before.
		doc_info = WebSVGDocumentInfo();
		svg_open_attrs = String();
		svg_body = String();
		render_body = String();
		building_dom = true;
		_clear_elements();
		inline_style.unref();
		building_dom = false;
		dom_authoritative = false;
		_update_render_body();
		_update_animation_state();
		mark_content_dirty();
		update_minimum_size();
		return OK;
	}

	// Root attributes, body, viewBox and intrinsic size, all verbatim.
	WebSVGDocumentInfo info;
	const bool has_root = WebSVGDocument::split_root(p_markup, info);
	WebSVGDocument::scan(p_markup, info);
	doc_info = info;
	svg_open_attrs = info.root_attributes;
	svg_body = info.body;

	if (has_root && info.has_intrinsic_size) {
		// A replaced element with no CSS size takes the document's own size, so the
		// node ends up rendering the artwork 1:1 by default.
		set_size(info.intrinsic_size);
	}

	if (!_should_build_dom()) {
		// The raw body is rendered as-is and stays the source of truth.
		building_dom = true;
		_clear_elements();
		inline_style.unref();
		building_dom = false;
		dom_authoritative = false;
		_update_render_body();
		_update_animation_state();
		mark_content_dirty();
		update_minimum_size();
		return has_root ? OK : ERR_INVALID_DATA;
	}

	Ref<XMLParser> parser;
	parser.instantiate();
	const Error oerr = parser->open_buffer(p_markup.to_utf8_buffer());
	if (oerr != OK) {
		return oerr;
	}

	// Verbatim source for each element in document order, so an element the typed
	// DOM does not model can be preserved instead of dropped.
	const Vector<Vector2i> spans = WebSVGDocument::element_spans(p_markup);
	int element_index = -1;
	bool root_seen = false;

	building_dom = true;
	_clear_elements();
	inline_style.unref();

	Vector<Node *> stack;
	Node *current = this;
	WebSVGText *open_text = nullptr;
	// The innermost non-empty shape/text tag; SMIL <animate> children attach to it.
	WebSVGElement *open_leaf = nullptr;

	while (parser->read() == OK) {
		const XMLParser::NodeType nt = parser->get_node_type();
		if (nt == XMLParser::NODE_TEXT) {
			if (open_text) {
				open_text->set_text(open_text->get_text() + parser->get_node_data().strip_edges());
			}
			continue;
		}
		if (nt == XMLParser::NODE_ELEMENT_END) {
			const String name = parser->get_node_name().to_lower();
			if (name == "text") {
				open_text = nullptr;
				open_leaf = nullptr;
			} else if (name == "g") {
				if (!stack.is_empty()) {
					current = stack[stack.size() - 1];
					stack.remove_at(stack.size() - 1);
				}
			} else if (name == "rect" || name == "circle" || name == "ellipse" || name == "line" || name == "polyline" || name == "polygon" || name == "path") {
				open_leaf = nullptr;
			}
			continue;
		}
		if (nt != XMLParser::NODE_ELEMENT) {
			continue;
		}

		// Kept in step with `spans`, which is indexed by document order over the
		// same set of elements.
		element_index++;

		const String name = parser->get_node_name().to_lower();
		const bool empty = parser->is_empty();

		if (name == "svg" && !root_seen) {
			// Sizing already came from WebSVGDocument::split_root, which follows the
			// CSS replaced-element rules rather than only reading width/height.
			root_seen = true;
			const String style = parser->get_named_attribute_value_safe("style");
			if (!style.is_empty()) {
				inline_style.instantiate();
				_apply_box_css(inline_style, style);
			}
			continue;
		}

		if (name == "animate" || name == "animatetransform") {
			WebSVGAnimation *anim = memnew(WebSVGAnimation);
			if (name == "animatetransform") {
				anim->set_animation_kind(WebSVGAnimation::KIND_ANIMATE_TRANSFORM);
				const String type = parser->get_named_attribute_value_safe("type").strip_edges().to_lower();
				if (type == "scale") {
					anim->set_svg_transform_type(WebSVGAnimation::TRANSFORM_SCALE);
				} else if (type == "rotate") {
					anim->set_svg_transform_type(WebSVGAnimation::TRANSFORM_ROTATE);
				} else if (type == "skewx") {
					anim->set_svg_transform_type(WebSVGAnimation::TRANSFORM_SKEW_X);
				} else if (type == "skewy") {
					anim->set_svg_transform_type(WebSVGAnimation::TRANSFORM_SKEW_Y);
				}
			} else {
				anim->set_attribute_name(parser->get_named_attribute_value_safe("attributeName"));
			}
			anim->set_from_value(parser->get_named_attribute_value_safe("from"));
			anim->set_to_value(parser->get_named_attribute_value_safe("to"));
			anim->set_values(parser->get_named_attribute_value_safe("values"));
			anim->set_key_times(parser->get_named_attribute_value_safe("keyTimes"));
			const String dur = parser->get_named_attribute_value_safe("dur");
			if (!dur.is_empty()) {
				anim->set_duration(parse_clock_seconds(dur));
			}
			const String begin = parser->get_named_attribute_value_safe("begin");
			if (!begin.is_empty()) {
				anim->set_begin_delay(parse_clock_seconds(begin));
			}
			const String repeat = parser->get_named_attribute_value_safe("repeatCount").strip_edges().to_lower();
			anim->set_repeat_count(repeat.is_empty() || repeat == "indefinite" ? 0.0f : repeat.to_float());
			anim->set_fill_freeze(parser->get_named_attribute_value_safe("fill").strip_edges().to_lower() == "freeze");
			anim->set_svg_calc_mode(parser->get_named_attribute_value_safe("calcMode").strip_edges().to_lower() == "discrete" ? WebSVGAnimation::CALC_MODE_DISCRETE : WebSVGAnimation::CALC_MODE_LINEAR);
			anim->set_additive(parser->get_named_attribute_value_safe("additive").strip_edges().to_lower() == "sum");
			const String anim_id = parser->get_named_attribute_value_safe("id");
			if (!anim_id.is_empty()) {
				anim->set_element_id(anim_id);
			}
			(open_leaf ? (Node *)open_leaf : current)->add_child(anim);
			continue;
		}

		WebSVGElement *e = nullptr;
		if (name == "rect") {
			WebSVGRect *r = memnew(WebSVGRect);
			r->set_rect_position(Vector2(_parse_length(parser->get_named_attribute_value_safe("x")), _parse_length(parser->get_named_attribute_value_safe("y"))));
			r->set_rect_size(Vector2(_parse_length(parser->get_named_attribute_value_safe("width")), _parse_length(parser->get_named_attribute_value_safe("height"))));
			r->set_corner_radius(Vector2(_parse_length(parser->get_named_attribute_value_safe("rx")), _parse_length(parser->get_named_attribute_value_safe("ry"))));
			e = r;
		} else if (name == "circle") {
			WebSVGCircle *c = memnew(WebSVGCircle);
			c->set_center(Vector2(_parse_length(parser->get_named_attribute_value_safe("cx")), _parse_length(parser->get_named_attribute_value_safe("cy"))));
			c->set_radius(_parse_length(parser->get_named_attribute_value_safe("r")));
			e = c;
		} else if (name == "ellipse") {
			WebSVGEllipse *el = memnew(WebSVGEllipse);
			el->set_center(Vector2(_parse_length(parser->get_named_attribute_value_safe("cx")), _parse_length(parser->get_named_attribute_value_safe("cy"))));
			el->set_radii(Vector2(_parse_length(parser->get_named_attribute_value_safe("rx")), _parse_length(parser->get_named_attribute_value_safe("ry"))));
			e = el;
		} else if (name == "line") {
			WebSVGLine *ln = memnew(WebSVGLine);
			ln->set_from(Vector2(_parse_length(parser->get_named_attribute_value_safe("x1")), _parse_length(parser->get_named_attribute_value_safe("y1"))));
			ln->set_to(Vector2(_parse_length(parser->get_named_attribute_value_safe("x2")), _parse_length(parser->get_named_attribute_value_safe("y2"))));
			e = ln;
		} else if (name == "polyline" || name == "polygon") {
			WebSVGPolyline *pl = (name == "polygon") ? memnew(WebSVGPolygon) : memnew(WebSVGPolyline);
			PackedVector2Array pts;
			Vector<String> n = parser->get_named_attribute_value_safe("points").replace(",", " ").split(" ", false);
			for (int i = 0; i + 1 < n.size(); i += 2) {
				pts.push_back(Vector2(n[i].to_float(), n[i + 1].to_float()));
			}
			pl->set_points(pts);
			e = pl;
		} else if (name == "path") {
			WebSVGPath *pa = memnew(WebSVGPath);
			pa->set_d(parser->get_named_attribute_value_safe("d"));
			e = pa;
		} else if (name == "text") {
			WebSVGText *tx = memnew(WebSVGText);
			tx->set_text_position(Vector2(_parse_length(parser->get_named_attribute_value_safe("x")), _parse_length(parser->get_named_attribute_value_safe("y"))));
			const String fs = parser->get_named_attribute_value_safe("font-size");
			if (!fs.is_empty()) {
				tx->set_font_size(_parse_length(fs));
			}
			tx->set_font_family(parser->get_named_attribute_value_safe("font-family"));
			e = tx;
			if (!empty) {
				open_text = tx;
			}
		} else if (name == "g") {
			e = memnew(WebSVGGroup);
		} else {
			// An element the typed DOM does not model: <defs>, gradients, <use>,
			// <clipPath>, <mask>, <style>, <image>, filters. Keeping its source
			// verbatim is what makes serializing the DOM lossless -- without it,
			// editing one shape would discard the rest of an imported document.
			WebSVGRaw *raw = memnew(WebSVGRaw);
			raw->set_tag_name(parser->get_node_name());
			if (element_index >= 0 && element_index < spans.size()) {
				const Vector2i span = spans[element_index];
				raw->set_markup(p_markup.substr(span.x, span.y - span.x));
			}
			const String raw_id = parser->get_named_attribute_value_safe("id");
			if (!raw_id.is_empty()) {
				raw->set_element_id(raw_id);
			}
			current->add_child(raw);

			if (!empty) {
				// The whole subtree now lives inside the raw node, so skip it here --
				// while still counting its elements to keep `element_index` aligned.
				const String raw_name = parser->get_node_name();
				int depth = 1;
				while (depth > 0 && parser->read() == OK) {
					const XMLParser::NodeType rnt = parser->get_node_type();
					if (rnt == XMLParser::NODE_ELEMENT) {
						element_index++;
						if (!parser->is_empty() && parser->get_node_name() == raw_name) {
							depth++;
						}
					} else if (rnt == XMLParser::NODE_ELEMENT_END && parser->get_node_name() == raw_name) {
						depth--;
					}
				}
			}
			continue;
		}

		const String fill_attr = parser->get_named_attribute_value_safe("fill");
		if (!fill_attr.is_empty()) {
			_apply_presentation_prop(e, "fill", fill_attr);
		}
		const String fill_op = parser->get_named_attribute_value_safe("fill-opacity");
		if (!fill_op.is_empty()) {
			_apply_presentation_prop(e, "fill-opacity", fill_op);
		}
		const String stroke_attr = parser->get_named_attribute_value_safe("stroke");
		if (!stroke_attr.is_empty()) {
			_apply_presentation_prop(e, "stroke", stroke_attr);
		}
		const String sw = parser->get_named_attribute_value_safe("stroke-width");
		if (!sw.is_empty()) {
			_apply_presentation_prop(e, "stroke-width", sw);
		}
		const String so = parser->get_named_attribute_value_safe("stroke-opacity");
		if (!so.is_empty()) {
			_apply_presentation_prop(e, "stroke-opacity", so);
		}
		const String op = parser->get_named_attribute_value_safe("opacity");
		if (!op.is_empty()) {
			_apply_presentation_prop(e, "opacity", op);
		}
		const String tr = parser->get_named_attribute_value_safe("transform");
		if (!tr.is_empty()) {
			_apply_presentation_prop(e, "transform", tr);
		}
		const String id = parser->get_named_attribute_value_safe("id");
		if (!id.is_empty()) {
			e->set_element_id(id);
		}
		const String style = parser->get_named_attribute_value_safe("style");
		if (!style.is_empty()) {
			_apply_presentation(e, style);
		}

		current->add_child(e);

		if (name == "g" && !empty) {
			stack.push_back(current);
			current = e;
		} else if (!empty) {
			open_leaf = e; // Shapes and <text> may carry SMIL animation children.
		}
	}

	building_dom = false;
	dom_authoritative = false; // Render the raw body so gradients/defs survive.
	_update_render_body();

	if (inline_style.is_valid()) {
		add_theme_style_override("style", inline_style);
		// Mirror padding and box-sizing into the theme constants WebSVG uses for geometry.
		add_theme_constant_override("padding_left", (int)Math::round(inline_style->get_padding(SIDE_LEFT)));
		add_theme_constant_override("padding_top", (int)Math::round(inline_style->get_padding(SIDE_TOP)));
		add_theme_constant_override("padding_right", (int)Math::round(inline_style->get_padding(SIDE_RIGHT)));
		add_theme_constant_override("padding_bottom", (int)Math::round(inline_style->get_padding(SIDE_BOTTOM)));
		add_theme_constant_override("box_sizing", (int)inline_style->get_box_sizing());
	}
	_update_animation_state();
	mark_content_dirty();
	update_minimum_size();
	return OK;
}

Dictionary WebSVG::get_computed_style() const {
	Dictionary d;
	d["border_box"] = get_border_box();
	d["padding_box"] = get_padding_box();
	const Rect2 cb = get_content_box();
	d["content_box"] = cb;
	d["svg_document"] = _build_svg_document(cb.size);
	// What is actually handed to the rasterizer: the same document with text and
	// markers already flattened to paths. Differs from `svg_document` only in that
	// flattening, and only for documents that contain text.
	d["render_document"] = _build_svg_document(cb.size, true);
	d["intrinsic_size"] = doc_info.intrinsic_size;
	d["view_box"] = doc_info.view_box;
	d["has_view_box"] = doc_info.has_view_box;
	d["fit"] = (int)fit;
	d["preserve_aspect_ratio"] = _fit_preserve_aspect_ratio(cb.size);

	Array pad;
	for (int i = 0; i < 4; i++) {
		pad.push_back(_padding((Side)i));
	}
	d["padding"] = pad; // [left, top, right, bottom]
	d["box_sizing"] = _resolved_box_sizing() == StyleBoxCSS::BOX_SIZING_BORDER_BOX ? "border-box" : "content-box";

	Ref<StyleBoxCSS> css = _get_css_style();
	if (css.is_valid()) {
		d["background_color"] = css->get_background_color();
		d["background_clip"] = (int)css->get_background_clip();

		Array bw;
		Array bc;
		Array bs;
		for (int i = 0; i < 4; i++) {
			bw.push_back(css->get_border_width((Side)i));
			bc.push_back(css->get_border_color((Side)i));
			bs.push_back((int)css->get_border_style((Side)i));
		}
		d["border_width"] = bw;
		d["border_color"] = bc;
		d["border_style"] = bs;

		Array radii;
		for (int i = 0; i < 4; i++) {
			radii.push_back(css->get_corner_radius((Corner)i));
		}
		d["corner_radius"] = radii;

		Array shadows;
		TypedArray<WebBoxShadow> sh = css->get_box_shadows();
		for (int i = 0; i < sh.size(); i++) {
			Ref<WebBoxShadow> s = sh[i];
			if (s.is_null()) {
				continue;
			}
			Dictionary sd;
			sd["offset"] = s->get_offset();
			sd["blur_radius"] = s->get_blur_radius();
			sd["spread"] = s->get_spread();
			sd["color"] = s->get_color();
			sd["inset"] = s->is_inset();
			shadows.push_back(sd);
		}
		d["box_shadows"] = shadows;
	}
	return d;
}

void WebSVG::_validate_property(PropertyInfo &p_property) const {
	if (p_property.name == "svg_markup") {
		if (source_mode == SOURCE_FILE) {
			// Hidden rather than shown read-only. The file is the source of truth,
			// so storing it would only duplicate the document into the scene; and
			// feeding a real-world SVG (tens of KB, often on a single line) to the
			// Inspector's multiline text field costs seconds every time the
			// property list is rebuilt. document_status carries the summary, and
			// scripts can still read the markup through get_svg_markup().
			p_property.usage = PROPERTY_USAGE_NONE;
		}
	} else if (p_property.name == "embedded_source") {
		if (!embed_source || source_mode != SOURCE_FILE) {
			p_property.usage = PROPERTY_USAGE_NONE;
		}
	} else if (p_property.name == "fit_align") {
		// Alignment only means something when the aspect ratio is preserved.
		if (fit == FIT_NONE || fit == FIT_FILL) {
			p_property.usage |= PROPERTY_USAGE_READ_ONLY;
		}
	}
}

PackedStringArray WebSVG::get_configuration_warnings() const {
	PackedStringArray warnings = Control::get_configuration_warnings();
	if (!source_warning.is_empty()) {
		warnings.push_back(source_warning);
	}
	for (const String &f : doc_info.unsupported) {
		warnings.push_back(vformat("The document uses %s, which cannot be rendered.", f));
	}
	for (const String &w : doc_info.warnings) {
		warnings.push_back(w);
	}
	if (!doc_info.removed.is_empty()) {
		warnings.push_back("Sanitizing removed active content from the document: " + String(", ").join(doc_info.removed));
	}
	return warnings;
}

void WebSVG::_bind_methods() {
	ClassDB::bind_method(D_METHOD("parse_svg", "markup"), &WebSVG::parse_svg);
	ClassDB::bind_method(D_METHOD("to_svg_string"), &WebSVG::to_svg_string);
	ClassDB::bind_method(D_METHOD("set_svg_markup", "markup"), &WebSVG::set_svg_markup);
	ClassDB::bind_method(D_METHOD("get_svg_markup"), &WebSVG::get_svg_markup);

	ClassDB::bind_method(D_METHOD("get_border_box"), &WebSVG::get_border_box);
	ClassDB::bind_method(D_METHOD("get_padding_box"), &WebSVG::get_padding_box);
	ClassDB::bind_method(D_METHOD("get_content_box"), &WebSVG::get_content_box);
	ClassDB::bind_method(D_METHOD("get_computed_style"), &WebSVG::get_computed_style);

	ClassDB::bind_method(D_METHOD("set_current_color", "color"), &WebSVG::set_current_color);
	ClassDB::bind_method(D_METHOD("get_current_color"), &WebSVG::get_current_color);
	ClassDB::bind_method(D_METHOD("set_animations_enabled", "enabled"), &WebSVG::set_animations_enabled);
	ClassDB::bind_method(D_METHOD("are_animations_enabled"), &WebSVG::are_animations_enabled);
	ClassDB::bind_method(D_METHOD("set_animation_time", "seconds"), &WebSVG::set_animation_time);
	ClassDB::bind_method(D_METHOD("get_animation_time"), &WebSVG::get_animation_time);

	ClassDB::bind_method(D_METHOD("load_svg_file", "path"), &WebSVG::load_svg_file);
	ClassDB::bind_method(D_METHOD("load_svg_bytes", "bytes", "base_dir"), &WebSVG::load_svg_bytes, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("reload_source"), &WebSVG::reload_source);
	ClassDB::bind_method(D_METHOD("clear"), &WebSVG::clear);

	ClassDB::bind_method(D_METHOD("set_svg_source", "source"), &WebSVG::set_svg_source);
	ClassDB::bind_method(D_METHOD("get_svg_source"), &WebSVG::get_svg_source);
	ClassDB::bind_method(D_METHOD("set_svg_file_path", "path"), &WebSVG::set_svg_file_path);
	ClassDB::bind_method(D_METHOD("get_svg_file_path"), &WebSVG::get_svg_file_path);
	ClassDB::bind_method(D_METHOD("get_source_mode"), &WebSVG::get_source_mode);
	ClassDB::bind_method(D_METHOD("set_dom_mode", "mode"), &WebSVG::set_dom_mode);
	ClassDB::bind_method(D_METHOD("get_dom_mode"), &WebSVG::get_dom_mode);
	ClassDB::bind_method(D_METHOD("set_embed_source", "embed"), &WebSVG::set_embed_source);
	ClassDB::bind_method(D_METHOD("is_embed_source"), &WebSVG::is_embed_source);
	ClassDB::bind_method(D_METHOD("set_sanitize_enabled", "enabled"), &WebSVG::set_sanitize_enabled);
	ClassDB::bind_method(D_METHOD("is_sanitize_enabled"), &WebSVG::is_sanitize_enabled);
	ClassDB::bind_method(D_METHOD("set_inline_resources_enabled", "enabled"), &WebSVG::set_inline_resources_enabled);
	ClassDB::bind_method(D_METHOD("is_inline_resources_enabled"), &WebSVG::is_inline_resources_enabled);
	ClassDB::bind_method(D_METHOD("set_embedded_source", "markup"), &WebSVG::set_embedded_source);
	ClassDB::bind_method(D_METHOD("get_embedded_source"), &WebSVG::get_embedded_source);

	ClassDB::bind_method(D_METHOD("set_fit", "fit"), &WebSVG::set_fit);
	ClassDB::bind_method(D_METHOD("get_fit"), &WebSVG::get_fit);
	ClassDB::bind_method(D_METHOD("set_fit_align", "align"), &WebSVG::set_fit_align);
	ClassDB::bind_method(D_METHOD("get_fit_align"), &WebSVG::get_fit_align);

	ClassDB::bind_method(D_METHOD("get_intrinsic_size"), &WebSVG::get_intrinsic_size);
	ClassDB::bind_method(D_METHOD("get_view_box"), &WebSVG::get_view_box);
	ClassDB::bind_method(D_METHOD("has_view_box"), &WebSVG::has_view_box);
	ClassDB::bind_method(D_METHOD("get_document_info"), &WebSVG::get_document_info);
	ClassDB::bind_method(D_METHOD("get_unsupported_features"), &WebSVG::get_unsupported_features);
	ClassDB::bind_method(D_METHOD("get_document_status"), &WebSVG::get_document_status);

	// Restored before the source properties so a source that cannot be reached at
	// load time can still fall back to the embedded snapshot.
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "embedded_source", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NO_EDITOR), "set_embedded_source", "get_embedded_source");

	ADD_GROUP("Source", "");
	// Texture2D rather than DPITexture: `.svg` is claimed by ResourceImporterTexture
	// first (equal priority, registered earlier), so by default it imports as
	// CompressedTexture2D. Restricting the hint to DPITexture would reject the
	// files most projects actually have.
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "svg_source", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_svg_source", "get_svg_source");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "svg_file_path", PROPERTY_HINT_FILE, "*.svg,*.svgz"), "set_svg_file_path", "get_svg_file_path");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "svg_markup", PROPERTY_HINT_MULTILINE_TEXT), "set_svg_markup", "get_svg_markup");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "document_status", PROPERTY_HINT_MULTILINE_TEXT, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_document_status");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "embed_source"), "set_embed_source", "is_embed_source");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "dom_mode", PROPERTY_HINT_ENUM, "Auto,Always,Never"), "set_dom_mode", "get_dom_mode");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "sanitize"), "set_sanitize_enabled", "is_sanitize_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "inline_external_resources"), "set_inline_resources_enabled", "is_inline_resources_enabled");

	ADD_GROUP("Sizing", "");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "fit", PROPERTY_HINT_ENUM, "None,Contain,Cover,Fill,Scale Down"), "set_fit", "get_fit");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "fit_align", PROPERTY_HINT_ENUM, "Top Left,Top Center,Top Right,Center Left,Center,Center Right,Bottom Left,Bottom Center,Bottom Right"), "set_fit_align", "get_fit_align");

	ADD_GROUP("Animation", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "animations_enabled"), "set_animations_enabled", "are_animations_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "animation_time", PROPERTY_HINT_NONE, "suffix:s", PROPERTY_USAGE_NONE), "set_animation_time", "get_animation_time");

	BIND_ENUM_CONSTANT(SOURCE_MARKUP);
	BIND_ENUM_CONSTANT(SOURCE_FILE);

	BIND_ENUM_CONSTANT(DOM_AUTO);
	BIND_ENUM_CONSTANT(DOM_ALWAYS);
	BIND_ENUM_CONSTANT(DOM_NEVER);

	BIND_ENUM_CONSTANT(FIT_NONE);
	BIND_ENUM_CONSTANT(FIT_CONTAIN);
	BIND_ENUM_CONSTANT(FIT_COVER);
	BIND_ENUM_CONSTANT(FIT_FILL);
	BIND_ENUM_CONSTANT(FIT_SCALE_DOWN);

	BIND_ENUM_CONSTANT(ALIGN_TOP_LEFT);
	BIND_ENUM_CONSTANT(ALIGN_TOP_CENTER);
	BIND_ENUM_CONSTANT(ALIGN_TOP_RIGHT);
	BIND_ENUM_CONSTANT(ALIGN_CENTER_LEFT);
	BIND_ENUM_CONSTANT(ALIGN_CENTER);
	BIND_ENUM_CONSTANT(ALIGN_CENTER_RIGHT);
	BIND_ENUM_CONSTANT(ALIGN_BOTTOM_LEFT);
	BIND_ENUM_CONSTANT(ALIGN_BOTTOM_CENTER);
	BIND_ENUM_CONSTANT(ALIGN_BOTTOM_RIGHT);

	BIND_THEME_ITEM_CUSTOM(Theme::DATA_TYPE_STYLEBOX, WebSVG, style, "style");
	BIND_THEME_ITEM_CUSTOM_HINT(Theme::DATA_TYPE_CONSTANT, WebSVG, box_sizing, "box_sizing", PROPERTY_HINT_ENUM, "0 - Content Box:0,1 - Border Box:1");
	BIND_THEME_ITEM_CUSTOM(Theme::DATA_TYPE_CONSTANT, WebSVG, padding_left, "padding_left");
	BIND_THEME_ITEM_CUSTOM(Theme::DATA_TYPE_CONSTANT, WebSVG, padding_top, "padding_top");
	BIND_THEME_ITEM_CUSTOM(Theme::DATA_TYPE_CONSTANT, WebSVG, padding_right, "padding_right");
	BIND_THEME_ITEM_CUSTOM(Theme::DATA_TYPE_CONSTANT, WebSVG, padding_bottom, "padding_bottom");
}

WebSVG::WebSVG() {
	set_mouse_filter(MOUSE_FILTER_STOP);
	set_size(Vector2(300, 150)); // CSS default <svg> intrinsic size.
	clipped_content_ci = RenderingServer::get_singleton()->canvas_item_create();
	RenderingServer::get_singleton()->canvas_item_set_parent(clipped_content_ci, get_canvas_item());
}

WebSVG::~WebSVG() {
	if (clipped_content_ci.is_valid()) {
		RenderingServer::get_singleton()->free_rid(clipped_content_ci);
	}
}
