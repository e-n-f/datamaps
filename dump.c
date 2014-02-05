#include <stdio.h>
#include "dump.h"
#include "util.h"

static int first = 1;

void dump_begin(int dump) {
	if (dump == 2) {
		printf("{\n");
		printf("\"type\": \"FeatureCollection\",\n");
		printf("\"features\": [\n");
	}
}

void dump_end(int dump) {
	if (dump == 2) {
		printf("]\n}\n");
	}
}

void dump_out(int dump, unsigned int *x, unsigned int *y, int components, int metabits, long long meta) {
	if (dump == 2) {
		if (!first) {
			printf(",\n");
		}
		first = 0;

		printf("{ ");
		printf("\"type\": \"Feature\", ");
		printf("\"properties\": {");

		if (metabits != 0) {
			printf(" \"meta\": %lld ", meta);
		}

		printf("}, ");

		printf("\"geometry\": { ");

		if (components == 1) {
			printf("\"type\": \"Point\", ");
		} else {
			printf("\"type\": \"LineString\", ");
		}

		printf("\"coordinates\": [ ");

		int k;
		for (k = 0; k < components; k++) {
			double lat, lon;
			tile2latlon(x[k], y[k], 32, &lat, &lon);

			if (components != 1) {
				printf("[ ");
			}

			printf("%lf, %lf", lon, lat);

			if (components != 1) {
				printf(" ]");
				if (k + 1 < components) {
					printf(",");
				}
			}
			printf(" ");
		}

		printf("] } }\n");
	} else {
		int k;
		for (k = 0; k < components; k++) {
			double lat, lon;
			tile2latlon(x[k], y[k], 32, &lat, &lon);

			printf("%lf,%lf ", lat, lon);
		}

		if (metabits != 0) {
			printf("%d:%lld ", metabits, meta);
		}

		printf("// ");

		for (k = 0; k < components; k++) {
			printf("%08x %08x ", x[k], y[k]);
		}

		printf("\n");
	}
}


