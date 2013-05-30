#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <png.h>
#include <math.h>
#include "util.h"

void out(double *src, int *chroma, int width, int height, int transparency) {
	unsigned char buf[width * height * 4];

	int midr, midg, midb;

	double limit2 = 1;
	double limit = limit2 / 2;

	double gamma = .5;

	int i;
	for (i = 0; i < width * height; i++) {
		if (chroma[i] == 0) {
			midr = midg = midb = 128;
		} else {
			// http://basecase.org/env/on-rainbows
			double h = chroma[i] / 3348.0;
			h += .5;
			h *= -1;
			double r1 = sin(M_PI * h);
			double g1 = sin(M_PI * (h + 1.0/3));
			double b1 = sin(M_PI * (h + 2.0/3));
			midr = 255 * (r1 * r1);
			midg = 255 * (g1 * g1);
			midb = 255 * (b1 * b1);
		}

		if (src[i] == 0) {
			buf[4 * i + 0] = 0;
			buf[4 * i + 1] = 0;
			buf[4 * i + 2] = 0;
			buf[4 * i + 3] = transparency;
		} else {
			if (gamma != 1) {
				src[i] = exp(log(src[i]) * gamma);
			}

			if (src[i] <= limit) {
				buf[4 * i + 0] = midr * (src[i] / limit);
				buf[4 * i + 1] = midg * (src[i] / limit);
				buf[4 * i + 2] = midb * (src[i] / limit);
				buf[4 * i + 3] = 255 * (src[i] / limit) +
						  transparency * (1 - (src[i] / limit));
			} else if (src[i] <= limit2) {
				double along = (src[i] - limit) / (limit2 - limit);
				buf[4 * i + 0] = 255 * along + midr * (1 - along);
				buf[4 * i + 1] = 255 * along + midg * (1 - along);
				buf[4 * i + 2] = 255 * along + midb * (1 - along);
				buf[4 * i + 3] = 255;
			} else {
				buf[4 * i + 0] = 255;
				buf[4 * i + 1] = 255;
				buf[4 * i + 2] = 255;
				buf[4 * i + 3] = 255;
			}
		}
	}

	png_image image;

	memset(&image, 0, sizeof image);
	image.version = PNG_IMAGE_VERSION;
	image.format = PNG_FORMAT_RGBA;
	image.width = width;
	image.height = height;

	png_image_write_to_stdio(&image, stdout, 0, buf, 4 * width, NULL);
	png_image_free(&image);
}

static void putPixel(int x0, int y0, double bright, double *image, int *chroma, int meta) {
	if (x0 >= 0 && y0 >= 0 && x0 <= 255 && y0 <= 255) {
		image[y0 * 256 + x0] += bright;

		if (bright > .0001) {
			chroma[y0 * 256 + x0] = meta;
		}
	}
}

static double fpart(double x) {
	return x - floor(x);
}

static double rfpart(double x) {
	return 1 - fpart(x);
}

// loosely based on
// http://en.wikipedia.org/wiki/Xiaolin_Wu's_line_algorithm
static void antialiasedLine(double x0, double y0, double x1, double y1, double *image, int *chroma, double bright, int meta) {
	int steep = fabs(y1 - y0) > fabs(x1 - x0);

	if (steep) {
		double tmp = x0;
		x0 = y0;
		y0 = tmp;

		tmp = x1;
		x1 = y1;
		y1 = tmp;
	}

	if (x0 > x1) {
		double tmp = x0;
		x0 = x1;
		x1 = tmp;

		tmp = y0;
		y0 = y1;
		y1 = tmp;
	}

	double dx = x1 - x0;
	double dy = y1 - y0;
	double gradient = dy / dx;

	// start and end of line are inside the same pixel.
	if (floor(x0) == floor(x1)) {
		y0 = (y0 + y1) / 2;

		if (steep) {
			putPixel(y0,     x0, dx * rfpart(y0) * bright, image, chroma, meta);
			putPixel(y0 + 1, x0, dx *  fpart(y0) * bright, image, chroma, meta);
		} else {
			putPixel(x0, y0,     dx * rfpart(y0) * bright, image, chroma, meta);
			putPixel(x0, y0 + 1, dx *  fpart(y0) * bright, image, chroma, meta);
		}

		return;
	}

	// there is a fractional pixel at the start
	if (x0 != floor(x0)) {
		double yy = y0 + .5 * rfpart(x0) * gradient;

		if (steep) {
			putPixel(yy,     x0, rfpart(x0) * rfpart(yy) * bright, image, chroma, meta);
			putPixel(yy + 1, x0, rfpart(x0) *  fpart(yy) * bright, image, chroma, meta);
		} else {
			putPixel(x0, yy,     rfpart(x0) * rfpart(yy) * bright, image, chroma, meta);
			putPixel(x0, yy + 1, rfpart(x0) *  fpart(yy) * bright, image, chroma, meta);
		}

		y0 += gradient * rfpart(x0);
		x0 = ceil(x0);
	}

	// there is a fractional pixel at the end
	if (x1 != floor(x1)) {
		double yy = y1 - .5 * fpart(x1) * gradient;

		if (steep) {
			putPixel(yy,     x1, fpart(x1) * rfpart(yy) * bright, image, chroma, meta);
			putPixel(yy + 1, x1, fpart(x1) *  fpart(yy) * bright, image, chroma, meta);
		} else {
			putPixel(x1, yy,     fpart(x1) * rfpart(yy) * bright, image, chroma, meta);
			putPixel(x1, yy + 1, fpart(x1) *  fpart(yy) * bright, image, chroma, meta);
		}

		y1 -= gradient * fpart(x1);
		x1 = floor(x1);
	}

	// now there are only whole pixels along the path

	// the middle of each whole pixel is halfway through a step
	y0 += .5 * gradient;

	for (; x0 < x1; x0++) {
		if (steep) {
			putPixel(y0,     x0, rfpart(y0) * bright, image, chroma, meta);
			putPixel(y0 + 1, x0,  fpart(y0) * bright, image, chroma, meta);
		} else {
			putPixel(x0, y0,     rfpart(y0) * bright, image, chroma, meta);
			putPixel(x0, y0 + 1,  fpart(y0) * bright, image, chroma, meta);
		}

		y0 += gradient;
	}
}

#define INSIDE 0
#define LEFT 1
#define RIGHT 2
#define BOTTOM 4
#define TOP 8

// XXX Why doesn't this look right with 0..255?
// Because of not drawing the last point?
#define XMIN -1
#define YMIN -1
#define XMAX 256
#define YMAX 256

static int computeOutCode(double x, double y) {
	int code = INSIDE;

	if (x < XMIN) {
		code |= LEFT;
	} else if (x > XMAX) {
		code |= RIGHT;
	}

	if (y < YMIN) {
		code |= BOTTOM;
	} else if (y > YMAX) {
		code |= TOP;
	}

	return code;
}

// http://en.wikipedia.org/wiki/Cohen%E2%80%93Sutherland_algorithm
void drawClip(double x0, double y0, double x1, double y1, double *image, int *chroma, double bright, int meta) {
	int outcode0 = computeOutCode(x0, y0);
	int outcode1 = computeOutCode(x1, y1);
	int accept = 0;
 
	while (1) {
		if (!(outcode0 | outcode1)) { // Bitwise OR is 0. Trivially accept and get out of loop
			accept = 1;
			break;
		} else if (outcode0 & outcode1) { // Bitwise AND is not 0. Trivially reject and get out of loop
			break;
		} else {
			// failed both tests, so calculate the line segment to clip
			// from an outside point to an intersection with clip edge
			double x = x0, y = y0;
 
			// At least one endpoint is outside the clip rectangle; pick it.
			int outcodeOut = outcode0 ? outcode0 : outcode1;
 
			// Now find the intersection point;
			// use formulas y = y0 + slope * (x - x0), x = x0 + (1 / slope) * (y - y0)
			if (outcodeOut & TOP) {           // point is above the clip rectangle
				x = x0 + (x1 - x0) * (YMAX - y0) / (y1 - y0);
				y = YMAX;
			} else if (outcodeOut & BOTTOM) { // point is below the clip rectangle
				x = x0 + (x1 - x0) * (YMIN - y0) / (y1 - y0);
				y = YMIN;
			} else if (outcodeOut & RIGHT) {  // point is to the right of clip rectangle
				y = y0 + (y1 - y0) * (XMAX - x0) / (x1 - x0);
				x = XMAX;
			} else if (outcodeOut & LEFT) {   // point is to the left of clip rectangle
				y = y0 + (y1 - y0) * (XMIN - x0) / (x1 - x0);
				x = XMIN;
			}
 
			// Now we move outside point to intersection point to clip
			// and get ready for next pass.
			if (outcodeOut == outcode0) {
				x0 = x;
				y0 = y;
				outcode0 = computeOutCode(x0, y0);
			} else {
				x1 = x;
				y1 = y;
				outcode1 = computeOutCode(x1, y1);
			}
		}
	}

	if (accept) {
		antialiasedLine(x0, y0, x1, y1, image, chroma, bright, meta);
	}
}

static unsigned char brush1[] = { 1, 0xFF };

static unsigned char brush2[] = { 3,
	0x11, 0x44, 0x11,
	0x44, 0xFF, 0x44,
	0x11, 0x44, 0x11,
};

static unsigned char brush3[] = { 3,
	0x33, 0xBB, 0x33,
	0xBB, 0xFF, 0xBB,
	0x33, 0xBB, 0x33,
};

static unsigned char brush4[] = { 5,
	0x00, 0x00, 0x22, 0x00, 0x00,
	0x00, 0xCC, 0xFF, 0xCC, 0x00,
	0x22, 0xFF, 0xFF, 0xFF, 0x22,
	0x00, 0xCC, 0xFF, 0xCC, 0x00,
	0x00, 0x00, 0x22, 0x00, 0x00,
};

static unsigned char brush5[] = { 5,
	0x11, 0x88, 0xBB, 0x88, 0x11,
	0x88, 0xFF, 0xFF, 0xFF, 0x88,
	0xBB, 0xFF, 0xFF, 0xFF, 0xBB,
	0x88, 0xFF, 0xFF, 0xFF, 0x88,
	0x11, 0x88, 0xBB, 0x88, 0x11,
};

static unsigned char brush6[] = { 7,
	0x00, 0x22, 0x99, 0xBB, 0x99, 0x22, 0x00,
	0x22, 0xEE, 0xFF, 0xFF, 0xFF, 0xEE, 0x22,
	0x99, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x99,
	0xBB, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xBB,
	0x99, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x99,
	0x22, 0xEE, 0xFF, 0xFF, 0xFF, 0xEE, 0x22,
	0x00, 0x22, 0x99, 0xBB, 0x99, 0x22, 0x00,
};

static unsigned char brush7[] = { 9,
	0x00, 0x11, 0x88, 0xDD, 0xFF, 0xDD, 0x88, 0x11, 0x00,
	0x11, 0xCC, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xCC, 0x11,
	0x88, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x88,
	0xDD, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xDD,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xDD, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xDD,
	0x88, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x88,
	0x11, 0xCC, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xCC, 0x11,
	0x00, 0x11, 0x88, 0xDD, 0xFF, 0xDD, 0x88, 0x11, 0x00,
};

static unsigned char brush8[] = { 13,
	0x00, 0x00, 0x00, 0x44, 0x99, 0xCC, 0xFF, 0xCC, 0x99, 0x44, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x88, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x88, 0x00, 0x00,
	0x00, 0x88, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x88, 0x00,
	0x44, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x44,
	0x99, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x99,
	0xCC, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xCC,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xCC, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xCC,
	0x99, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x99,
	0x44, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x44,
	0x00, 0x88, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x88, 0x00,
	0x00, 0x00, 0x88, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x88, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x44, 0x99, 0xCC, 0xFF, 0xCC, 0x99, 0x44, 0x00, 0x00, 0x00,
};

static unsigned char *brushes[] = {
	brush1, brush1, brush2, brush3, brush4, brush5, brush6, brush7, brush8,
};

void drawPixel(double x, double y, double *image, int *chroma, double bright, int meta) {
	putPixel(x,     y,     bright * rfpart(x) * rfpart(y), image, chroma, meta);
	putPixel(x + 1, y,     bright *  fpart(x) * rfpart(y), image, chroma, meta);
	putPixel(x,     y + 1, bright * rfpart(x) *  fpart(y), image, chroma, meta);
	putPixel(x + 1, y + 1, bright *  fpart(x) *  fpart(y), image, chroma, meta);
}

void drawBrush(double x, double y, double *image, int *chroma, double bright, int brush, int meta) {
	int nbrush = sizeof(brushes) / sizeof(brushes[0]);
	while (brush >= nbrush) {
		brush--;
		bright *= 2;
	}

	int width = brushes[brush][0];

	int xx, yy;
	for (xx = 0; xx < width; xx++) {
		for (yy = 0; yy < width; yy++) {
			drawPixel(x + xx - width/2, y + yy - width/2, image, chroma, brushes[brush][1 + yy * width + xx] * bright / 255, meta);
		}
	}
}
