#include <stdio.h>
#include <stdlib.h>
#include <math.h>

// http://wiki.openstreetmap.org/wiki/Slippy_map_tilenames
void latlon2tile(double lat, double lon, int zoom, unsigned int *x, unsigned int *y) {
	double lat_rad = lat * M_PI / 180;
	unsigned long long n = 1LL << zoom;

	*x = n * ((lon + 180) / 360);
	*y = n * (1 - (log(tan(lat_rad) + 1/cos(lat_rad)) / M_PI)) / 2;
}

int main(int argc, char **argv) {
	char s[2000];

	while (fgets(s, 2000, stdin)) {
		double lat, lon;

		if (sscanf(s, " <trkpt lat=\"%lf\" lon=\"%lf\"", &lat, &lon) == 2) {
			unsigned int x, y;
			long long quad = 0;
			int i;

			latlon2tile(lat, lon, 32, &x, &y);

			for (i = 0; i < 32; i++) {
				quad |= ((x >> i) & 1LL) << (i * 2);
				quad |= ((y >> i) & 1LL) << (i * 2 + 1);
			}

			fwrite(&quad, sizeof(quad), 1, stdout);
		}
	}

	return 0;
}
