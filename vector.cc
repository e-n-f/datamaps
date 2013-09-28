#include <iostream>
#include <fstream>
#include <string>
#include "vector_tile.pb.h"

extern "C" {
	#include "graphics.h"
}

class env {
public:
	mapnik::vector::tile tile;
};

double *graphics_init() {
	GOOGLE_PROTOBUF_VERIFY_VERSION;

	env *e = new env;

	mapnik::vector::tile_layer *l = e->tile.add_layers();
	l->set_name("layer");
	l->set_version(1);

	return (double *) e;
}

void out(double *src, double *cx, double *cy, int width, int height, int transparency, double gamma, int invert, int color, int color2, int saturate, int mask) {
	env *e = (env *) src;

	std::string s;
	e->tile.SerializeToString(&s);

	std::cout << s;
}

int drawClip(double x0, double y0, double x1, double y1, double *image, double *cx, double *cy, double bright, double hue, int antialias, double thick) {
	return 0;
}

void drawPixel(double x, double y, double *image, double *cx, double *cy, double bright, double hue) {

}

void drawBrush(double x, double y, double *image, double *cx, double *cy, double bright, double brush, double hue) {

}
