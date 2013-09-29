#include <iostream>
#include <fstream>
#include <string>
#include <zlib.h>
#include "vector_tile.pb.h"

#define XMAX 4096
#define YMAX 4096

extern "C" {
	#include "graphics.h"
	#include "clip.h"
}

class env {
public:
	mapnik::vector::tile tile;
	mapnik::vector::tile_layer *layer;
	mapnik::vector::tile_feature *feature;

	int x;
	int y;

	int cmd_idx;
	int cmd;
	int length;
};

#define MOVE_TO 1
#define LINE_TO 2
#define CLOSE_PATH 7
#define CMD_BITS 3

double *graphics_init() {
	GOOGLE_PROTOBUF_VERIFY_VERSION;

	env *e = new env;

	e->layer = e->tile.add_layers();
	e->layer->set_name("world");
	e->layer->set_version(1);
	e->layer->set_extent(4096);

	e->feature = e->layer->add_features();
	e->feature->set_type(mapnik::vector::tile::LineString);

	e->x = 0;
	e->y = 0;

	e->cmd_idx = -1;
	e->cmd = -1;
	e->length = 0;

	return (double *) e;
}

// from mapnik-vector-tile/src/vector_tile_compression.hpp
static inline int compress(std::string const& input, std::string & output)
{
	z_stream deflate_s;
	deflate_s.zalloc = Z_NULL;
	deflate_s.zfree = Z_NULL;
	deflate_s.opaque = Z_NULL;
	deflate_s.avail_in = 0;
	deflate_s.next_in = Z_NULL;
	deflateInit(&deflate_s, Z_DEFAULT_COMPRESSION);
	deflate_s.next_in = (Bytef *)input.data();
	deflate_s.avail_in = input.size();
	size_t length = 0;
	do {
		size_t increase = input.size() / 2 + 1024;
		output.resize(length + increase);
		deflate_s.avail_out = increase;
		deflate_s.next_out = (Bytef *)(output.data() + length);
		int ret = deflate(&deflate_s, Z_FINISH);
		if (ret != Z_STREAM_END && ret != Z_OK && ret != Z_BUF_ERROR) {
			return -1;
		}
		length += (increase - deflate_s.avail_out);
	} while (deflate_s.avail_out == 0);
	deflateEnd(&deflate_s);
	output.resize(length);
	return 0;
}

void out(double *src, double *cx, double *cy, int width, int height, int transparency, double gamma, int invert, int color, int color2, int saturate, int mask) {
	env *e = (env *) src;

	if (e->cmd_idx >= 0) {
		//printf("old command: %d %d\n", e->cmd, e->length);
		e->feature->set_geometry(e->cmd_idx, 
			(e->length << CMD_BITS) |
			(e->cmd & ((1 << CMD_BITS) - 1)));
	}

	std::string s;
	e->tile.SerializeToString(&s);

	std::string compressed;
	compress(s, compressed);

	std::cout << compressed;
}


static void op(env *e, int cmd, int x, int y) {
	if (cmd != e->cmd) {
		if (e->cmd_idx >= 0) {
			//printf("old command: %d %d\n", e->cmd, e->length);
			e->feature->set_geometry(e->cmd_idx, 
				(e->length << CMD_BITS) |
				(e->cmd & ((1 << CMD_BITS) - 1)));
		}

		e->cmd = cmd;
		e->length = 0;
		e->cmd_idx = e->feature->geometry_size();

		e->feature->add_geometry(0); // placeholder
	}

	if (cmd == MOVE_TO || cmd == LINE_TO) {
		int dx = x - e->x;
		int dy = y - e->y;
		//printf("new geom: %d %d\n", x, y);

		e->feature->add_geometry((dx << 1) ^ (dx >> 31));
		e->feature->add_geometry((dy << 1) ^ (dy >> 31));
		
		e->x = x;
		e->y = y;
		e->length++;
	} else if (cmd == CLOSE_PATH) {
		e->length++;
	}
}

int drawClip(double x0, double y0, double x1, double y1, double *image, double *cx, double *cy, double bright, double hue, int antialias, double thick) {
	int accept = clip(&x0, &y0, &x1, &y1, 0, 0, XMAX / 16.0, YMAX / 16.0);

	if (accept) {
		int xx0 = x0 * 16;
		int yy0 = y0 * 16;
		int xx1 = x1 * 16;
		int yy1 = y1 * 16;

		// Guarding against rounding error

		if (xx0 < 0) {
			xx0 = 0;
		}
		if (xx0 > 4095) {
			xx0 = 4095;
		}
		if (yy0 < 0) {
			yy0 = 0;
		}
		if (yy0 > 4095) {
			yy0 = 4095;
		}

		if (xx1 < 0) {
			xx1 = 0;
		}
		if (xx1 > 4095) {
			xx1 = 4095;
		}
		if (yy1 < 0) {
			yy1 = 0;
		}
		if (yy1 > 4095) {
			yy1 = 4095;
		}

		env *e = (env *) image;

		op(e, MOVE_TO, xx0, yy0);
		op(e, LINE_TO, xx1, yy1);
		// op(e, CLOSE_PATH, 0, 0);
	}

	return 0;
}

void drawPixel(double x, double y, double *image, double *cx, double *cy, double bright, double hue) {

}

void drawBrush(double x, double y, double *image, double *cx, double *cy, double bright, double brush, double hue) {

}
