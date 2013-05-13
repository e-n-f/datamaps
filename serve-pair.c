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

double brightness[] = {
	1,
	3,
	5,
	10,
	255,
	255,
	255,
	255,
	255,
	255,
	255,
	255,
	255,
	255,
	255,
	255,
	768,
	1024,
	2048,
};

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

void drawLine(int x0, int y0, int x1, int y1, unsigned char *image, int zoom, int add) {
	if (x0 < 0 && x1 < 0) {
		return;
	}
	if (x0 > 255 && x1 > 255) {
		return;
	}
	if (y0 < 0 && y1 < 0) {
		return;
	}
	if (y0 > 255 && y1 > 255) {
		return;
	}

        int dx = abs(x1 - x0), sx = (x0 < x1) ? 1 : -1;
        int dy = abs(y1 - y0), sy = (y0 < y1) ? 1 : -1;
        int err = ((dx > dy) ? dx : -dy) / 2, e2;

	// int add = 256 / sqrt(dx * dx + dy * dy);
	// int add = exp(log(1.5) * zoom) / sqrt(dx * dx + dy * dy);
	// int add = brightness[zoom] / sqrt(dx * dx + dy * dy);

	add /= sqrt(dx * dx + dy * dy);

	if (add < 1) {
		return;
	}

	while (1) {
                if (x0 == x1 && y0 == y1) {
			break;
		}

		if (x0 >= 0 && y0 >= 0 && x0 <= 255 && y0 <= 255) {
			image[4 * (y0 * 256 + x0) + 0] = 64;

			int bright = image[4 * (y0 * 256 + x0) + 1] + add;
			if (bright > 255) {
				bright = 255;
			}
			image[4 * (y0 * 256 + x0) + 1] = bright;

			image[4 * (y0 * 256 + x0) + 2] = 64;
			image[4 * (y0 * 256 + x0) + 3] = 255;
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

void process(int zoom, int x, int y, int z, int ox, int oy, unsigned char *startbuf, unsigned char *endbuf, int step, unsigned char *image, int debug) {
	char fname[strlen(FNAME) + 3 + 5 + 1];
	sprintf(fname, "%s/%d.sort", FNAME, zoom);

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
	fprintf(stderr, "size: %u  %d %d %d\n", count, zoom, x, y);

	// no real rationale for exponent -- chosen by experiment
	int bright = exp(log(1.53) * z);

	unsigned int j;
	for (j = 0; j < count; j += step) {
		unsigned long long quad1 = buf2quad(start + j * BYTES);
		unsigned long long quad2 = buf2quad(start + j * BYTES + BYTES / 2);

		int x1, y1;
		int x2, y2;

		quad2xy(quad1, &x1, &y1, z, ox, oy);
		quad2xy(quad2, &x2, &y2, z, ox, oy);

		if (debug) {
			fprintf(stderr, "%d,%d %d,%d\n", x1, y1, x2, y2);
		}

		drawLine(x1, y1, x2, y2, image, z, bright);
	}

	munmap(map, st.st_size);
	close(fd);
}

int main(int argc, char **argv) {
	if (argc < 4) {
		fprintf(stderr, "Usage: %s zoom x y\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	unsigned int z = atoi(argv[1]);
	unsigned int x = atoi(argv[2]);
	unsigned int y = atoi(argv[3]);

	unsigned char image[256 * 256 * 4];
	memset(image, 0, 256 * 256 * 4);

	int i;
	for (i = 0; i < 256 * 256; i++) {
		image[4 * i + 3] = 192;
	}

	int step;
	int dup;
#define ALL 13

	if (z >= ALL) {
		step = 1;
		dup = 1 << (z - ALL);
	} else {
		step = 1 << (ALL - z);
		dup = 1;
	}

	step = 1; // XXX

	unsigned long long startquad = 0;

	for (i = 0; i < z; i++) {
		startquad |= ((x >> i) & 1LL) << (2 * (i + (32 - z)));
		startquad |= ((y >> i) & 1LL) << (2 * (i + (32 - z)) + 1);
	}

	unsigned long long endquad = startquad;

	for (i = 0; i < 32 - z; i++) {
		endquad |= 3LL << (2 * i);
	}

	unsigned char startbuf[BYTES];
	unsigned char endbuf[BYTES];
	quad2buf(startquad, startbuf);
	quad2buf(endquad, endbuf);

	int zoom;
	for (zoom = z; zoom < z + 8 && zoom < 24; zoom++) {
		process(zoom, x, y, z, x, y, startbuf, endbuf, step, image, 0);
	}

	int ox = x, oy = y;

	for (zoom = z - 1; zoom >= z - 8 && zoom >= 0; zoom--) {
		x /= 2;
		y /= 2;

		fprintf(stderr, "looking at %d %d %d\n", zoom, x, y);

		startquad = 0;

		for (i = 0; i < zoom; i++) {
			startquad |= ((x >> i) & 1LL) << (2 * (i + (32 - zoom)));
			startquad |= ((y >> i) & 1LL) << (2 * (i + (32 - zoom)) + 1);
		}

		endquad = startquad;

		for (i = 0; i < 32 - zoom; i++) {
			endquad |= 3LL << (2 * i);
		}

		fprintf(stderr, "that's %llx to %llx\n", startquad, endquad);

		unsigned char startbuf[BYTES];
		unsigned char endbuf[BYTES];
		quad2buf(startquad, startbuf);
		quad2buf(endquad, endbuf);

		process(zoom, x, y, z, ox, oy, startbuf, endbuf, 1, image, 0);
	}

	out(image, 256, 256);
	return 0;
}
