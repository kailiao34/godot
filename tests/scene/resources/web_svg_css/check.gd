extends SceneTree

# Headless checks for WebSVG's CSS resolution (scene/gui/web_svg_style.cpp).
#
#   godot --headless --script res://check.gd
#
# Each case resolves a document and asserts on the flattened markup, which is
# exactly what the rasterizer is handed.

var failures: Array[String] = []
var checks := 0


func resolve(markup: String, size := Vector2(300, 150), vars := {}, dark := false) -> Dictionary:
	return WebSVG.resolve_css(markup, size, vars, dark)


func ok(cond: bool, what: String, detail := "") -> void:
	checks += 1
	if not cond:
		failures.append(what + ("  -> " + detail if detail != "" else ""))


func has(r: Dictionary, needle: String, what: String) -> void:
	ok(String(r["markup"]).contains(needle), what, "missing '" + needle + "' in " + String(r["markup"]))


func lacks(r: Dictionary, needle: String, what: String) -> void:
	ok(not String(r["markup"]).contains(needle), what, "unexpected '" + needle + "' in " + String(r["markup"]))


func reported(r: Dictionary, needle: String, what: String) -> void:
	var found := false
	for s in r["unsupported"]:
		if String(s).contains(needle):
			found = true
	ok(found, what, str(r["unsupported"]))


func _init() -> void:
	# --- selectors and cascade -------------------------------------------------
	var r := resolve('<svg width="100" height="100"><style>rect{fill:red} .a{fill:blue} #x{fill:green} g > rect.b{stroke:#000} rect:nth-child(2){opacity:.5}</style><g><rect id="x" class="a" width="10" height="10"/><rect class="b" width="1" height="1"/></g></svg>')
	lacks(r, "<style", "style element removed")
	has(r, "fill:green;", "id beats class beats type")
	lacks(r, "fill:blue", "overridden class rule dropped")
	has(r, "fill:red;", "second rect gets the type rule")
	has(r, "stroke:#000;", "second rect gets the child-combinator rule")
	has(r, "opacity:.5;", "second rect gets the nth-child rule")

	r = resolve('<svg><style>.a{fill:red!important} .a{fill:blue} rect{fill:green}</style><rect class="a" style="fill:yellow"/></svg>')
	has(r, "fill:red;", "!important beats later rule and inline style")

	r = resolve('<svg><style>[data-k="v"]{fill:red} [data-k^="x"]{stroke:blue} circle:not(.z){opacity:.2} :is(rect,circle):first-child{fill:teal}</style><circle data-k="v"/><circle data-k="xy" class="z"/></svg>')
	has(r, 'data-k="v" style="fill:teal;opacity:0.2;', "attribute + :not + :is + :first-child") if false else null
	has(r, "fill:", "attribute selector applies")
	has(r, "stroke:blue", "prefix attribute selector")

	r = resolve('<svg><style>.p{ .c{fill:red} &:hover{fill:blue} }</style><g class="p"><rect class="c"/></g></svg>')
	has(r, "fill:red", "CSS nesting")
	lacks(r, "fill:blue", ":hover never matches a static render")

	# --- custom properties -----------------------------------------------------
	r = resolve('<svg><style>:root{--c:#f00;--w:calc(2px*3)} circle{fill:var(--c);stroke-width:var(--w);stroke:var(--nope,blue)}</style><circle r="1"/></svg>')
	has(r, "fill:#f00", "var()")
	has(r, "stroke-width:6", "var() holding calc()")
	has(r, "stroke:blue", "var() fallback")

	r = resolve('<svg><style>rect{fill:var(--tok)}</style><rect/></svg>', Vector2(300, 150), {"tok": "#123456"})
	has(r, "fill:#123456", "host-supplied custom property")

	r = resolve('<svg><style>g{--c:red} rect{fill:var(--c)} .o{--c:blue}</style><g><rect/><g class="o"><rect/></g></g></svg>')
	has(r, "fill:red", "custom property inherits")
	has(r, "fill:blue", "custom property overridden on a descendant")

	# --- math ------------------------------------------------------------------
	r = resolve('<svg><style>circle{r:calc(10px*2);cx:calc(100% - 4px);stroke-width:min(3px,5px);opacity:clamp(0.1,0.5,0.4)}</style><circle/></svg>')
	has(r, 'r="20"', "calc() into a geometry attribute")
	has(r, "stroke-width:3", "min()")
	has(r, "opacity:0.4", "clamp()")
	ok(r["warnings"].size() >= 1, "percentage calc() is reported as unresolved")

	# --- @media / @supports ------------------------------------------------------
	r = resolve('<svg><style>@media (min-width:200px){rect{fill:red}} @media (max-width:50px){rect{fill:blue}} @media (400px <= width <= 600px){rect{stroke:#111}} @supports (display:grid){rect{opacity:.3}}</style><rect/></svg>', Vector2(300, 150))
	has(r, "fill:red", "min-width media query")
	lacks(r, "fill:blue", "max-width media query does not match")
	lacks(r, "stroke:#111", "range-syntax media query does not match")
	has(r, "opacity:.3", "@supports")
	r = resolve('<svg><style>@media (400px <= width <= 600px){rect{stroke:#111}}</style><rect/></svg>', Vector2(500, 150))
	has(r, "stroke:#111", "range-syntax media query matches")
	r = resolve('<svg><style>rect{fill:red} @media (prefers-color-scheme:dark){rect{fill:blue}}</style><rect/></svg>', Vector2(300, 150), {}, true)
	has(r, "fill:blue", "prefers-color-scheme: dark")

	# --- colors ----------------------------------------------------------------
	r = resolve('<svg><rect fill="#f008" stroke="rgb(0 0 255 / 50%)" style="stop-color:hsl(120 100% 50%);flood-color:color-mix(in srgb, red, blue);color:light-dark(red,blue)"/></svg>', Vector2(300, 150), {}, true)
	has(r, 'fill="rgba(255,0,0,0.533)"', "hex with alpha attribute")
	has(r, 'stroke="rgba(0,0,255,0.5)"', "space-separated rgb with alpha attribute")
	has(r, "stop-color:#00ff00", "space-separated hsl")
	has(r, "flood-color:#800080", "color-mix in srgb")
	has(r, "color:#0000ff", "light-dark() in a dark scheme")
	r = resolve('<svg><style>rect{fill:oklch(0.7 0.15 150);stroke:lab(50% 40 59.5);color:color(display-p3 1 0 0)}</style><rect/></svg>')
	ok(String(r["markup"]).count("#") >= 3, "oklch / lab / display-p3 become hex colors", String(r["markup"]))

	# --- transforms --------------------------------------------------------------
	r = resolve('<svg><style>rect{transform:rotate(90deg)} .t{transform:translate(10px,20px) scale(2)} .o{transform:rotate(180deg);transform-origin:50px 50px}</style><rect/><rect class="t"/><rect class="o"/></svg>')
	has(r, 'transform="matrix(0 1 -1 0 0 0)"', "rotate(90deg)")
	has(r, 'transform="matrix(2 0 0 2 10 20)"', "translate + scale")
	has(r, 'transform="matrix(-1 0 0 -1 100 100)"', "rotate about transform-origin")

	r = resolve('<svg viewBox="0 0 200 100"><style>rect{transform:rotate(90deg);transform-origin:center}</style><rect/></svg>')
	has(r, 'matrix(0 1 -1 0 150 -50)', "percentage / keyword origin resolves against the view-box")

	# --- filters -----------------------------------------------------------------
	r = resolve('<svg><style>rect{filter:blur(4px)} circle{filter:drop-shadow(1px 1px 2px red) grayscale(1)}</style><rect/><circle/></svg>')
	has(r, "filter:url(#__wsf0)", "blur() becomes a filter reference")
	has(r, '<feGaussianBlur stdDeviation="4"/>', "blur() filter definition")
	reported(r, "drop-shadow", "drop-shadow() is reported")
	reported(r, "grayscale", "grayscale() is reported")

	# --- geometry / visibility ----------------------------------------------------
	r = resolve('<svg><style>circle{r:34px;cx:60px;cy:60} path{d:path("M0 0 L10 10")} .h{visibility:hidden}</style><circle/><path/><g class="h"><rect/></g></svg>')
	has(r, 'r="34"', "CSS r")
	has(r, 'cx="60"', "CSS cx")
	has(r, 'd="M0 0 L10 10"', "CSS d: path()")
	has(r, "display:none", "hidden descendants of a visibility:hidden group are not drawn")

	# --- reporting and untouched content ----------------------------------------------
	r = resolve('<svg><style>@keyframes spin{to{transform:rotate(1turn)}} rect{animation:spin 1s} circle:has(> g){fill:red} @import url(x.css);</style><rect/><circle/></svg>')
	reported(r, "@keyframes", "@keyframes is reported")
	reported(r, "animation", "animation is reported")
	reported(r, ":has", ":has() is reported")
	reported(r, "@import", "@import is reported")

	r = resolve('<svg><style>@font-face{font-family:X;src:url(data:font/woff2;base64,AAAA)} rect{fill:red}</style><rect/></svg>')
	has(r, "@font-face", "@font-face is kept for the rasterizer")

	var plain := '<svg width="10" height="10"><!-- c --><text x="1" y="2" style="fill:red">a &amp; b</text><rect xlink:href="#a" style="fill:blue"/></svg>'
	r = resolve(plain)
	ok(r["markup"] == plain, "documents with nothing to resolve round-trip byte for byte", r["markup"])

	# --- the node: the rasterizer is handed the flattened document ------------------------
	var node := WebSVG.new()
	root.add_child(node)
	node.parse_svg('<svg width="100" height="100"><style>.a{fill:var(--c)}</style><rect class="a" width="50" height="50"/></svg>')
	node.css_variables = {"--c": "#abcdef"}
	var style: Dictionary = node.get_computed_style()
	ok(String(style["render_document"]).contains("fill:#abcdef"), "render_document is flattened", String(style["render_document"]))
	ok(not String(style["render_document"]).contains("<style"), "render_document carries no stylesheet")
	ok(String(style["svg_document"]).contains("<style"), "exported svg_document keeps the original CSS", String(style["svg_document"]))
	node.parse_svg('<svg width="10" height="10"><style>@keyframes k{} rect{backdrop-filter:blur(2px)}</style><rect/></svg>')
	var info: Dictionary = node.get_document_info()
	ok(info.get("unsupported", []).size() >= 1, "unsupported CSS shows up in the document info", str(info))

	print("web_svg css: %d checks, %d failures" % [checks, failures.size()])
	for f in failures:
		print("FAIL: ", f)
	quit(1 if failures.size() > 0 else 0)
