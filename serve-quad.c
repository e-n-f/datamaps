#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string.h>
#include <png.h>
#include <math.h>

#define FNAME "2013-04-09.quad.sort"

int quadcmp(const void *v1, const void *v2) {
	const unsigned long long *q1 = v1;
	const unsigned long long *q2 = v2;

	if (*q1 > *q2) {
		return 1;
	} else if (*q1 == *q2) {
		return 0;
	} else {
		return -1;
	}
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

int main(int argc, char **argv) {
	if (argc < 4) {
		fprintf(stderr, "Usage: %s zoom x y\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	unsigned int zoom = atoi(argv[1]);
	unsigned int x = atoi(argv[2]);
	unsigned int y = atoi(argv[3]);

	int fd = open(FNAME, O_RDONLY);
	if (fd < 0) {
		perror(FNAME);
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

	unsigned long long startquad = 0;

	int i;
	for (i = 0; i < zoom; i++) {
		startquad |= ((x >> i) & 1LL) << (2 * (i + (32 - zoom)));
		startquad |= ((y >> i) & 1LL) << (2 * (i + (32 - zoom)) + 1);
	}

	unsigned long long endquad = startquad;

	for (i = 0; i < 32 - zoom; i++) {
		endquad |= 3LL << (2 * i);
	}

	unsigned long long *start = search(&startquad, map, st.st_size / sizeof(startquad), sizeof(startquad), quadcmp);
	unsigned long long *end = search(&endquad, map, st.st_size / sizeof(endquad), sizeof(endquad), quadcmp);

	// fprintf(stderr, "%016llx %016llx\n", startquad, endquad);
	// fprintf(stderr, "%016llx %016llx\n", start - map, end - map);

	end += 1; // points to the last value in range; need the one after that

	if (start != end && *start != *end) {
		start += 1; // if not exact match, points to element before match
	}

	unsigned count = end - start;
	fprintf(stderr, "size: %u  %d %d %d\n", count, zoom, x, y);

	unsigned char image[256 * 256 * 4];
	memset(image, 0, 256 * 256 * 4);

	for (i = 0; i < 256 * 256; i++) {
		image[4 * i + 3] = 192;
	}

	int step;
	int dup;
#define ALL 13

	if (zoom >= ALL) {
		step = 1;
		dup = 1 << (zoom - ALL);
	} else {
		step = 1 << (ALL - zoom);
		dup = 1;
	}

	unsigned int j;
	for (j = 0; j < count; j += step) {
		unsigned long long quad = start[j];
		int xx = 0, yy = 0;
		int n;

		for (n = 0; n < 8; n++) {
			xx |= ((quad >> (2 * (32 - zoom - 8 + n))) & 1) << n;
			yy |= ((quad >> (2 * (32 - zoom - 8 + n) + 1)) & 1) << n;
		}

		if (image[4 * (yy * 256 + xx) + 1] == 255) {
#define STEP 3
			if ((int) image[4 * (yy * 256 + xx) + 0] + STEP <= 255) {
				image[4 * (yy * 256 + xx) + 0] += STEP;
				image[4 * (yy * 256 + xx) + 2] += STEP;
			}
		} else {
			image[4 * (yy * 256 + xx) + 0] = 0;
			image[4 * (yy * 256 + xx) + 1] = 255;
			image[4 * (yy * 256 + xx) + 2] = 0;
			image[4 * (yy * 256 + xx) + 3] = 255;
		}
	}

	out(image, 256, 256);
	return 0;
}
