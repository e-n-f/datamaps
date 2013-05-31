#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string.h>
#include <math.h>
#include "util.h"
#include "graphics.h"

struct file {
	FILE *f;
	int components;
	int zoom;
	int bytes;
	unsigned char *buf;
};

void usage(char **argv) {
	fprintf(stderr, "Usage: %s [-z max] file\n", argv[0]);
	exit(EXIT_FAILURE);
}

int filecmp(const void *v1, const void *v2) {
	const struct file *const *f1 = v1;
	const struct file *const *f2 = v2;

	return memcmp((*f1)->buf, (*f2)->buf, gSortBytes);
}

int main(int argc, char **argv) {
	int i;
	extern int optind;
	extern char *optarg;

	int maxzoom = -1;

	while ((i = getopt(argc, argv, "z:")) != -1) {
		switch (i) {
		case 'z':
			maxzoom = atoi(optarg);
			break;

		default:
			usage(argv);
		}
	}

	if (argc - optind != 1) {
		usage(argv);
	}

	char *fname = argv[optind];

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

	if (maxzoom < 0) {
		maxzoom = mapbits / 2 - 8;
	}

	int bytes = (mapbits + metabits + 7) / 8;
	char eof[bytes];
	memset(eof, 0xFF, bytes);
	gSortBytes = bytes;

	int depth;
	if (mapbits / 2 + 1 > maxzoom + 9) {
		depth = mapbits / 2 + 1;
	} else {
		depth = maxzoom + 9;
	}

	struct file *files[depth * maxn];
	int nfiles = 0;

	int xtile[maxzoom + 1];
	int ytile[maxzoom + 1];
	for (i = 0; i <= maxzoom; i++) {
		xtile[i] = ytile[i] = -1;
	}

	int z_lookup;
	for (z_lookup = 0; z_lookup < depth; z_lookup++) {
		for (i = 1; i <= maxn; i++) {
			char fn[strlen(fname) + 1 + 5 + 1 + 5 + 1];
			sprintf(fn, "%s/%d,%d", fname, i, z_lookup);

			FILE *f = fopen(fn, "r");
			if (f == NULL) {
				perror(fn);
			} else {
				files[nfiles] = malloc(sizeof(struct file));
				files[nfiles]->f = f;
				files[nfiles]->components = i;
				files[nfiles]->zoom = z_lookup;
				files[nfiles]->bytes = bytesfor(mapbits, metabits, i, z_lookup);
				files[nfiles]->buf = malloc(files[nfiles]->bytes);

				if (fread(files[nfiles]->buf, files[nfiles]->bytes, 1, files[nfiles]->f) != 1) {
					memset(files[nfiles]->buf, 0xFF, bytes);
				}

				nfiles++;
			}
		}
	}

	while (1) {
		qsort(files, nfiles, sizeof(struct file *), filecmp);

		if (memcmp(files[0]->buf, eof, bytes) == 0) {
			break;
		}

		// The problem with this is that only the first component of
		// each vector is indexed. We actually want all the tiles that
		// the vector intersects, and could queue those by doing
		// line drawing, except that the first component might not
		// actually have the lowest tile number. How to fix?

		unsigned int x[files[0]->components], y[files[0]->components];
		unsigned int meta;
		buf2xys(files[0]->buf, mapbits, metabits, files[0]->zoom, files[0]->components, x, y, &meta);

		double lat, lon;
		tile2latlon(x[0], y[0], 32, &lat, &lon);

		int z;
		for (z = 0; z <= maxzoom; z++) {
			long long xx = x[0], yy = y[0];

			if (xtile[z] != xx >> (32 - z) ||
			    ytile[z] != yy >> (32 - z)) {
				printf("%d %d %d\n",
					z,
					xtile[z] = xx >> (32 - z),
					ytile[z] = yy >> (32 - z));
			}
		}

		// printf("%lf,%lf\n", lat, lon);

		if (fread(files[0]->buf, files[0]->bytes, 1, files[0]->f) != 1) {
			memset(files[0]->buf, 0xFF, bytes);
		}
	}

	return 0;
}
