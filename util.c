#include <stdlib.h>
#include <string.h>
#include <math.h>

int gSortBytes;
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

// http://wiki.openstreetmap.org/wiki/Slippy_map_tilenames
void latlon2tile(double lat, double lon, int zoom, unsigned int *x, unsigned int *y) {
        double lat_rad = lat * M_PI / 180;
        unsigned long long n = 1LL << zoom;

        *x = n * ((lon + 180) / 360);
        *y = n * (1 - (log(tan(lat_rad) + 1/cos(lat_rad)) / M_PI)) / 2;
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

	n /= 2;
	int ob = *offbits;

	for (i = 31 - skip; i > 31 - n; i--) {
		// Bits come from x32 and y32 high-bit first

		int xb = (x32 >> i) & 1;
		int yb = (y32 >> i) & 1;

		// And go into the buffer high-bit first

		unsigned int shift = 7 - (ob & 7);

		buf[ob >> 3] |= (yb << shift) | (xb << (shift - 1));
		ob += 2;
	}

	*offbits = ob;
}

// Fill startbuf and endbuf with the bit patterns for the start and end of the specified tile
void zxy2bufs(unsigned int z, unsigned int x, unsigned int y, unsigned char *startbuf, unsigned char *endbuf, int bytes) {
	memset(startbuf, 0, bytes);
	memset(endbuf, 0, bytes);

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

void meta2buf(int bits, long long data, unsigned char *buf, int *offbits, int max) {
	int i;
 
	for (i = bits - 1; i >= 0 && *offbits < max; i--) {
		int b = (data >> i) & 1;
		buf[*offbits / 8] |= b << (7 - (*offbits % 8));
		(*offbits)++;
	}
}

int bytesfor(int mapbits, int metabits, int components, int z_lookup) {
	int bits = mapbits + metabits + (mapbits - 2 * z_lookup) * (components - 1);

	return (bits + 7) / 8;
}
