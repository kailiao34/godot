extends SceneTree

# Rasterizes each case through the same path WebSVG uses (flattened document ->
# ThorVG) and writes PNGs next to the case SVGs, for comparison with a browser.
#   godot --headless --path . --script res://render_cases.gd -- <out_dir>

func _init() -> void:
	var out := OS.get_cmdline_user_args()[0]
	DirAccess.make_dir_recursive_absolute(out)
	var dir := DirAccess.open("res://cases")
	for f in dir.get_files():
		if not f.ends_with(".svg"):
			continue
		var markup := FileAccess.get_file_as_string("res://cases/" + f)
		var node := WebSVG.new()
		root.add_child(node)
		node.parse_svg(markup)
		var doc: String = node.get_computed_style()["render_document"]
		var img := Image.new()
		var err := img.load_svg_from_string(doc, 1.0)
		if err != OK:
			print("ERR ", f, " ", err)
		else:
			img.save_png(out.path_join(f.get_basename() + ".png"))
			print("ok ", f, " ", img.get_size(), " unsupported=", node.get_document_info().get("unsupported", []))
		node.queue_free()
	quit()
