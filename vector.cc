#include <iostream>
#include <fstream>
#include <string>
#include "vector_tile.pb.h"

extern "C" {
	#include "graphics.h"
}

double *graphics_init() {
	GOOGLE_PROTOBUF_VERIFY_VERSION;

	mapnik::vector::tile *t = new mapnik::vector::tile;

	mapnik::vector::tile_layer *l = t->add_layers();
	l->set_name("layer");
	l->set_version(1);

	return (double *) t;
}

void out(double *src, double *cx, double *cy, int width, int height, int transparency, double gamma, int invert, int color, int color2, int saturate, int mask) {
	mapnik::vector::tile *t = (mapnik::vector::tile *) src;

	std::string s;
	t->SerializeToString(&s);

	std::cout << s;
}

int drawClip(double x0, double y0, double x1, double y1, double *image, double *cx, double *cy, double bright, double hue, int antialias, double thick) {
	return 0;
}

void drawPixel(double x, double y, double *image, double *cx, double *cy, double bright, double hue) {

}

void drawBrush(double x, double y, double *image, double *cx, double *cy, double bright, double brush, double hue) {

}
