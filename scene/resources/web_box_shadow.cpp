/**************************************************************************/
/*  web_box_shadow.cpp                                                    */
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

#include "web_box_shadow.h"

#include "core/object/class_db.h"

void WebBoxShadow::set_offset(const Vector2 &p_offset) {
	offset = p_offset;
	emit_changed();
}

Vector2 WebBoxShadow::get_offset() const {
	return offset;
}

void WebBoxShadow::set_blur_radius(real_t p_blur_radius) {
	blur_radius = MAX(0.0, p_blur_radius);
	emit_changed();
}

real_t WebBoxShadow::get_blur_radius() const {
	return blur_radius;
}

void WebBoxShadow::set_spread(real_t p_spread) {
	spread = p_spread;
	emit_changed();
}

real_t WebBoxShadow::get_spread() const {
	return spread;
}

void WebBoxShadow::set_color(const Color &p_color) {
	color = p_color;
	emit_changed();
}

Color WebBoxShadow::get_color() const {
	return color;
}

void WebBoxShadow::set_inset(bool p_inset) {
	inset = p_inset;
	emit_changed();
}

bool WebBoxShadow::is_inset() const {
	return inset;
}

void WebBoxShadow::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_offset", "offset"), &WebBoxShadow::set_offset);
	ClassDB::bind_method(D_METHOD("get_offset"), &WebBoxShadow::get_offset);

	ClassDB::bind_method(D_METHOD("set_blur_radius", "blur_radius"), &WebBoxShadow::set_blur_radius);
	ClassDB::bind_method(D_METHOD("get_blur_radius"), &WebBoxShadow::get_blur_radius);

	ClassDB::bind_method(D_METHOD("set_spread", "spread"), &WebBoxShadow::set_spread);
	ClassDB::bind_method(D_METHOD("get_spread"), &WebBoxShadow::get_spread);

	ClassDB::bind_method(D_METHOD("set_color", "color"), &WebBoxShadow::set_color);
	ClassDB::bind_method(D_METHOD("get_color"), &WebBoxShadow::get_color);

	ClassDB::bind_method(D_METHOD("set_inset", "inset"), &WebBoxShadow::set_inset);
	ClassDB::bind_method(D_METHOD("is_inset"), &WebBoxShadow::is_inset);

	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "offset", PROPERTY_HINT_NONE, "suffix:px"), "set_offset", "get_offset");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "blur_radius", PROPERTY_HINT_RANGE, "0,100,0.1,or_greater,suffix:px"), "set_blur_radius", "get_blur_radius");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "spread", PROPERTY_HINT_RANGE, "-100,100,0.1,or_greater,or_less,suffix:px"), "set_spread", "get_spread");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "color"), "set_color", "get_color");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "inset"), "set_inset", "is_inset");
}
