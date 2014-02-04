#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "util.h"
#include "graphics.h"
#include "clip.h"

struct graphics {
	int width;
	int height;
};

struct graphics *graphics_init(int width, int height, char **filetype) {
	struct graphics *g = malloc(sizeof(struct graphics));

	g->width = width;
	g->height = height;

	*filetype = "txt";
	return g;
}

void out(struct graphics *gc, int transparency, double gamma, int invert, int color, int color2, int saturate, int mask) {
}

// http://rosettacode.org/wiki/Bitmap/Bresenham's_line_algorithm#C
void drawLine(int x0, int y0, int x1, int y1, struct graphics *g, double bright, double hue, long long meta, double thick, struct tilecontext *tc) {
	int dx = abs(x1 - x0), sx = (x0 < x1) ? 1 : -1;
	int dy = abs(y1 - y0), sy = (y0 < y1) ? 1 : -1;
	int err = ((dx > dy) ? dx : -dy) / 2, e2;

	while (1) {
		if (x0 == x1 && y0 == y1) {
			break;
		}

		drawPixel(x0, y0, g, bright, hue, meta, tc);

		int n;
		for (n = 1; n < ceil(thick / 2); n++) {
			if (dx > dy) {
				drawPixel(x0, y0 - n, g, bright, hue, meta, tc);
				drawPixel(x0, y0 + n, g, bright, hue, meta, tc);
			} else {
				drawPixel(x0 - n, y0, g, bright, hue, meta, tc);
				drawPixel(x0 + n, y0, g, bright, hue, meta, tc);
			}
		}

		e2 = err;
		if (e2 > -dx) { 
			err -= dy;
			x0 += sx;
		}
		if (e2 <  dy) {
			err += dx;
			y0 += sy;
		}
	}
}

int drawClip(double x0, double y0, double x1, double y1, struct graphics *g, double bright, double hue, long long meta, int antialias, double thick, struct tilecontext *tc) {
	double xmin = -1 - thick;
	double ymin = -1 - thick;
	double xmax = g->width + thick;
	double ymax = g->height + thick;

	int accept = clip(&x0, &y0, &x1, &y1, xmin, ymin, xmax, ymax);

	if (accept) {
		if (g != NULL) {
			drawLine(x0, y0, x1, y1, g, bright, hue, meta, thick, tc);
		}

		return 1;
	}

	return 0;
}

void drawPixel(double x, double y, struct graphics *g, double bright, double hue, long long meta, struct tilecontext *tc) {
	x -= tc->xoff;
	y -= tc->yoff;

	long long scale = 1LL << (32 - tc->z);
	long long bx = tc->x * scale;
	long long by = tc->y * scale;

	bx += x / g->width * scale;
	by += y / g->height * scale;

	double lat, lon;
	tile2latlon(bx, by, 32, &lat, &lon);
	printf("%.6f,%.6f\n", lat, lon);
}

void drawBrush(double x, double y, struct graphics *g, double bright, double brush, double hue, long long meta, int gaussian, struct tilecontext *tc) {
	drawPixel(x, y, g, bright, hue, meta, tc);
}
