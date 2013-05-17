#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string.h>
#include <png.h>
#include <math.h>

#define FNAME "pairs"

#define LEVELS 24
#define BYTES (2 * 2 * LEVELS / 8)

int quadcmp(const void *v1, const void *v2) {
	const unsigned char *q1 = v1;
	const unsigned char *q2 = v2;

	int i;
	for (i = 2 * 2 * LEVELS / 8 - 1; i >= 0; i--) {
		int diff = (int) q1[i] - (int) q2[i];

		if (diff != 0) {
			return diff;
		}
	}

	return 0;
}

// http://www.tbray.org/ongoing/When/200x/2003/03/22/Binary
void *search(const void *key, const void *base, size_t nel, size_t width,
		int (*cmp)(const void *, const void *)) {

	long long high = nel, low = -1, probe;
	while (high - low > 1) {
		probe = (low + high) >> 1;
		int c = cmp(((char *) base) + probe * width, key);
		if (c > 0) {
			high = probe;
		} else {
			low = probe;
		}
	}

	if (low < 0) {
		low = 0;
	}

	return ((char *) base) + low * width;
}

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

unsigned long long buf2quad(unsigned char *buf) {
	unsigned long long quad = 0;
	int i;

	for (i = 0; i < 2 * LEVELS; i += 8) {
		quad |= ((unsigned long long) buf[i / 8]) << i;
	}

	quad <<= (64 - (2 * LEVELS));
	return quad;
}

void quad2buf(unsigned long long quad, unsigned char *buf) {
	int i;

	quad >>= (64 - (2 * LEVELS));

	for (i = 0; i < 2 * LEVELS; i += 8) {
		buf[i / 8] = (quad >> i) & 0xFF;
		buf[i / 8 + BYTES / 2] = (quad >> i) & 0xFF;
	}
}

void quad2xy(unsigned long long quad, int *ox, int *oy, int z, int x, int y) {
	long long wx = 0, wy = 0;
	int i;

	// first decode into world coordinates

        for (i = 0; i < 32; i++) {
                wx |= ((quad >> (i * 2)) & 1) << i;
                wy |= ((quad >> (i * 2 + 1)) & 1) << i;
        }

	// then offset origin

	wx -= (long long) x << (32 - z);
	wy -= (long long) y << (32 - z);

	// then scale. requires sign-extending shift

	*ox = wx >> (32 - z - 8);
	*oy = wy >> (32 - z - 8);
}

void quad2fxy(unsigned long long quad, double *ox, double *oy, int z, int x, int y) {
	long long wx = 0, wy = 0;
	int i;

	// first decode into world coordinates

        for (i = 0; i < 32; i++) {
                wx |= ((quad >> (i * 2)) & 1) << i;
                wy |= ((quad >> (i * 2 + 1)) & 1) << i;
        }

	// then offset origin

	wx -= (long long) x << (32 - z);
	wy -= (long long) y << (32 - z);

	// then scale. requires sign-extending shift

	*ox = (double) wx / (1 << (32 - z - 8));
	*oy = (double) wy / (1 << (32 - z - 8));
}

void putPixel(int x0, int y0, double *image, double add) {
	if (x0 >= 0 && y0 >= 0 && x0 <= 255 && y0 <= 255) {
		image[y0 * 256 + x0] += add;
	}
}

// http://rosettacode.org/wiki/Bitmap/Bresenham's_line_algorithm#C
void drawLine(int x0, int y0, int x1, int y1, double *image, double add) {
        int dx = abs(x1 - x0), sx = (x0 < x1) ? 1 : -1;
        int dy = abs(y1 - y0), sy = (y0 < y1) ? 1 : -1;
        int err = ((dx > dy) ? dx : -dy) / 2, e2;

	while (1) {
                if (x0 == x1 && y0 == y1) {
			break;
		}

		putPixel(x0, y0, image, add);

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

void plot(int x0, int y0, double c, double *image, double add) {
	putPixel(x0, y0, image, add * c);
}

double fpart(double x) {
	return x - floor(x);
}

double rfpart(double x) {
	return 1 - fpart(x);
}

// loosely based on
// http://en.wikipedia.org/wiki/Xiaolin_Wu's_line_algorithm
void antialiasedLine(double x0, double y0, double x1, double y1, double *image, double add) {
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
			plot(y0,     x0, dx * rfpart(y0), image, add);
			plot(y0 + 1, x0, dx *  fpart(y0), image, add);
		} else {
			plot(x0, y0,     dx * rfpart(y0), image, add);
			plot(x0, y0 + 1, dx *  fpart(y0), image, add);
		}

		return;
	}

	// there is a fractional pixel at the start
	if (x0 != floor(x0)) {
		double yy = y0 + .5 * rfpart(x0) * gradient;

		if (steep) {
			plot(yy,     x0, rfpart(x0) * rfpart(yy), image, add);
			plot(yy + 1, x0, rfpart(x0) *  fpart(yy), image, add);
		} else {
			plot(x0, yy,     rfpart(x0) * rfpart(yy), image, add);
			plot(x0, yy + 1, rfpart(x0) *  fpart(yy), image, add);
		}

		y0 += gradient * rfpart(x0);
		x0 = ceil(x0);
	}

	// there is a fractional pixel at the end
	if (x1 != floor(x1)) {
		double yy = y1 - .5 * fpart(x1) * gradient;

		if (steep) {
			plot(yy,     x1, fpart(x1) * rfpart(yy), image, add);
			plot(yy + 1, x1, fpart(x1) *  fpart(yy), image, add);
		} else {
			plot(x1, yy,     fpart(x1) * rfpart(yy), image, add);
			plot(x1, yy + 1, fpart(x1) *  fpart(yy), image, add);
		}

		y1 -= gradient * fpart(x1);
		x1 = floor(x1);
	}

	// now there are only whole pixels along the path

	// the middle of each whole pixel is halfway through a step
	y0 += .5 * gradient;

	for (; x0 < x1; x0++) {
		if (steep) {
			plot(y0,     x0, rfpart(y0), image, add);
			plot(y0 + 1, x0,  fpart(y0), image, add);

		} else {
			plot(x0, y0,     rfpart(y0), image, add);
			plot(x0, y0 + 1,  fpart(y0), image, add);
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
void drawClip(double x0, double y0, double x1, double y1, double *image, double add) {
        double dx = fabs(x1 - x0);
        double dy = fabs(y1 - y0);
	add /= sqrt(dx * dx + dy * dy);

	if (add < 5) {
		return;
	}

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
		antialiasedLine(x0, y0, x1, y1, image, add);
	}
}

void process(int z_lookup, unsigned char *startbuf, unsigned char *endbuf, int z_draw, int x_draw, int y_draw, double *image) {
	char fname[strlen(FNAME) + 3 + 5 + 1];
	sprintf(fname, "%s/%d.sort", FNAME, z_lookup);

	int fd = open(fname, O_RDONLY);
	if (fd < 0) {
		perror(fname);
		exit(EXIT_FAILURE);
	}

	struct stat st;
	if (fstat(fd, &st) < 0) {
		perror("stat");
		exit(EXIT_FAILURE);
	}

	// fprintf(stderr, "size: %016llx\n", st.st_size);

	unsigned long long *map = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
	if (map == MAP_FAILED) {
		perror("mmap");
		exit(EXIT_FAILURE);
	}

	unsigned char *start = search(startbuf, map, st.st_size / BYTES, BYTES, quadcmp);
	unsigned char *end = search(endbuf, map, st.st_size / BYTES, BYTES, quadcmp);

	// fprintf(stderr, "%016llx %016llx\n", startquad, endquad);
	// fprintf(stderr, "%016llx %016llx\n", start - map, end - map);

	end += BYTES; // points to the last value in range; need the one after that

	if (start != end && memcmp(start, end, BYTES) != 0) {
		start += BYTES; // if not exact match, points to element before match
	}

	unsigned count = (end - start) / BYTES;

	// no real rationale for exponent -- chosen by experiment
	int bright = exp(log(1.53) * z_draw) * 2.3;

	unsigned int j;
	for (j = 0; j < count; j += 1) {
		unsigned long long quad1 = buf2quad(start + j * BYTES);
		unsigned long long quad2 = buf2quad(start + j * BYTES + BYTES / 2);

		double x1, y1;
		double x2, y2;

		quad2fxy(quad1, &x1, &y1, z_draw, x_draw, y_draw);
		quad2fxy(quad2, &x2, &y2, z_draw, x_draw, y_draw);

		drawClip(x1, y1, x2, y2, image, bright);
	}

	munmap(map, st.st_size);
	close(fd);
}

int main(int argc, char **argv) {
	int i;

	if (argc < 4) {
		fprintf(stderr, "Usage: %s z x y\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	unsigned int z_draw = atoi(argv[1]);
	unsigned int x_draw = atoi(argv[2]);
	unsigned int y_draw = atoi(argv[3]);

	double image[256 * 256];
	memset(image, 0, sizeof(image));

	unsigned long long startquad = 0;

	for (i = 0; i < z_draw; i++) {
		startquad |= ((x_draw >> i) & 1LL) << (2 * (i + (32 - z_draw)));
		startquad |= ((y_draw >> i) & 1LL) << (2 * (i + (32 - z_draw)) + 1);
	}

	unsigned long long endquad = startquad;

	for (i = 0; i < 32 - z_draw; i++) {
		endquad |= 3LL << (2 * i);
	}

	unsigned char startbuf[BYTES];
	unsigned char endbuf[BYTES];
	quad2buf(startquad, startbuf);
	quad2buf(endquad, endbuf);

	int z_lookup;

	for (z_lookup = z_draw; z_lookup < z_draw + 9 && z_lookup < 24; z_lookup++) {
		process(z_lookup, startbuf, endbuf, z_draw, x_draw, y_draw, image);
	}

	int x_lookup = x_draw, y_lookup = y_draw;

	for (z_lookup = z_draw - 1; z_lookup >= 0; z_lookup--) {
		x_lookup /= 2;
		y_lookup /= 2;

		fprintf(stderr, "looking at %d %d %d\n", z_lookup, x_lookup, y_lookup);

		startquad = 0;

		for (i = 0; i < z_lookup; i++) {
			startquad |= ((x_lookup >> i) & 1LL) << (2 * (i + (32 - z_lookup)));
			startquad |= ((y_lookup >> i) & 1LL) << (2 * (i + (32 - z_lookup)) + 1);
		}

		endquad = startquad;

		for (i = 0; i < 32 - z_lookup; i++) {
			endquad |= 3LL << (2 * i);
		}

		fprintf(stderr, "that's %llx to %llx\n", startquad, endquad);

		unsigned char startbuf[BYTES];
		unsigned char endbuf[BYTES];
		quad2buf(startquad, startbuf);
		quad2buf(endquad, endbuf);

		process(z_lookup, startbuf, endbuf, z_draw, x_draw, y_draw, image);
	}

double limit = 400;
double limit2 = 2000;
#define OPACITY 224

	unsigned char img2[256 * 256 * 4];
	for (i = 0; i < 256 * 256; i++) {
		if (image[i] == 0) {
			img2[4 * i + 0] = 0;
			img2[4 * i + 1] = 0;
			img2[4 * i + 2] = 0;
			img2[4 * i + 3] = OPACITY;
		} else {
			if (image[i] <= limit) {
				img2[4 * i + 0] = 0;
				img2[4 * i + 1] = 255 * (image[i] / limit);
				img2[4 * i + 2] = 0;
				img2[4 * i + 3] = 255 * (image[i] / limit) +
						  OPACITY * (1 - (image[i] / limit));
			} else if (image[i] <= limit2) {
				img2[4 * i + 0] = 255 * (image[i] - limit) / (limit2 - limit);
				img2[4 * i + 1] = 255;
				img2[4 * i + 2] = 255 * (image[i] - limit) / (limit2 - limit);
				img2[4 * i + 3] = 255;
			} else {
				img2[4 * i + 0] = 255;
				img2[4 * i + 1] = 255;
				img2[4 * i + 2] = 255;
				img2[4 * i + 3] = 255;
			}
		}
	}

	out(img2, 256, 256);
	return 0;
}
