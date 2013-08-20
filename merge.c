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

void usage(char **argv) {
	fprintf(stderr, "Usage: %s [-o outfile] [-u] file ...\n", argv[0]);
	exit(EXIT_FAILURE);
}

struct file {
	FILE *fp;
	int remaining;
	unsigned char *data;
};

int main(int argc, char **argv) {
	int i;
	extern int optind;
	extern char *optarg;

	char *destdir = NULL;
	int uniq = 0;

	while ((i = getopt(argc, argv, "o:u")) != -1) {
		switch (i) {
		case 'o':
			destdir = optarg;
			break;

		case 'u':
			uniq = 1;
			break;

		default:
			usage(argv);
		}
	}

	if (argc - optind < 1 || destdir == NULL) {
		usage(argv);
	}

	int nfile = argc - optind;

	int maxn = 0;
	int mapbits = 0;
	int metabits = 0;

	for (i = 0; i < nfile; i++) {
		char *fname = argv[optind + i];

		char meta[strlen(fname) + 1 + 4 + 1];
		sprintf(meta, "%s/meta", fname);
		FILE *f = fopen(meta, "r");
		if (f == NULL) {
			perror(meta);
			exit(EXIT_FAILURE);
		}

		int file_mapbits, file_metabits, file_maxn;

		char s[2000] = "";
		if (fgets(s, 2000, f) == NULL || strcmp(s, "1\n") != 0) {
			fprintf(stderr, "%s: Unknown version %s", meta, s);
			exit(EXIT_FAILURE);
		}
		if (fgets(s, 2000, f) == NULL || sscanf(s, "%d %d %d", &file_mapbits, &file_metabits, &file_maxn) != 3) {
			fprintf(stderr, "%s: couldn't find size declaration", meta);
			exit(EXIT_FAILURE);
		}
		fclose(f);

		if (i > 0) {
			if (file_mapbits != mapbits || file_metabits != metabits) {
				fprintf(stderr, "Mismatched encoding of %s (-z %d -m %d) and %s (-z %d -m %d)\n",
					argv[optind + i - 1], (mapbits - 16) / 2, metabits,
					argv[optind + i], (file_mapbits - 16) / 2, file_metabits);
				exit(EXIT_FAILURE);
			}
		}

		if (file_maxn > maxn) {
			maxn = file_maxn;
		}

		mapbits = file_mapbits;
		metabits = file_metabits;
	}

	int maxzoom = (mapbits - 16) / 2;

	if (mkdir(destdir, 0777) != 0) {
		perror(destdir);
		exit(EXIT_FAILURE);
	}

	char s[strlen(destdir) + 5 + 1];
	sprintf(s, "%s/meta", destdir);
	FILE *f = fopen(s, "w");
	if (f == NULL) {
		perror(s);
		exit(EXIT_FAILURE);
	}
	fprintf(f, "1\n");
	fprintf(f, "%d %d %d\n", mapbits, metabits, maxn);
	fclose(f);

	int z_lookup;
	for (z_lookup = 0; z_lookup < maxzoom; z_lookup++) {
		for (i = 1; i <= maxn; i++) {
			if (i == 1 && z_lookup != 0) {
				continue;
			}

			int bytes = bytesfor(mapbits, metabits, i, z_lookup);
			printf("merging zoom level %d for point count %d (%d bytes)\n", z_lookup, i, bytes);

			struct file files[nfile];
			int n = 0;
			int remaining = 0;

			int j;
			for (j = 0; j < nfile; j++) {
				char *fname = argv[optind + j];

				char fname2[strlen(fname) + 1 + 5 + 1 + 5 + 1];
				sprintf(fname2, "%s/%d,%d", fname, i, z_lookup);

				files[n].fp = fopen(fname2, "rb");
				if (files[n].fp == NULL) {
					perror(fname2);
				} else {
					files[n].data = malloc(bytes);
					files[n].remaining = fread(files[n].data, bytes, 1, files[n].fp);
					if (files[n].remaining > 0) {
						remaining++;
					}
					n++;
				}
			}

			if (remaining != 0) {
				char outfname[strlen(destdir) + 1 + 5 + 1 + 5 + 1];
				sprintf(outfname, "%s/%d,%d", destdir, i, z_lookup);
				FILE *out = fopen(outfname, "wb");

				if (out == NULL) {
					perror(outfname);
					exit(EXIT_FAILURE);
				}

				while (remaining) {
					int best = -1;

					for (j = 0; j < n; j++) {
						if (files[j].remaining) {
							if (best < 0 || memcmp(files[j].data, files[best].data, bytes) < 0) {
								best = j;
							}
						}
					}

					if (best < 0) {
						fprintf(stderr, "shouldn't happen\n");
						break;
					}

					fwrite(files[best].data, bytes, 1, out);

#if 0
					for (j = 0; j < bytes; j++) {
						printf("%02x ", files[best].data[j]);
					}
					printf("\n");
#endif

					files[best].remaining = fread(files[best].data, bytes, 1, files[best].fp);
					if (files[best].remaining <= 0) {
						remaining--;
					}
				}

				fclose(out);
			}

			for (j = 0; j < n; j++) {
				free(files[j].data);
				fclose(files[j].fp);
			}
		}
	}

	return 0;
}
