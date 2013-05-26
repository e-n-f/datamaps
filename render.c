#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string.h>
#include <png.h>
#include <math.h>

static int gSortBytes;
int bufcmp(const void *v1, const void *v2) {
	return memcmp(v1, v2, gSortBytes);
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

// Convert world coordinates to floating pixels within a particular tile
void wxy2fxy(long long wx, long long wy, double *ox, double *oy, int z, int x, int y) {
	// then offset origin

	wx -= (long long) x << (32 - z);
	wy -= (long long) y << (32 - z);

	// then scale

	*ox = (double) wx / (1 << (32 - z - 8));
	*oy = (double) wy / (1 << (32 - z - 8));
}

// Convert world coordinates to a bit stream
void xy2buf(unsigned int x32, unsigned int y32, unsigned char *buf, int *offbits, int n, int skip) {
	int i;

	for (i = 31 - skip; i > 31 - n / 2; i--) {
		// Bits come from x32 and y32 high-bit first

		int xb = (x32 >> i) & 1;
		int yb = (y32 >> i) & 1;

		// And go into the buffer high-bit first

		buf[*offbits / 8] |=  yb << (7 - (*offbits % 8));
		(*offbits)++;
		buf[*offbits / 8] |=  xb << (7 - (*offbits % 8));
		(*offbits)++;
	}
}

// Fill startbuf and endbuf with the bit patterns for the start and end of the specified tile
void zxy2bufs(unsigned int z, unsigned int x, unsigned int y, unsigned char *startbuf, unsigned char *endbuf, int bytes) {
	int i = 0;

	x <<= (32 - z);
	y <<= (32 - z);

	xy2buf(x, y, startbuf, &i, 2 * z, 0);
	memcpy(endbuf, startbuf, bytes);
	for (; i < bytes * 8; i++) { // fill meta bits, too
		endbuf[i / 8] |= 1 << (7 - (i % 8));
	}
}

// Convert a bit stream to N xy pairs (world coordinates)
void buf2xys(unsigned char *buf, int mapbits, int skip, int n, unsigned int *x, unsigned int *y) {
	int i, j;
	int offbits = 0;

	for (i = 0; i < n; i++) {
		x[i] = 0;
		y[i] = 0;
	}

	// First pull off the common bits

	for (i = 31; i > 31 - skip; i--) {
		int y0 = (buf[offbits / 8] >> (7 - offbits % 8)) & 1;
		offbits++;
		int x0 = (buf[offbits / 8] >> (7 - offbits % 8)) & 1;
		offbits++;

		for (j = 0; j < n; j++) {
			x[j] |= x0 << i;
			y[j] |= y0 << i;
		}
	}

	// and then the remainder for each component

	for (j = 0; j < n; j++) {
		for (i = 31 - skip; i > 31 - mapbits / 2; i--) {
			int y0 = (buf[offbits / 8] >> (7 - offbits % 8)) & 1;
			offbits++;
			int x0 = (buf[offbits / 8] >> (7 - offbits % 8)) & 1;
			offbits++;

			x[j] |= x0 << i;
			y[j] |= y0 << i;
		}
	}
}

int bytesfor(int mapbits, int metabits, int components, int z_lookup) {
	int bits = mapbits + metabits;

	int i;
	for (i = 1; i < components; i++) {
		bits += mapbits - 2 * z_lookup;
	}

	return (bits + 7) / 8;
}

void putPixel(int x0, int y0, double add, double *image) {
	if (x0 >= 0 && y0 >= 0 && x0 <= 255 && y0 <= 255) {
		image[y0 * 256 + x0] += add;
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
			putPixel(y0,     x0, dx * rfpart(y0) * add, image);
			putPixel(y0 + 1, x0, dx *  fpart(y0) * add, image);
		} else {
			putPixel(x0, y0,     dx * rfpart(y0) * add, image);
			putPixel(x0, y0 + 1, dx *  fpart(y0) * add, image);
		}

		return;
	}

	// there is a fractional pixel at the start
	if (x0 != floor(x0)) {
		double yy = y0 + .5 * rfpart(x0) * gradient;

		if (steep) {
			putPixel(yy,     x0, rfpart(x0) * rfpart(yy) * add, image);
			putPixel(yy + 1, x0, rfpart(x0) *  fpart(yy) * add, image);
		} else {
			putPixel(x0, yy,     rfpart(x0) * rfpart(yy) * add, image);
			putPixel(x0, yy + 1, rfpart(x0) *  fpart(yy) * add, image);
		}

		y0 += gradient * rfpart(x0);
		x0 = ceil(x0);
	}

	// there is a fractional pixel at the end
	if (x1 != floor(x1)) {
		double yy = y1 - .5 * fpart(x1) * gradient;

		if (steep) {
			putPixel(yy,     x1, fpart(x1) * rfpart(yy) * add, image);
			putPixel(yy + 1, x1, fpart(x1) *  fpart(yy) * add, image);
		} else {
			putPixel(x1, yy,     fpart(x1) * rfpart(yy) * add, image);
			putPixel(x1, yy + 1, fpart(x1) *  fpart(yy) * add, image);
		}

		y1 -= gradient * fpart(x1);
		x1 = floor(x1);
	}

	// now there are only whole pixels along the path

	// the middle of each whole pixel is halfway through a step
	y0 += .5 * gradient;

	for (; x0 < x1; x0++) {
		if (steep) {
			putPixel(y0,     x0, rfpart(y0) * add, image);
			putPixel(y0 + 1, x0,  fpart(y0) * add, image);
		} else {
			putPixel(x0, y0,     rfpart(y0) * add, image);
			putPixel(x0, y0 + 1,  fpart(y0) * add, image);
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

void process1(char *fname, unsigned char *startbuf, unsigned char *endbuf, int z_draw, int x_draw, int y_draw,
	      double *image, int mapbits, int metabits, int bytes) {
	char fn[strlen(fname) + 1 + 3 + 1];
	sprintf(fn, "%s/1,0", fname);

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

	if (start != end && memcmp(start, startbuf, bytes) != 0) {
		// XXX is this true with meta bits?
		start += bytes; // if not exact match, points to element before match
	}

#define ALL 13

	int step;
        if (z_draw >= ALL) {
                step = 1;
        } else {
                step = 1 << (ALL - z_draw);
        }

	for (; start < end; start += step * bytes) {
		unsigned int x = 0, y = 0;
		double xd, yd;
		buf2xys(start, mapbits, 0, 1, &x, &y);
		wxy2fxy(x, y, &xd, &yd, z_draw, x_draw, y_draw);

		putPixel(xd,     yd,     100 * rfpart(xd) * rfpart(yd), image);
		putPixel(xd + 1, yd,     100 *  fpart(xd) * rfpart(yd), image);
		putPixel(xd,     yd + 1, 100 * rfpart(xd) *  fpart(yd), image);
		putPixel(xd + 1, yd + 1, 100 *  fpart(xd) *  fpart(yd), image);
	}

	munmap(map, st.st_size);
	close(fd);
}

void process(char *fname, int components, int z_lookup, unsigned char *startbuf, unsigned char *endbuf, int z_draw, int x_draw, int y_draw, double *image, int mapbits, int metabits) {
	int bytes = bytesfor(mapbits, metabits, components, z_lookup);

	char fn[strlen(fname) + 1 + 5 + 1 + 5 + 1];
	sprintf(fn, "%s/%d,%d", fname, components, z_lookup);

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
		start += bytes; // if not exact match, points to element before match
	}

	unsigned count = (end - start) / bytes;

	// no real rationale for exponent -- chosen by experiment
	int bright = exp(log(1.53) * z_draw) * 2.3;

	unsigned int j;
	for (j = 0; j < count; j += 1) {
		int k;

		unsigned int x[components], y[components];
		double xd[components], yd[components];

		buf2xys(start + j * bytes, mapbits, z_lookup, components, x, y);

		for (k = 0; k < components; k++) {
			wxy2fxy(x[k], y[k], &xd[k], &yd[k], z_draw, x_draw, y_draw);
		}

		for (k = 1; k < components; k++) {
			drawClip(xd[k - 1], yd[k - 1], xd[k], yd[k], image, bright);
		}
	}

	munmap(map, st.st_size);
	close(fd);
}

int main(int argc, char **argv) {
	int i;
	extern int optind;
	extern char *optarg;

	int transparency = 224;

	while ((i = getopt(argc, argv, "t:")) != -1) {
		switch (i) {
		case 't':
			transparency = atoi(optarg);
			break;
		}
	}

	if (argc - optind != 4) {
		fprintf(stderr, "Usage: %s file z x y\n", argv[0]);
		exit(EXIT_FAILURE);
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

	// For zoom levels smaller than this one, we look up the entire area
	// of the tile we are drawing, which will end up being multiple tiles
	// of the higher zoom.

	unsigned char startbuf[bytes];
	unsigned char endbuf[bytes];
	memset(startbuf, 0, bytes);
	memset(endbuf, 0, bytes);
	zxy2bufs(z_draw, x_draw, y_draw, startbuf, endbuf, bytes);

	// Do the single-point case

	process1(fname, startbuf, endbuf, z_draw, x_draw, y_draw, image, mapbits, metabits, bytes);

	// Do the zoom levels smaller than this one

	int z_lookup;
	for (z_lookup = z_draw + 1; z_lookup < z_draw + 9 && z_lookup < mapbits / 2; z_lookup++) {
		for (i = 2; i <= maxn; i++) {
			int bytes = bytesfor(mapbits, metabits, i, z_lookup);

			unsigned char startbuf[bytes];
			unsigned char endbuf[bytes];
			memset(startbuf, 0, bytes);
			memset(endbuf, 0, bytes);
			zxy2bufs(z_draw, x_draw, y_draw, startbuf, endbuf, bytes);

			process(fname, i, z_lookup, startbuf, endbuf, z_draw, x_draw, y_draw, image, mapbits, metabits);
		}
	}

	// For zoom levels larger than this one, each stage looks up a
	// larger area for potential overlaps.

	int x_lookup, y_lookup;
	for (z_lookup = z_draw, x_lookup = x_draw, y_lookup = y_draw;
	     z_lookup >= 0;
	     z_lookup--, x_lookup /= 2, y_lookup /= 2) {
		for (i = 2; i <= maxn; i++) {
			int bytes = bytesfor(mapbits, metabits, i, z_lookup);

			unsigned char startbuf[bytes];
			unsigned char endbuf[bytes];
			memset(startbuf, 0, bytes);
			memset(endbuf, 0, bytes);

			zxy2bufs(z_lookup, x_lookup, y_lookup, startbuf, endbuf, bytes);
			process(fname, i, z_lookup, startbuf, endbuf, z_draw, x_draw, y_draw, image, mapbits, metabits);
		}
	}

double limit = 400;
double limit2 = 2000;

	unsigned char img2[256 * 256 * 4];
	for (i = 0; i < 256 * 256; i++) {
		if (image[i] == 0) {
			img2[4 * i + 0] = 0;
			img2[4 * i + 1] = 0;
			img2[4 * i + 2] = 0;
			img2[4 * i + 3] = transparency;
		} else {
			if (image[i] <= limit) {
				img2[4 * i + 0] = 0;
				img2[4 * i + 1] = 255 * (image[i] / limit);
				img2[4 * i + 2] = 0;
				img2[4 * i + 3] = 255 * (image[i] / limit) +
						  transparency * (1 - (image[i] / limit));
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
