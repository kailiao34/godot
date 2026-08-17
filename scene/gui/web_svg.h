/**************************************************************************/
/*  web_svg.h                                                             */
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
#include "scene/gui/web_svg_document.h"
#include "scene/resources/style_box_css.h"
#include "scene/resources/texture.h"

class DPITexture;

// A Control that renders an SVG document inside a CSS-faithful box.
//
// Mimics the HTML `<svg>` element: the node's rect size is the SVG width/height,
// and the CSS box model (border, background, box-shadow, padding, box-sizing)
// comes from the `style`, `padding_*` and `box_sizing` theme items.
//
// The document can come from three places, and all three converge on the same
// pipeline (decode -> sanitize -> inline external resources -> parse):
//   * `svg_markup`      - markup typed or pasted directly.
//   * `svg_source`      - a `.svg` file dragged in from the FileSystem dock, which
//                         this engine imports as a DPITexture carrying its source.
//   * `svg_file_path`   - a path, including `user://` and paths outside the
//                         project, loadable at runtime via load_svg_file().
class WebSVG : public Control {
	GDCLASS(WebSVG, Control);

public:
	enum SourceMode {
		SOURCE_MARKUP, // The document came from markup.
		SOURCE_FILE, // The document came from `svg_source` or `svg_file_path`.
	};

	// Whether to mirror the document into typed child nodes. Building the DOM is
	// what makes shapes scriptable, but it costs a Node per element, which is a
	// poor trade for a large imported file nobody intends to edit.
	enum DomMode {
		DOM_AUTO, // Markup: build. File: build only when SMIL animation needs it.
		DOM_ALWAYS,
		DOM_NEVER,
	};

	// CSS `object-fit`, realized by writing `viewBox` and `preserveAspectRatio`
	// onto the emitted root rather than by transforming on the Godot side, so a
	// browser rendering the same markup reaches the same result by spec.
	enum Fit {
		FIT_NONE, // Honor the document's own viewBox/preserveAspectRatio.
		FIT_CONTAIN,
		FIT_COVER,
		FIT_FILL,
		FIT_SCALE_DOWN,
	};

	enum FitAlign {
		ALIGN_TOP_LEFT,
		ALIGN_TOP_CENTER,
		ALIGN_TOP_RIGHT,
		ALIGN_CENTER_LEFT,
		ALIGN_CENTER,
		ALIGN_CENTER_RIGHT,
		ALIGN_BOTTOM_LEFT,
		ALIGN_BOTTOM_CENTER,
		ALIGN_BOTTOM_RIGHT,
	};

private:
	struct ThemeCache {
		Ref<StyleBox> style;
		int box_sizing = 0; // 0 = content-box, 1 = border-box.
		int padding_left = 0;
		int padding_top = 0;
		int padding_right = 0;
		int padding_bottom = 0;
	} theme_cache;

	// Inline `<svg style="...">` box CSS parsed from markup, applied as a style override.
	Ref<StyleBoxCSS> inline_style;

	// Raw inner SVG markup captured by parse_svg() and rendered verbatim (so
	// gradients, <defs>, filters, etc. survive). When the typed child DOM is
	// edited, rendering switches to the serialized DOM instead.
	String raw_svg_markup;
	String svg_body;
	// `svg_body` with <text>, <textPath> and <marker> already flattened to paths.
	// modules/svg does that conversion on every rasterization, and for a
	// text-heavy document it dominates the cost (~120ms for 267 tspans), so it is
	// done once per document here. Empty when the document has nothing to convert.
	// Only ever rendered, never exported: to_svg_string() must keep the real text
	// so a browser renders selectable, accessible text rather than outlines.
	String render_body;
	// Attributes from the source `<svg …>` opening tag (viewBox plus inherited
	// presentation attrs like fill/stroke), minus width/height/xmlns which the
	// rebuilt document sets itself. Preserving these keeps scaling and default
	// paint identical to the browser.
	String svg_open_attrs;
	bool dom_authoritative = false;
	bool building_dom = false;

	// Resolves CSS `currentColor` in the markup, matching the element's
	// inherited `color` in the browser.
	Color current_color = Color(0, 0, 0, 1);

	// --- Document source ---
	Ref<Texture2D> svg_source; // A `.svg` imported as DPITexture.
	String svg_file_path;
	String embedded_source; // Storage-only snapshot, kept when `embed_source` is on.
	String source_base_dir; // Resolves relative `<image href>` references.
	// The document exactly as it arrived, before sanitizing and inlining. Kept so
	// toggling those options re-derives from the original rather than from an
	// already-processed copy, which could not restore what was removed.
	String original_markup;
	// Set when the assigned `svg_source` works but is not ideal (imported as a
	// raster texture) or is unusable. Surfaced as a scene-tree warning.
	String source_warning;
	SourceMode source_mode = SOURCE_MARKUP;
	DomMode dom_mode = DOM_AUTO;
	bool embed_source = false;
	bool sanitize_enabled = true;
	bool inline_resources_enabled = true;
	// Set while a source property drives the load, so the markup setter does not
	// mistake the resulting document for a hand-edit and detach the source.
	bool loading_source = false;

	// Sizing, viewBox and the reporting surfaced by get_document_info().
	WebSVGDocumentInfo doc_info;
	Fit fit = FIT_NONE;
	FitAlign fit_align = ALIGN_CENTER;

	// Content rasterization cache.
	uint64_t last_rendered_doc_hash = 0;
	Rect2 last_rendered_content;
	Ref<DPITexture> content_texture;
	RID clipped_content_ci;
	bool content_dirty = true;

	// SMIL animation playback. When the DOM contains WebSVGAnimation elements
	// and playback is enabled, the document is re-serialized with the current
	// animated values every frame (re-rasterizing only when it changed).
	bool animations_enabled = true;
	double animation_time = 0.0;
	bool animation_active = false;

	static bool _has_animation_elements(const Node *p_parent);
	void _update_animation_state();
	void _apply_animation_frame(Node *p_parent);

	Ref<StyleBoxCSS> _get_css_style() const;
	StyleBoxCSS::BoxSizing _resolved_box_sizing() const;
	real_t _border_margin(Side p_side) const;
	float _padding(Side p_side) const;

	// `p_for_render` picks the flattened body when one is cached; the exported
	// document always uses the original.
	String _document_body(bool p_for_render) const;
	String _build_svg_document(const Size2 &p_viewport, bool p_for_render = false) const;
	void _update_render_body();
	String _fit_preserve_aspect_ratio(const Size2 &p_viewport) const;
	void _rebuild_texture_if_needed();

	// decode -> sanitize -> inline external resources -> parse.
	Error _load_document(const String &p_markup, const String &p_base_dir);
	Error _parse_document(const String &p_markup);
	// Reads whichever of `svg_source` / `svg_file_path` is set, or clears when
	// neither is.
	void _refresh_source();
	bool _should_build_dom() const;

	void _clear_elements();
	static bool _parse_css_color(const String &p_value, Color &r_color);
	static float _parse_length(const String &p_value);
	void _apply_presentation_prop(class WebSVGElement *p_element, const String &p_key, const String &p_value) const;
	void _apply_presentation(class WebSVGElement *p_element, const String &p_style) const;
	void _apply_box_css(const Ref<StyleBoxCSS> &p_sb, const String &p_style);

protected:
	void _notification(int p_what);
	void _validate_property(PropertyInfo &p_property) const;
	static void _bind_methods();

public:
	virtual Size2 get_minimum_size() const override;
	virtual PackedStringArray get_configuration_warnings() const override;

	// Parses a CSS color value ("#abc", "rgb(...)", named). Returns false for
	// `none`/`transparent`/`url(...)`. Shared with WebSVGAnimation interpolation.
	static bool parse_css_color_value(const String &p_value, Color &r_color);

	// SVG markup import/export.
	Error parse_svg(const String &p_markup);
	String to_svg_string() const;
	void set_svg_markup(const String &p_markup);
	String get_svg_markup() const;

	// --- File loading. All of these are available at runtime. ---

	// Loads any readable path: `res://`, `user://`, or an absolute OS path. `res://`
	// paths go through ResourceLoader first so they keep working in exported
	// projects, where the original `.svg` is replaced by its imported resource.
	Error load_svg_file(const String &p_path);
	// Loads a document already in memory. Handles gzip (`.svgz`), a BOM, and UTF-8
	// or UTF-16. `p_base_dir` resolves relative `<image href>` references.
	Error load_svg_bytes(const PackedByteArray &p_bytes, const String &p_base_dir = String());
	// Re-reads the current source, picking up edits made to the file on disk.
	Error reload_source();
	// Drops the document and any source reference.
	void clear();

	void set_svg_source(const Ref<Texture2D> &p_source);
	Ref<Texture2D> get_svg_source() const;
	void set_svg_file_path(const String &p_path);
	String get_svg_file_path() const;

	SourceMode get_source_mode() const;
	void set_dom_mode(DomMode p_mode);
	DomMode get_dom_mode() const;
	void set_embed_source(bool p_embed);
	bool is_embed_source() const;
	void set_sanitize_enabled(bool p_enabled);
	bool is_sanitize_enabled() const;
	void set_inline_resources_enabled(bool p_enabled);
	bool is_inline_resources_enabled() const;

	void set_fit(Fit p_fit);
	Fit get_fit() const;
	void set_fit_align(FitAlign p_align);
	FitAlign get_fit_align() const;

	// The document's own size, per the CSS rules for replaced elements: explicit
	// width/height, else the viewBox, else 300x150.
	Vector2 get_intrinsic_size() const;
	Rect2 get_view_box() const;
	bool has_view_box() const;
	// Sizing, element counts, features that will not render, and content that
	// sanitizing removed.
	Dictionary get_document_info() const;
	// Features present in the document that ThorVG cannot render.
	PackedStringArray get_unsupported_features() const;
	// One-line summary of the above, shown read-only in the Inspector.
	String get_document_status() const;

	// Storage-only mirror of the document, used when `embed_source` is on.
	void set_embedded_source(const String &p_markup);
	String get_embedded_source() const;

	// Called by child WebSVGElement nodes when their content changes.
	void notify_dom_changed();
	void mark_content_dirty();

	// CSS `currentColor` resolution (the inherited text color).
	void set_current_color(const Color &p_color);
	Color get_current_color() const;

	// SMIL animation playback.
	void set_animations_enabled(bool p_enabled);
	bool are_animations_enabled() const;
	void set_animation_time(double p_seconds);
	double get_animation_time() const;

	// Resolved box geometry, used by the headless computed-style bridge and tests.
	Rect2 get_border_box() const;
	Rect2 get_padding_box() const;
	Rect2 get_content_box() const;
	Dictionary get_computed_style() const;

	WebSVG();
	~WebSVG();
};

VARIANT_ENUM_CAST(WebSVG::SourceMode);
VARIANT_ENUM_CAST(WebSVG::DomMode);
VARIANT_ENUM_CAST(WebSVG::Fit);
VARIANT_ENUM_CAST(WebSVG::FitAlign);
