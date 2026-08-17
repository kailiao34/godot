/**************************************************************************/
/*  svg_marker_path_converter.h                                           */
/**************************************************************************/

#pragma once

#include "core/string/ustring.h"

class SVGMarkerPathConverter {
public:
	// Expands SVG marker references into ordinary path groups before ThorVG
	// parses the document.
	static String convert(const String &p_svg);
};

