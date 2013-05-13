#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define LEVELS 24

// http://wiki.openstreetmap.org/wiki/Slippy_map_tilenames
void tile2latlon(unsigned int x, unsigned int y, int zoom, double *lat, double *lon) {
	unsigned long long n = 1LL << zoom;
	*lon = 360.0 * x / n - 180.0;
	double lat_rad = atan(sinh(M_PI * (1 - 2.0 * y / n)));
	*lat = lat_rad * 180 / M_PI;
}

void quad2xy(unsigned long long quad, unsigned int *x, unsigned int *y) {
	int i;

	*x = 0;
	*y = 0;

	for (i = 0; i < 32; i++) {
		*x |= ((quad >> (i * 2)) & 1) << i;
		*y |= ((quad >> (i * 2 + 1)) & 1) << i;
	}
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

int main() {
	unsigned char buf[2 * 2 * LEVELS / 8];

	while (fread(buf, sizeof(buf), 1, stdin) > 0) {
		unsigned long long quad1 = buf2quad(buf);
		unsigned long long quad2 = buf2quad(buf + 2 * LEVELS / 8);

		unsigned int x1, y1, x2, y2;
		quad2xy(quad1, &x1, &y1);
		quad2xy(quad2, &x2, &y2);

		double lat1, lon1, lat2, lon2;
		tile2latlon(x1, y1, 32, &lat1, &lon1);
		tile2latlon(x2, y2, 32, &lat2, &lon2);

		printf("%016llx %016llx ", quad1, quad2);
		printf("%lf,%lf to %lf,%lf\n", lat1, lon1, lat2, lon2);
	}

	return 0;
}
