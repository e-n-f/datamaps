#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string.h>
#include <png.h>
#include <math.h>
#include "util.h"

void out(unsigned char *buf, int width, int height) {
	png_image image;

	memset(&image, 0, sizeof image);
	image.version = PNG_IMAGE_VERSION;
	image.format = PNG_FORMAT_RGBA;
	image.width = width;
	image.height = height;

	png_image_write_to_stdio(&image, stdout, 0, buf, 4 * width, NULL);
	png_image_free(&image);
}

void putPixel(int x0, int y0, double bright, double *image) {
	if (x0 >= 0 && y0 >= 0 && x0 <= 255 && y0 <= 255) {
		image[y0 * 256 + x0] += bright;
	}
}

double fpart(double x) {
	return x - floor(x);
}

double rfpart(double x) {
	return 1 - fpart(x);
}

// loosely based on
// http://en.wikipedia.org/wiki/Xiaolin_Wu's_line_algorithm
void antialiasedLine(double x0, double y0, double x1, double y1, double *image, double bright) {
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
			putPixel(y0,     x0, dx * rfpart(y0) * bright, image);
			putPixel(y0 + 1, x0, dx *  fpart(y0) * bright, image);
		} else {
			putPixel(x0, y0,     dx * rfpart(y0) * bright, image);
			putPixel(x0, y0 + 1, dx *  fpart(y0) * bright, image);
		}

		return;
	}

	// there is a fractional pixel at the start
	if (x0 != floor(x0)) {
		double yy = y0 + .5 * rfpart(x0) * gradient;

		if (steep) {
			putPixel(yy,     x0, rfpart(x0) * rfpart(yy) * bright, image);
			putPixel(yy + 1, x0, rfpart(x0) *  fpart(yy) * bright, image);
		} else {
			putPixel(x0, yy,     rfpart(x0) * rfpart(yy) * bright, image);
			putPixel(x0, yy + 1, rfpart(x0) *  fpart(yy) * bright, image);
		}

		y0 += gradient * rfpart(x0);
		x0 = ceil(x0);
	}

	// there is a fractional pixel at the end
	if (x1 != floor(x1)) {
		double yy = y1 - .5 * fpart(x1) * gradient;

		if (steep) {
			putPixel(yy,     x1, fpart(x1) * rfpart(yy) * bright, image);
			putPixel(yy + 1, x1, fpart(x1) *  fpart(yy) * bright, image);
		} else {
			putPixel(x1, yy,     fpart(x1) * rfpart(yy) * bright, image);
			putPixel(x1, yy + 1, fpart(x1) *  fpart(yy) * bright, image);
		}

		y1 -= gradient * fpart(x1);
		x1 = floor(x1);
	}

	// now there are only whole pixels along the path

	// the middle of each whole pixel is halfway through a step
	y0 += .5 * gradient;

	for (; x0 < x1; x0++) {
		if (steep) {
			putPixel(y0,     x0, rfpart(y0) * bright, image);
			putPixel(y0 + 1, x0,  fpart(y0) * bright, image);
		} else {
			putPixel(x0, y0,     rfpart(y0) * bright, image);
			putPixel(x0, y0 + 1,  fpart(y0) * bright, image);
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

int computeOutCode(double x, double y) {
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
void drawClip(double x0, double y0, double x1, double y1, double *image, double bright) {
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
		antialiasedLine(x0, y0, x1, y1, image, bright);
	}
}

unsigned char brush1[] = { 1, 0xFF };

unsigned char brush2[] = { 3,
	0x11, 0x44, 0x11,
	0x44, 0xFF, 0x44,
	0x11, 0x44, 0x11,
};

unsigned char brush3[] = { 3,
	0x33, 0xBB, 0x33,
	0xBB, 0xFF, 0xBB,
	0x33, 0xBB, 0x33,
};

unsigned char brush4[] = { 5,
	0x00, 0x00, 0x22, 0x00, 0x00,
	0x00, 0xCC, 0xFF, 0xCC, 0x00,
	0x22, 0xFF, 0xFF, 0xFF, 0x22,
	0x00, 0xCC, 0xFF, 0xCC, 0x00,
	0x00, 0x00, 0x22, 0x00, 0x00,
};

unsigned char brush5[] = { 5,
	0x11, 0x88, 0xBB, 0x88, 0x11,
	0x88, 0xFF, 0xFF, 0xFF, 0x88,
	0xBB, 0xFF, 0xFF, 0xFF, 0xBB,
	0x88, 0xFF, 0xFF, 0xFF, 0x88,
	0x11, 0x88, 0xBB, 0x88, 0x11,
};

unsigned char brush6[] = { 7,
	0x00, 0x22, 0x99, 0xBB, 0x99, 0x22, 0x00,
	0x22, 0xEE, 0xFF, 0xFF, 0xFF, 0xEE, 0x22,
	0x99, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x99,
	0xBB, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xBB,
	0x99, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x99,
	0x22, 0xEE, 0xFF, 0xFF, 0xFF, 0xEE, 0x22,
	0x00, 0x22, 0x99, 0xBB, 0x99, 0x22, 0x00,
};

unsigned char brush7[] = { 9,
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

unsigned char brush8[] = { 13,
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

unsigned char *brushes[] = {
	brush1, brush1, brush2, brush3, brush4, brush5, brush6, brush7, brush8,
};

void drawPixel(double x, double y, double *image, double bright) {
	putPixel(x,     y,     bright * rfpart(x) * rfpart(y), image);
	putPixel(x + 1, y,     bright *  fpart(x) * rfpart(y), image);
	putPixel(x,     y + 1, bright * rfpart(x) *  fpart(y), image);
	putPixel(x + 1, y + 1, bright *  fpart(x) *  fpart(y), image);
}

void drawBrush(double x, double y, double *image, double bright, int brush) {
	int nbrush = sizeof(brushes) / sizeof(brushes[0]);
	while (brush >= nbrush) {
		brush--;
		bright *= 2;
	}

	int width = brushes[brush][0];

	int xx, yy;
	for (xx = 0; xx < width; xx++) {
		for (yy = 0; yy < width; yy++) {
			drawPixel(x + xx - width/2, y + yy - width/2, image, brushes[brush][1 + yy * width + xx] * bright / 255);
		}
	}
}

void process(char *fname, int components, int z_lookup, unsigned char *startbuf, unsigned char *endbuf, int z_draw, int x_draw, int y_draw, double *image, int mapbits, int metabits, int dump, int gps) {
	int bytes = bytesfor(mapbits, metabits, components, z_lookup);

	char fn[strlen(fname) + 1 + 5 + 1 + 5 + 1];

	if (components == 1) {
		sprintf(fn, "%s/1,0", fname);
	} else {
		sprintf(fn, "%s/%d,%d", fname, components, z_lookup);
	}

	int fd = open(fn, O_RDONLY);
	if (fd < 0) {
		perror(fn);
		return;
	}

	struct stat st;
	if (fstat(fd, &st) < 0) {
		perror("stat");
		exit(EXIT_FAILURE);
	}

	unsigned char *map = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
	if (map == MAP_FAILED) {
		perror("mmap");
		exit(EXIT_FAILURE);
	}

	gSortBytes = bytes;
	unsigned char *start = search(startbuf, map, st.st_size / bytes, bytes, bufcmp);
	unsigned char *end = search(endbuf, map, st.st_size / bytes, bytes, bufcmp);

	end += bytes; // points to the last value in range; need the one after that

	if (start != end && memcmp(start, end, bytes) != 0) {
		// XXX is this true with meta bits?
		start += bytes; // if not exact match, points to element before match
	}

	int step = 1, brush = 1;
	double bright = .015;
	if (components == 1) {
#define ALL 13
		if (z_draw >= ALL) {
			step = 1;
			brush = z_draw - ALL + 1;
		} else {
			step = 1 << (ALL - z_draw);
		}

		bright *= exp(log(1.15) * (z_draw - 5));
	} else {
		bright = 0.075; // looks good at zoom level 5

		bright *= exp(log(1.23) * (z_draw - 5));
	}

	for (; start < end; start += step * bytes) {
		unsigned int x[components], y[components];
		double xd[components], yd[components];
		int k;

		buf2xys(start, mapbits, z_lookup, components, x, y);

		for (k = 0; k < components; k++) {
			wxy2fxy(x[k], y[k], &xd[k], &yd[k], z_draw, x_draw, y_draw);
		}

		if (dump) {
			// XXX should we clip
			// instead of just excluding based on bounding box?

			int above = 1, below = 1, left = 1, right = 1;

			for (k = 0; k < components; k++) {
				if (xd[k] >= 0) {
					left = 0;
				}
				if (xd[k] <= 256) {
					right = 0;
				}
				if (yd[k] >= 0) {
					above = 0;
				}
				if (yd[k] <= 256) {
					below = 0;
				}
			}

			if (! (above || below || left || right)) {
				for (k = 0; k < components; k++) {
					double lat, lon;
					tile2latlon(x[k], y[k], 32, &lat, &lon);

					printf("%lf,%lf ", lat, lon);
				}

				printf("// ");

				for (k = 0; k < components; k++) {
					printf("%08x %08x ", x[k], y[k]);
				}

				printf("\n");
			}
		} else if (components == 1) {
			if (brush == 1) {
				drawPixel(xd[0] - .5, yd[0] - .5, image, bright);
			} else {
				drawBrush(xd[0], yd[0], image, bright, brush);
			}
		} else {
			for (k = 1; k < components; k++) {
				double bright1 = bright;

				long long xk1 = x[k - 1];
				long long xk = x[k];

				if (gps) {
					double xdist = (long long) x[k] - (long long) x[k - 1];
					double ydist = (long long) y[k] - (long long) y[k - 1];
					double dist = sqrt(xdist * xdist + ydist * ydist);

					double min = 1600; // about 50 feet
					// 1.5: pretty good
					min = min * exp(log(1.5) * (16 - z_draw));

					if (dist > min) {
						bright1 /= (dist / min);
					}

					if (bright1 < .0025) {
						continue;
					}
				}

				if (xk - xk1 >= (1LL << 31)) {
					wxy2fxy(xk - (1LL << 32), y[k], &xd[k], &yd[k], z_draw, x_draw, y_draw);
					drawClip(xd[k - 1], yd[k - 1], xd[k], yd[k], image, bright1);

					wxy2fxy(x[k], y[k], &xd[k], &yd[k], z_draw, x_draw, y_draw);
					wxy2fxy(xk1 + (1LL << 32), y[k - 1], &xd[k - 1], &yd[k - 1], z_draw, x_draw, y_draw);
					drawClip(xd[k - 1], yd[k - 1], xd[k], yd[k], image, bright1);

					wxy2fxy(x[k - 1], y[k - 1], &xd[k - 1], &yd[k - 1], z_draw, x_draw, y_draw);
				} else if (xk1 - xk >= (1LL << 31)) {
					wxy2fxy(xk1 - (1LL << 32), y[k - 1], &xd[k - 1], &yd[k - 1], z_draw, x_draw, y_draw);
					drawClip(xd[k - 1], yd[k - 1], xd[k], yd[k], image, bright1);

					wxy2fxy(x[k - 1], y[k - 1], &xd[k - 1], &yd[k - 1], z_draw, x_draw, y_draw);
					wxy2fxy(xk + (1LL << 32), y[k], &xd[k], &yd[k], z_draw, x_draw, y_draw);
					drawClip(xd[k - 1], yd[k - 1], xd[k], yd[k], image, bright1);

					wxy2fxy(x[k], y[k], &xd[k], &yd[k], z_draw, x_draw, y_draw);
				} else {
					drawClip(xd[k - 1], yd[k - 1], xd[k], yd[k], image, bright1);
				}
			}
		}
	}

	munmap(map, st.st_size);
	close(fd);
}

void usage(char **argv) {
	fprintf(stderr, "Usage: %s [-t transparency] [-dg] file z x y\n", argv[0]);
	exit(EXIT_FAILURE);
}

int main(int argc, char **argv) {
	int i;
	extern int optind;
	extern char *optarg;

	int transparency = 224;
	int dump = 0;
	int gps = 0;

	while ((i = getopt(argc, argv, "t:dg")) != -1) {
		switch (i) {
		case 't':
			transparency = atoi(optarg);
			break;

		case 'd':
			dump = 1;
			break;

		case 'g':
			gps = 1;
			break;

		default:
			usage(argv);
		}
	}

	if (argc - optind != 4) {
		usage(argv);
	}

	char *fname = argv[optind];
	unsigned int z_draw = atoi(argv[optind + 1]);
	unsigned int x_draw = atoi(argv[optind + 2]);
	unsigned int y_draw = atoi(argv[optind + 3]);

	char meta[strlen(fname) + 1 + 4 + 1];
	sprintf(meta, "%s/meta", fname);
	FILE *f = fopen(meta, "r");
	if (f == NULL) {
		perror(meta);
		exit(EXIT_FAILURE);
	}

	char s[2000] = "";
	if (fgets(s, 2000, f) == NULL || strcmp(s, "1\n") != 0) {
		fprintf(stderr, "%s: Unknown version %s", meta, s);
		exit(EXIT_FAILURE);
	}
	int mapbits, metabits, maxn;
	if (fgets(s, 2000, f) == NULL || sscanf(s, "%d %d %d", &mapbits, &metabits, &maxn) != 3) {
		fprintf(stderr, "%s: couldn't find size declaration", meta);
		exit(EXIT_FAILURE);
	}
	fclose(f);

	int bytes = (mapbits + metabits + 7) / 8;

	double image[256 * 256];
	memset(image, 0, sizeof(image));

	// Do the single-point case

	unsigned char startbuf[bytes];
	unsigned char endbuf[bytes];
	zxy2bufs(z_draw, x_draw, y_draw, startbuf, endbuf, bytes);
	process(fname, 1, z_draw, startbuf, endbuf, z_draw, x_draw, y_draw, image, mapbits, metabits, dump, gps);

	// Do the zoom levels numbered greater than this one.
	//
	// For zoom levels greater than this one, we look up the entire area
	// of the tile we are drawing, which will end up being multiple tiles
	// of the higher zoom.

	int z_lookup;
	for (z_lookup = z_draw + 1; (dump || z_lookup < z_draw + 9) && z_lookup <= mapbits / 2; z_lookup++) {
		for (i = 2; i <= maxn; i++) {
			int bytes = bytesfor(mapbits, metabits, i, z_lookup);

			unsigned char startbuf[bytes];
			unsigned char endbuf[bytes];
			zxy2bufs(z_draw, x_draw, y_draw, startbuf, endbuf, bytes);
			process(fname, i, z_lookup, startbuf, endbuf, z_draw, x_draw, y_draw, image, mapbits, metabits, dump, gps);
		}
	}

	// For zoom levels numbered less than this one, each stage looks up a
	// larger area for potential overlaps.

	int x_lookup, y_lookup;
	for (z_lookup = z_draw, x_lookup = x_draw, y_lookup = y_draw;
	     z_lookup >= 0;
	     z_lookup--, x_lookup /= 2, y_lookup /= 2) {
		for (i = 2; i <= maxn; i++) {
			int bytes = bytesfor(mapbits, metabits, i, z_lookup);

			unsigned char startbuf[bytes];
			unsigned char endbuf[bytes];
			zxy2bufs(z_lookup, x_lookup, y_lookup, startbuf, endbuf, bytes);
			process(fname, i, z_lookup, startbuf, endbuf, z_draw, x_draw, y_draw, image, mapbits, metabits, dump, gps);
		}
	}

	unsigned char img2[256 * 256 * 4];

	int midr = 128;
	int midg = 128;
	int midb = 128;

	double limit2 = 1;
	double limit = limit2 / 2;

	double gamma = .5;

	for (i = 0; i < 256 * 256; i++) {
		if (image[i] == 0) {
			img2[4 * i + 0] = 0;
			img2[4 * i + 1] = 0;
			img2[4 * i + 2] = 0;
			img2[4 * i + 3] = transparency;
		} else {
			if (gamma != 1) {
				image[i] = exp(log(image[i]) * gamma);
			}

			if (image[i] <= limit) {
				img2[4 * i + 0] = midr * (image[i] / limit);
				img2[4 * i + 1] = midg * (image[i] / limit);
				img2[4 * i + 2] = midb * (image[i] / limit);
				img2[4 * i + 3] = 255 * (image[i] / limit) +
						  transparency * (1 - (image[i] / limit));
			} else if (image[i] <= limit2) {
				double along = (image[i] - limit) / (limit2 - limit);
				img2[4 * i + 0] = 255 * along + midr * (1 - along);
				img2[4 * i + 1] = 255 * along + midg * (1 - along);
				img2[4 * i + 2] = 255 * along + midb * (1 - along);
				img2[4 * i + 3] = 255;
			} else {
				img2[4 * i + 0] = 255;
				img2[4 * i + 1] = 255;
				img2[4 * i + 2] = 255;
				img2[4 * i + 3] = 255;
			}
		}
	}

	if (!dump) {
		out(img2, 256, 256);
	}

	return 0;
}
