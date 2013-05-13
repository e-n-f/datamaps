#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#define LEVELS 24

// http://wiki.openstreetmap.org/wiki/Slippy_map_tilenames
void latlon2tile(double lat, double lon, int zoom, unsigned int *x, unsigned int *y) {
	double lat_rad = lat * M_PI / 180;
	unsigned long long n = 1LL << zoom;

	*x = n * ((lon + 180) / 360);
	*y = n * (1 - (log(tan(lat_rad) + 1/cos(lat_rad)) / M_PI)) / 2;
}

void out(unsigned long long quad, FILE *fp) {
	int i;

	quad >>= (64 - (2 * LEVELS));

	for (i = 0; i < 2 * LEVELS; i += 8) {
		fputc((quad >> i) & 0xFF, fp);
	}
}

int main(int argc, char **argv) {
	char s[2000];
	unsigned long long oquad = 0;
	int within = 0;
	FILE *fp[LEVELS];

	if (argc != 2) {
		fprintf(stderr, "Usage: %s outdir\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	int i;
	for (i = 0; i < LEVELS; i++) {
		char fname[strlen(argv[1]) + 3 + 1];
		sprintf(fname, "%s/%d", argv[1], i);
		fp[i] = fopen(fname, "w");
		if (fp[i] == NULL) {
			perror(fname);
			exit(EXIT_FAILURE);
		}
	}

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

			if (within) {
				for (i = 0; i < LEVELS; i++) {
					if ((quad  & (3LL << (31 - i) * 2)) !=
					    (oquad & (3LL << (31 - i) * 2))) {
						break;
					}
				}

				if (i < LEVELS) {
					out(oquad, fp[i]);
					out(quad, fp[i]);
				}
			}

			within = 1;
			oquad = quad;
		} else if (strstr(s, "</trkseg") != NULL || strstr(s, "</track") != NULL) {
			within = 0;
		}
	}

	return 0;
}
