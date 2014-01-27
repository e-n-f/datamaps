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
#include "dump.h"

struct file {
	FILE *f;
	int components;
	int zoom;
	int bytes;
	unsigned char *buf;
	int done;

	struct file *next;
};

void usage(char **argv) {
	fprintf(stderr, "Usage: %s [-ad] [-z max] [-Z min] [-b minlat,minlon,maxlat,maxlon] file\n", argv[0]);
	exit(EXIT_FAILURE);
}

int filecmp(const void *v1, const void *v2) {
	const struct file *const *f1 = v1;
	const struct file *const *f2 = v2;

	return memcmp((*f1)->buf, (*f2)->buf, gSortBytes);
}

struct tile {
	int xtile;
	int ytile;
	long long count;
	double len;
	long long xsum;
	long long ysum;
	int sibling[2][2];
};

struct bounds {
	double minlat;
	double minlon;
	double maxlat;
	double maxlon;
};

void handle(long long xx, long long yy, struct tile *tile, char *fname, int minzoom, int maxzoom, int showdist, unsigned int *x, unsigned int *y, struct file **files, int sibling, int verbose, struct bounds *bounds) {
	int z;

	for (z = minzoom; z <= maxzoom; z++) {
		if (tile[z].xtile != xx >> (32 - z) ||
		    tile[z].ytile != yy >> (32 - z)) {
			if (tile[z].count > 0) {
				printf("%s %d %d %d",
					fname,
					z,
					tile[z].xtile,
					tile[z].ytile);

				if (verbose) {
					double lat, lon;
					tile2latlon(tile[z].xsum / tile[z].count, tile[z].ysum / tile[z].count,
						    32, &lat, &lon);

					printf(" %lld %lf,%lf", tile[z].count, lat, lon);
				}

				if (showdist) {
					printf(" %f", tile[z].len);
				}

				int qx = tile[z].xtile % 2;
				int qy = tile[z].ytile % 2;
				tile[z].sibling[qx][qy] = 1;

				printf("\n");
			}

			if (sibling && tile[z].xtile >= 0 && z > 0) {
				if (tile[z].xtile / 2 != (xx >> (32 - z)) / 2 ||
				    tile[z].ytile / 2 != (yy >> (32 - z)) / 2) {
					int qx, qy;
					for (qx = 0; qx < 2; qx++) {
						for (qy = 0; qy < 2; qy++) {
							if (tile[z].sibling[qx][qy] == 0) {
								printf("%s %d %d %d",
									fname, z,
									tile[z].xtile / 2 * 2 + qx,
									tile[z].ytile / 2 * 2 + qy);

								if (verbose) {
									double lat, lon;
									tile2latlon(tile[z].xtile / 2 * 2 + qx,
										    tile[z].ytile / 2 * 2 + qy,
										    z, &lat, &lon);

									printf(" 0 %lf,%lf", lat, lon);
								}

								if (showdist) {
									printf(" %f", 0.0);
								}

								printf("\n");
							}
						}
					}

					memset(tile[z].sibling, 0, sizeof(tile[z].sibling));
				}
			}

			tile[z].xtile = xx >> (32 - z);
			tile[z].ytile = yy >> (32 - z);
			tile[z].count = 0;
			tile[z].len = 0;
			tile[z].xsum = tile[z].ysum = 0;
		}

		int include = 0;
		if (bounds == NULL) {
			include = 1;
		} else {
			double lat, lon;
			tile2latlon(xx, yy, 32, &lat, &lon);
			if (lat >= bounds->minlat && lat <= bounds->maxlat &&
			    lon >= bounds->minlon && lon <= bounds->maxlon) {
				include = 1;
			}
		}

		if (include) {
			tile[z].count++;
			tile[z].xsum += xx;
			tile[z].ysum += yy;

			if (showdist && x != NULL) {
				int i;
				double dist = 0;
				double max = 1LL << (32 - z);

				for (i = 0; i + 1 < files[0]->components; i++) {
					double d1 = (long long) x[i] - x[i + 1];
					double d2 = (long long) y[i] - y[i + 1];
					double d = sqrt(d1 * d1 + d2 * d2);

#define MAX 6400  /* ~200 feet */
					if (d < MAX) {
						dist += sqrt(d1 * d1 + d2 * d2) / max;
					}
				}

				tile[z].len += dist;
			}
		}
	}
}

void insert(struct file *m, struct file **head, int bytes) {
	while (*head != NULL && memcmp(m->buf, (*head)->buf, bytes) > 0) {
		head = &((*head)->next);
	}

	m->next = *head;
	*head = m;
}

int main(int argc, char **argv) {
	int i;
	extern int optind;
	extern char *optarg;

	int maxzoom = -1;
	int minzoom = 0;
	int showdist = 0;
	int sibling = 0;
	int all = 0;
	int verbose = 0;
	int usebounds = 0;

	struct bounds bounds;
	bounds.minlat = -90;
	bounds.minlon = -180;
	bounds.maxlat = 90;
	bounds.maxlon = 180;

	while ((i = getopt(argc, argv, "z:Z:aDdsvb:")) != -1) {
		switch (i) {
		case 'z':
			maxzoom = atoi(optarg);
			break;

		case 'Z':
			minzoom = atoi(optarg);
			break;

		case 'd':
			showdist = 1;
			break;

		case 's':
			sibling = 1;
			break;

		case 'a':
			all = 1;
			break;

		case 'D':
			all = 2;
			break;

		case 'b':
			if (sscanf(optarg, "%lf,%lf,%lf,%lf", &bounds.minlat, &bounds.minlon,
					&bounds.maxlat, &bounds.maxlon) != 4) {
				usage(argv);
			}
			usebounds = 1;
			break;

		case 'v':
			verbose = 1;
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

	struct tile tile[maxzoom + 1];

	long long size_total = 0;
	long long size_read = 0;
	int size_progress = -1;

	for (i = 0; i <= maxzoom; i++) {
		tile[i].xtile = tile[i].ytile = -1;
		tile[i].count = 0;
		tile[i].len = 0;
		tile[i].xsum = tile[i].ysum = 0;
		memset(tile[i].sibling, 0, sizeof(tile[i].sibling));
	}

	int z_lookup;
	for (z_lookup = 0; z_lookup < depth; z_lookup++) {
		for (i = 1; i <= maxn; i++) {
			if (i == 1 && z_lookup != 0) {
				continue;
			}

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
				files[nfiles]->done = 0;

				struct stat st;
				if (stat(fn, &st) == 0) {
					size_total += st.st_size;
				}

				if (fread(files[nfiles]->buf, files[nfiles]->bytes, 1, files[nfiles]->f) != 1) {
					memset(files[nfiles]->buf, 0xFF, bytes);
					files[nfiles]->done = 1;
				} else {
					size_read += files[nfiles]->bytes;
				}

				nfiles++;
			}
		}
	}

	if (all) {
		dump_begin(all);
	}

	struct file *head = NULL;
	for (i = 0; i < nfiles; i++) {
		if (!files[i]->done) {
			insert(files[i], &head, bytes);
		}
	}

	while (head != NULL) {
		// The problem with this is that only the first component of
		// each vector is indexed. We actually want all the tiles that
		// the vector intersects, and could queue those by doing
		// line drawing, except that the first component might not
		// actually have the lowest tile number. How to fix?

		unsigned int x[head->components], y[head->components];
		unsigned long long meta = 0;
		buf2xys(head->buf, mapbits, metabits, head->zoom, head->components, x, y, &meta);

		if (all) {
			dump_out(all, x, y, head->components, metabits, meta);
		} else {
			long long xx = x[0], yy = y[0];

			handle(xx, yy, tile, fname, minzoom, maxzoom, showdist, x, y, files, sibling, verbose,
			       usebounds ? &bounds : NULL);
		}

		if (fread(head->buf, head->bytes, 1, head->f) != 1) {
			memset(head->buf, 0xFF, bytes);
			head->done = 1;

			head = head->next;
		} else {
			size_read += head->bytes;

			struct file *m = head;
			head = m->next;
			m->next = NULL;

			insert(m, &head, bytes);

			if (100 * size_read / size_total != size_progress) {
				fprintf(stderr, "enumerate: %lld%% \r", 100 * size_read / size_total);
				size_progress = 100 * size_read / size_total;
			}
		}
	}

	if (all) {
		dump_end(all);
	} else {
		handle(-1, -1, tile, fname, minzoom, maxzoom, showdist, NULL, NULL, files, sibling, verbose,
		       usebounds ? &bounds : NULL);
	}

	return 0;
}
