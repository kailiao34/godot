/**************************************************************************/
/*  svg_text_path_converter.h                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#pragma once

#include "core/string/ustring.h"

class SVGTextPathConverter {
public:
	// Replaces SVG text elements with ordinary path elements shaped by Godot's
	// TextServer. The returned document remains an SVG and is rasterized by the
	// unmodified ThorVG backend.
	static String convert(const String &p_svg);
};

