#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <png.h>
#include <math.h>
#include "util.h"
#include "graphics.h"

#if PNG_LIBPNG_VER < 10600
#error libpng >= 1.6 is required
#endif

double *graphics_init() {
	double *image = malloc(256 * 256 * sizeof(double));

	memset(image, 0, 256 * 256 * sizeof(double));
	return image;
}

void out(double *src, double *cx, double *cy, int width, int height, int transparency, double gamma, int invert, int color, int color2, int saturate, int mask) {
	unsigned char *buf = malloc(width * height * 4);

	int midr, midg, midb;

	double limit2 = 1;
	double limit = limit2 / 2;

	int r, g, b;
	if (color < 0) {
		r = 128;
		g = 128;
		b = 128;
	} else {
		r = (color >> 16) & 0xFF;
		g = (color >>  8) & 0xFF;
		b = (color >>  0) & 0xFF;
	}

	int i;
	for (i = 0; i < width * height; i++) {
		double sat = 0;

		if (cx[i] == 0 && cy[i] == 0) {
			midr = r;
			midg = g;
			midb = b;
		} else {
			double h = atan2(cy[i], cx[i]) / (2 * M_PI);

			if (src[i] != 0) {
				sat = sqrt(cx[i] * cx[i] + cy[i] * cy[i]) / src[i];
			}

			// http://basecase.org/env/on-rainbows
			h += .5;
			h *= -1;
			double r1 = sin(M_PI * h);
			double g1 = sin(M_PI * (h + 1.0/3));
			double b1 = sin(M_PI * (h + 2.0/3));
			midr = 255 * (r1 * r1) * sat + r * (1 - sat);
			midg = 255 * (g1 * g1) * sat + g * (1 - sat);
			midb = 255 * (b1 * b1) * sat + b * (1 - sat);
		}

		int fg = 255;
		int bg = 0;

		if (invert) {
			bg = 255;
			fg = 0;
		}

		int r2, g2, b2;
		if (color2 < 0) {
			r2 = fg;
			g2 = fg;
			b2 = fg;
		} else {
			r2 = (color2 >> 16) & 0xFF;
			g2 = (color2 >>  8) & 0xFF;
			b2 = (color2 >>  0) & 0xFF;
		}

		if (src[i] != 0) {
			if (gamma != 1) {
				src[i] = exp(log(src[i]) * gamma);
			}
		}

		if (mask) {
			src[i] = limit - src[i];
			if (src[i] < 0) {
				src[i] = 0;
			}
		}

		if (src[i] == 0) {
			buf[4 * i + 0] = bg;
			buf[4 * i + 1] = bg;
			buf[4 * i + 2] = bg;
			buf[4 * i + 3] = transparency;
		} else {
			if (sat != 0) {
#define COLOR_CAP .7
				if (src[i] > limit2 * COLOR_CAP) {
					src[i] = limit2 * COLOR_CAP;
				}
			}

			if (!saturate) {
				if (src[i] > limit2) {
					src[i] = limit2;
				}

				src[i] *= limit / limit2;
			}

			if (src[i] <= limit) {
				double along = src[i] / limit;
				double opacity = (255 * along + transparency * (1 - along)) / 255;

				buf[4 * i + 0] = midr * along / opacity + bg * (1 - along / opacity);
				buf[4 * i + 1] = midg * along / opacity + bg * (1 - along / opacity);
				buf[4 * i + 2] = midb * along / opacity + bg * (1 - along / opacity);
				buf[4 * i + 3] = opacity * 255;
			} else if (src[i] <= limit2) {
				double along = (src[i] - limit) / (limit2 - limit);
				buf[4 * i + 0] = r2 * along + midr * (1 - along);
				buf[4 * i + 1] = g2 * along + midg * (1 - along);
				buf[4 * i + 2] = b2 * along + midb * (1 - along);
				buf[4 * i + 3] = 255;
			} else {
				buf[4 * i + 0] = r2;
				buf[4 * i + 1] = g2;
				buf[4 * i + 2] = b2;
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

	free(buf);
}

static void putPixel(double x, double y, double bright, double *image, double *cx, double *cy, double hue) {
	int x0 = floor(x);
	int y0 = floor(y);

	if (x0 >= 0 && y0 >= 0 && x0 <= 255 && y0 <= 255) {
		image[y0 * 256 + x0] += bright;

		if (hue >= 0) {
			cx[y0 * 256 + x0] += bright * cos(hue * 2 * M_PI);
			cy[y0 * 256 + x0] += bright * sin(hue * 2 * M_PI);
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
static void antialiasedLine(double x0, double y0, double x1, double y1, double *image, double *cx, double *cy, double bright, double hue) {
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
			putPixel(y0,     x0, dx * rfpart(y0) * bright, image, cx, cy, hue);
			putPixel(y0 + 1, x0, dx *  fpart(y0) * bright, image, cx, cy, hue);
		} else {
			putPixel(x0, y0,     dx * rfpart(y0) * bright, image, cx, cy, hue);
			putPixel(x0, y0 + 1, dx *  fpart(y0) * bright, image, cx, cy, hue);
		}

		return;
	}

	// there is a fractional pixel at the start
	if (x0 != floor(x0)) {
		double yy = y0 + .5 * rfpart(x0) * gradient;

		if (steep) {
			putPixel(yy,     x0, rfpart(x0) * rfpart(yy) * bright, image, cx, cy, hue);
			putPixel(yy + 1, x0, rfpart(x0) *  fpart(yy) * bright, image, cx, cy, hue);
		} else {
			putPixel(x0, yy,     rfpart(x0) * rfpart(yy) * bright, image, cx, cy, hue);
			putPixel(x0, yy + 1, rfpart(x0) *  fpart(yy) * bright, image, cx, cy, hue);
		}

		y0 += gradient * rfpart(x0);
		x0 = ceil(x0);
	}

	// there is a fractional pixel at the end
	if (x1 != floor(x1)) {
		double yy = y1 - .5 * fpart(x1) * gradient;

		if (steep) {
			putPixel(yy,     x1, fpart(x1) * rfpart(yy) * bright, image, cx, cy, hue);
			putPixel(yy + 1, x1, fpart(x1) *  fpart(yy) * bright, image, cx, cy, hue);
		} else {
			putPixel(x1, yy,     fpart(x1) * rfpart(yy) * bright, image, cx, cy, hue);
			putPixel(x1, yy + 1, fpart(x1) *  fpart(yy) * bright, image, cx, cy, hue);
		}

		y1 -= gradient * fpart(x1);
		x1 = floor(x1);
	}

	// now there are only whole pixels along the path

	// the middle of each whole pixel is halfway through a step
	y0 += .5 * gradient;

	for (; x0 < x1; x0++) {
		if (steep) {
			putPixel(y0,     x0, rfpart(y0) * bright, image, cx, cy, hue);
			putPixel(y0 + 1, x0,  fpart(y0) * bright, image, cx, cy, hue);
		} else {
			putPixel(x0, y0,     rfpart(y0) * bright, image, cx, cy, hue);
			putPixel(x0, y0 + 1,  fpart(y0) * bright, image, cx, cy, hue);
		}

		y0 += gradient;
	}
}

static void antialiasedLineThick(double x0, double y0, double x1, double y1, double *image, double *cx, double *cy, double bright, double hue, double thick) {
	if (thick <= 1) {
		antialiasedLine(x0, y0, x1, y1, image, cx, cy, bright * thick, hue);
		return;
	}

	antialiasedLine(x0, y0, x1, y1, image, cx, cy, bright, hue);
	int off = 1;
	thick--;

	double angle = atan2(y1 - y0, x1 - x0) + M_PI / 2;
	double c = cos(angle);
	double s = sin(angle);

	while (thick > 0) {
		if (thick >= 2) {
			antialiasedLine(x0 + c * off, y0 + s * off, x1 + c * off, y1 + s * off, image, cx, cy, bright, hue);
			antialiasedLine(x0 - c * off, y0 - s * off, x1 - c * off, y1 - s * off, image, cx, cy, bright, hue);
		} else {
			antialiasedLine(x0 + c * (off - 1 + thick / 2), y0 + s * (off - 1 + thick / 2),
					x1 + c * (off - 1 + thick / 2), y1 + s * (off - 1 + thick / 2), image, cx, cy, bright * thick / 2, hue);
			antialiasedLine(x0 - c * (off - 1 + thick / 2), y0 - s * (off - 1 + thick / 2),
					x1 - c * (off - 1 + thick / 2), y1 - s * (off - 1 + thick / 2), image, cx, cy, bright * thick / 2, hue);
		}

		thick -= 2;
		off++;
	}
}

// http://rosettacode.org/wiki/Bitmap/Bresenham's_line_algorithm#C
void drawLine(int x0, int y0, int x1, int y1, double *image, double *cx, double *cy, double bright, double hue) {
	int dx = abs(x1 - x0), sx = (x0 < x1) ? 1 : -1;
	int dy = abs(y1 - y0), sy = (y0 < y1) ? 1 : -1;
	int err = ((dx > dy) ? dx : -dy) / 2, e2;

	while (1) {
		if (x0 == x1 && y0 == y1) {
			break;
		}

		putPixel(x0, y0, bright, image, cx, cy, hue);

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

#define INSIDE 0
#define LEFT 1
#define RIGHT 2
#define BOTTOM 4
#define TOP 8

static int computeOutCode(double x, double y, double xmin, double ymin, double xmax, double ymax) {
	int code = INSIDE;

	if (x < xmin) {
		code |= LEFT;
	} else if (x > xmax) {
		code |= RIGHT;
	}

	if (y < ymin) {
		code |= BOTTOM;
	} else if (y > ymax) {
		code |= TOP;
	}

	return code;
}

// http://en.wikipedia.org/wiki/Cohen%E2%80%93Sutherland_algorithm
int drawClip(double x0, double y0, double x1, double y1, double *image, double *cx, double *cy, double bright, double hue, int antialias, double thick) {
	double xmin = -1 - thick;
	double ymin = -1 - thick;
	double xmax = 256 + thick;
	double ymax = 256 + thick;

	int outcode0 = computeOutCode(x0, y0, xmin, ymin, xmax, ymax);
	int outcode1 = computeOutCode(x1, y1, xmin, ymin, xmax, ymax);
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
				x = x0 + (x1 - x0) * (ymax - y0) / (y1 - y0);
				y = ymax;
			} else if (outcodeOut & BOTTOM) { // point is below the clip rectangle
				x = x0 + (x1 - x0) * (ymin - y0) / (y1 - y0);
				y = ymin;
			} else if (outcodeOut & RIGHT) {  // point is to the right of clip rectangle
				y = y0 + (y1 - y0) * (xmax - x0) / (x1 - x0);
				x = xmax;
			} else if (outcodeOut & LEFT) {   // point is to the left of clip rectangle
				y = y0 + (y1 - y0) * (xmin - x0) / (x1 - x0);
				x = xmin;
			}
 
			// Now we move outside point to intersection point to clip
			// and get ready for next pass.
			if (outcodeOut == outcode0) {
				x0 = x;
				y0 = y;
				outcode0 = computeOutCode(x0, y0, xmin, ymin, xmax, ymax);
			} else {
				x1 = x;
				y1 = y;
				outcode1 = computeOutCode(x1, y1, xmin, ymin, xmax, ymax);
			}
		}
	}

	if (accept) {
		if (image != NULL) {
			if (antialias) {
				antialiasedLineThick(x0, y0, x1, y1, image, cx, cy, bright, hue, thick);
			} else {
				drawLine(x0, y0, x1, y1, image, cx, cy, bright, hue);
			}
		}

		return 1;
	}

	return 0;
}

void drawPixel(double x, double y, double *image, double *cx, double *cy, double bright, double hue) {
	putPixel(x,     y,     bright * rfpart(x) * rfpart(y), image, cx, cy, hue);
	putPixel(x + 1, y,     bright *  fpart(x) * rfpart(y), image, cx, cy, hue);
	putPixel(x,     y + 1, bright * rfpart(x) *  fpart(y), image, cx, cy, hue);
	putPixel(x + 1, y + 1, bright *  fpart(x) *  fpart(y), image, cx, cy, hue);
}

static double thebrush = -1;
static int brushwidth = -1;
static unsigned char *brushbytes = NULL;

void drawBrush(double x, double y, double *image, double *cx, double *cy, double bright, double brush, double hue) {
	if (brush != thebrush) {
		free(brushbytes);
		thebrush = brush;

#define MULT 8

		double radius = MULT * sqrt(brush / M_PI);
		int bigwidth = (((int) (2 * radius + (MULT - 1))) / MULT) * MULT;
		brushwidth = bigwidth / MULT;

		unsigned char *temp = malloc(bigwidth * bigwidth);
		memset(temp, '\0', bigwidth * bigwidth);

		int off = 0;
		if (brush <= 2) {
			off = MULT;
		}

		int sum = 0;
		int xa;
		for (xa = 0; xa < 2 * radius; xa++) {
			double dx = acos((xa - radius) / radius);
			double yy = fabs(sin(dx)) * radius;

			int ya;
			for (ya = radius - yy; ya < radius + yy; ya++) {
				int y1 = ya - off;
				int x1 = xa - off;

				if (y1 >= 0 && y1 < bigwidth && x1 >= 0 && x1 < bigwidth) {
					temp[bigwidth * y1 + x1] = 1;
					sum++;
				}
			}
		}

		brushbytes = malloc(brushwidth * brushwidth);
		memset(brushbytes, '\0', brushwidth * brushwidth);

		for (xa = 0; xa < bigwidth; xa++) {
			int ya;
			for (ya = 0; ya < bigwidth; ya++) {
				brushbytes[xa / MULT + (ya / MULT) * brushwidth] += temp[xa + ya * bigwidth];
			}
		}

		double scale = MULT * MULT * brush / (double) sum;

		for (xa = 0; xa < brushwidth * brushwidth; xa++) {
			brushbytes[xa] *= scale;
		}

		free(temp);
	}

	int width = brushwidth;

	if (x + width < 0) {
		return;
	}
	if (y + width < 0) {
		return;
	}

	int xx, yy;
	for (xx = 0; xx < width; xx++) {
		for (yy = 0; yy < width; yy++) {
			drawPixel(x + xx, y + yy, image, cx, cy, brushbytes[yy * width + xx] * bright / (MULT * MULT), hue);
		}
	}
}
