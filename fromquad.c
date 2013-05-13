#include <stdio.h>
#include <stdlib.h>
#include <math.h>

// http://wiki.openstreetmap.org/wiki/Slippy_map_tilenames
void tile2latlon(unsigned int x, unsigned int y, int zoom, double *lat, double *lon) {
	unsigned long long n = 1LL << zoom;
	*lon = 360.0 * x / n - 180.0;
	double lat_rad = atan(sinh(M_PI * (1 - 2.0 * y / n)));
	*lat = lat_rad * 180 / M_PI;
}

int main() {
	unsigned long long quad;

	while (fread(&quad, sizeof(quad), 1, stdin) > 0) {
		unsigned int x = 0, y = 0;
		double lat, lon;
		int i;

		for (i = 0; i < 32; i++) {
			x |= ((quad >> (i * 2)) & 1) << i;
			y |= ((quad >> (i * 2 + 1)) & 1) << i;
		}

		tile2latlon(x, y, 32, &lat, &lon);
		printf("%lf,%lf\n", lat, lon);
	}

	return 0;
}
