#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <png.h>
#include <math.h>
#include <limits.h>
#include "util.h"
#include "graphics.h"
#include "clip.h"

static void fail(png_structp png_ptr, png_const_charp error_msg) {
	fprintf(stderr, "PNG error %s\n", error_msg);
	exit(EXIT_FAILURE);
}

struct graphics {
	int width;
	int height;
	double *image;
	double *cx;
	double *cy;

	int clipx;
	int clipy;
	int clipwidth;
	int clipheight;
};

struct graphics *graphics_init(int width, int height, char **filetype) {
	struct graphics *g = malloc(sizeof(struct graphics));

	g->width = width;
	g->height = height;
	g->image = malloc(width * height * sizeof(double));
	g->cx = malloc(width * height * sizeof(double));
	g->cy = malloc(width * height * sizeof(double));

	memset(g->image, 0, width * height * sizeof(double));
	memset(g->cx, 0, width * height * sizeof(double));
	memset(g->cy, 0, width * height * sizeof(double));

	g->clipx = 0;
	g->clipy = 0;
	g->clipwidth = INT_MAX;
	g->clipheight = INT_MAX;

	*filetype = "png";
	return g;
}

void out(struct graphics *gc, int transparency, double gamma, int invert, int color, int color2, int saturate, int mask, double color_cap, int cie) {
	unsigned char *buf = malloc(gc->width * gc->height * 4);

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
	for (i = 0; i < gc->width * gc->height; i++) {
		double sat = 0;

		if (gc->cx[i] == 0 && gc->cy[i] == 0) {
			midr = r;
			midg = g;
			midb = b;
		} else {
			double h = atan2(gc->cy[i], gc->cx[i]) / (2 * M_PI);

			if (gc->image[i] != 0) {
				sat = sqrt(gc->cx[i] * gc->cx[i] + gc->cy[i] * gc->cy[i]) / gc->image[i];
			}

			if (cie) {
				h *= 2 * M_PI;
				// put red at the right
				h = h + (M_PI / 2 - (M_PI - 2));

				double l = .5;

				double r1 = sin(h + M_PI - 2.0) * 0.417211 * sat + l;
				double g1 = sin(h + M_PI + 1.5) * 0.158136 * sat + l;
				double b1 = sin(h + M_PI      ) * 0.455928 * sat + l;

				midr = exp(log(r1 * 0.923166 + 0.0791025) * 1.25) * 255;
				midg = exp(log(g1 * 0.923166 + 0.0791025) * 1.25) * 255;
				midb = exp(log(b1 * 0.923166 + 0.0791025) * 1.25) * 255;
			} else {
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

		if (gc->image[i] != 0) {
			if (gamma != 1) {
				gc->image[i] = exp(log(gc->image[i]) * gamma);
			}
		}

		if (mask) {
			gc->image[i] = limit - gc->image[i];
			if (gc->image[i] < 0) {
				gc->image[i] = 0;
			}
		}

		if (gc->image[i] == 0) {
			buf[4 * i + 0] = bg;
			buf[4 * i + 1] = bg;
			buf[4 * i + 2] = bg;
			buf[4 * i + 3] = transparency;
		} else {
			if (sat != 0) {
				if (gc->image[i] > limit2 * color_cap) {
					gc->image[i] = limit2 * color_cap;
				}
			}

			if (!saturate) {
				if (gc->image[i] > limit2) {
					gc->image[i] = limit2;
				}

				gc->image[i] *= limit / limit2;
			}

			if (gc->image[i] <= limit) {
				double along = gc->image[i] / limit;
				double opacity = (255 * along + transparency * (1 - along)) / 255;

				buf[4 * i + 0] = midr * along / opacity + bg * (1 - along / opacity);
				buf[4 * i + 1] = midg * along / opacity + bg * (1 - along / opacity);
				buf[4 * i + 2] = midb * along / opacity + bg * (1 - along / opacity);
				buf[4 * i + 3] = opacity * 255;
			} else if (gc->image[i] <= limit2) {
				double along = (gc->image[i] - limit) / (limit2 - limit);
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

	unsigned char *rows[gc->height];
	for (i = 0 ; i < gc->height; i++) {
		rows[i] = buf + i * (4 * gc->width);
	}

	png_structp png_ptr;
	png_infop info_ptr;

	png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, fail, fail, fail);
	if (png_ptr == NULL) {
		fprintf(stderr, "PNG failure (write struct)\n");
		exit(EXIT_FAILURE);
	}
	info_ptr = png_create_info_struct(png_ptr);
	if (info_ptr == NULL) {
		png_destroy_write_struct(&png_ptr, NULL);
		fprintf(stderr, "PNG failure (info struct)\n");
		exit(EXIT_FAILURE);
	}

	png_set_IHDR(png_ptr, info_ptr, gc->width, gc->height, 8, PNG_COLOR_TYPE_RGB_ALPHA, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
	png_set_rows(png_ptr, info_ptr, rows);
	png_init_io(png_ptr, stdout);
	png_write_png(png_ptr, info_ptr, 0, NULL);
	png_destroy_write_struct(&png_ptr, &info_ptr);

	free(buf);
}

static void putPixel(double x, double y, double bright, struct graphics *g, double hue) {
	int x0 = floor(x);
	int y0 = floor(y);

	if (x0 >= 0 && y0 >= 0 && x0 <= g->width - 1 && y0 <= g->height - 1) {
		if (x0 >= g->clipx && x0 < g->clipx + g->clipwidth && y0 >= g->clipy && y0 < g->clipy + g->clipheight) {
			g->image[y0 * g->width + x0] += bright;

			if (hue >= 0) {
				g->cx[y0 * g->width + x0] += bright * cos(hue * 2 * M_PI);
				g->cy[y0 * g->width + x0] += bright * sin(hue * 2 * M_PI);
			}
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
static void antialiasedLine(double x0, double y0, double x1, double y1, struct graphics *g, double bright, double hue) {
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
			putPixel(y0,     x0, dx * rfpart(y0) * bright, g, hue);
			putPixel(y0 + 1, x0, dx *  fpart(y0) * bright, g, hue);
		} else {
			putPixel(x0, y0,     dx * rfpart(y0) * bright, g, hue);
			putPixel(x0, y0 + 1, dx *  fpart(y0) * bright, g, hue);
		}

		return;
	}

	// there is a fractional pixel at the start
	if (x0 != floor(x0)) {
		double yy = y0 + .5 * rfpart(x0) * gradient;

		if (steep) {
			putPixel(yy,     x0, rfpart(x0) * rfpart(yy) * bright, g, hue);
			putPixel(yy + 1, x0, rfpart(x0) *  fpart(yy) * bright, g, hue);
		} else {
			putPixel(x0, yy,     rfpart(x0) * rfpart(yy) * bright, g, hue);
			putPixel(x0, yy + 1, rfpart(x0) *  fpart(yy) * bright, g, hue);
		}

		y0 += gradient * rfpart(x0);
		x0 = ceil(x0);
	}

	// there is a fractional pixel at the end
	if (x1 != floor(x1)) {
		double yy = y1 - .5 * fpart(x1) * gradient;

		if (steep) {
			putPixel(yy,     x1, fpart(x1) * rfpart(yy) * bright, g, hue);
			putPixel(yy + 1, x1, fpart(x1) *  fpart(yy) * bright, g, hue);
		} else {
			putPixel(x1, yy,     fpart(x1) * rfpart(yy) * bright, g, hue);
			putPixel(x1, yy + 1, fpart(x1) *  fpart(yy) * bright, g, hue);
		}

		y1 -= gradient * fpart(x1);
		x1 = floor(x1);
	}

	// now there are only whole pixels along the path

	// the middle of each whole pixel is halfway through a step
	y0 += .5 * gradient;

	for (; x0 < x1; x0++) {
		if (steep) {
			putPixel(y0,     x0, rfpart(y0) * bright, g, hue);
			putPixel(y0 + 1, x0,  fpart(y0) * bright, g, hue);
		} else {
			putPixel(x0, y0,     rfpart(y0) * bright, g, hue);
			putPixel(x0, y0 + 1,  fpart(y0) * bright, g, hue);
		}

		y0 += gradient;
	}
}

static void antialiasedLineThick(double x0, double y0, double x1, double y1, struct graphics *g, double bright, double hue, double thick) {
	if (thick <= 1) {
		antialiasedLine(x0, y0, x1, y1, g, bright * thick, hue);
		return;
	}

	antialiasedLine(x0, y0, x1, y1, g, bright, hue);
	int off = 1;
	thick--;

	double angle = atan2(y1 - y0, x1 - x0) + M_PI / 2;
	double c = cos(angle);
	double s = sin(angle);

	while (thick > 0) {
		if (thick >= 2) {
			antialiasedLine(x0 + c * off, y0 + s * off, x1 + c * off, y1 + s * off, g, bright, hue);
			antialiasedLine(x0 - c * off, y0 - s * off, x1 - c * off, y1 - s * off, g, bright, hue);
		} else {
			antialiasedLine(x0 + c * (off - 1 + thick / 2), y0 + s * (off - 1 + thick / 2),
					x1 + c * (off - 1 + thick / 2), y1 + s * (off - 1 + thick / 2), g, bright * thick / 2, hue);
			antialiasedLine(x0 - c * (off - 1 + thick / 2), y0 - s * (off - 1 + thick / 2),
					x1 - c * (off - 1 + thick / 2), y1 - s * (off - 1 + thick / 2), g, bright * thick / 2, hue);
		}

		thick -= 2;
		off++;
	}
}

// http://rosettacode.org/wiki/Bitmap/Bresenham's_line_algorithm#C
void drawLine(int x0, int y0, int x1, int y1, struct graphics *g, double bright, double hue) {
	int dx = abs(x1 - x0), sx = (x0 < x1) ? 1 : -1;
	int dy = abs(y1 - y0), sy = (y0 < y1) ? 1 : -1;
	int err = ((dx > dy) ? dx : -dy) / 2, e2;

	while (1) {
		if (x0 == x1 && y0 == y1) {
			break;
		}

		putPixel(x0, y0, bright, g, hue);

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
			if (antialias) {
				antialiasedLineThick(x0, y0, x1, y1, g, bright, hue, thick);
			} else {
				drawLine(x0, y0, x1, y1, g, bright, hue);
			}
		}

		return 1;
	}

	return 0;
}

void drawPixel(double x, double y, struct graphics *g, double bright, double hue, long long meta, struct tilecontext *tc) {
	putPixel(x,     y,     bright * rfpart(x) * rfpart(y), g, hue);
	putPixel(x + 1, y,     bright *  fpart(x) * rfpart(y), g, hue);
	putPixel(x,     y + 1, bright * rfpart(x) *  fpart(y), g, hue);
	putPixel(x + 1, y + 1, bright *  fpart(x) *  fpart(y), g, hue);
}

static double thebrush = -1;
static int brushwidth = -1;
static int thegaussian = -1;
static double *brushbytes = NULL;

void drawBrush(double x, double y, struct graphics *g, double bright, double brush, double hue, long long meta, int gaussian, struct tilecontext *tc) {
	if (brush != thebrush || gaussian != thegaussian) {
		free(brushbytes);
		thebrush = brush;
		thegaussian = gaussian;

#define MULT 9

		double radius = MULT * sqrt(brush / M_PI);
		int bigwidth = 2 * ceil(radius / MULT) * MULT + MULT;
		int mid = bigwidth / 2;
		brushwidth = bigwidth / MULT;

		double *temp = malloc(bigwidth * bigwidth * sizeof(double));
		memset(temp, '\0', bigwidth * bigwidth * sizeof(double));

		double sum = 0;
		int xa;
		for (xa = mid - floor(radius); xa <= mid + floor(radius); xa++) {
			double dx = acos((xa - mid) / radius);
			double yy = floor(fabs(sin(dx)) * radius);

			int ya;
			for (ya = mid - yy; ya <= mid + yy; ya++) {
				int y1 = ya;
				int x1 = xa;

				if (y1 >= 0 && y1 < bigwidth && x1 >= 0 && x1 < bigwidth) {
					double inc = 1;

					if (gaussian) {
						double xx = (xa - mid) / radius;
						double yy = (ya - mid) / radius;
						double d = sqrt(xx * xx + yy * yy);

						inc = exp(-(d * d) / (2.0 / (3.0 * 3.0)));
					}

					temp[bigwidth * y1 + x1] = inc;
					sum += inc;
				}
			}
		}

		brushbytes = malloc(brushwidth * brushwidth * sizeof(double));
		memset(brushbytes, '\0', brushwidth * brushwidth * sizeof(double));

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

	// match where single pixels are drawn
	x -= ceil(brushwidth / 2) + .5;
	y -= ceil(brushwidth / 2) + .5;

	int width = brushwidth;

	if (x + width < 0) {
		return;
	}
	if (y + width < 0) {
		return;
	}
	if (x - width > g->width) {
		return;
	}
	if (y - width > g->height) {
		return;
	}

	int xx, yy;
	for (xx = 0; xx < width; xx++) {
		for (yy = 0; yy < width; yy++) {
			drawPixel(x + xx, y + yy, g, brushbytes[yy * width + xx] * bright / (MULT * MULT), hue, meta, tc);
		}
	}
}

void setClip(struct graphics *gc, int x, int y, int width, int height) {
	gc->clipx = x;
	gc->clipy = y;
	gc->clipwidth = width;
	gc->clipheight = height;
}
