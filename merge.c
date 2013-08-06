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
	fprintf(stderr, "Usage: %s [-o outfile] [-d] file ...\n", argv[0]);
	exit(EXIT_FAILURE);
}

int main(int argc, char **argv) {
	int i;
	extern int optind;
	extern char *optarg;

	char *destdir = NULL;
	int dedup = 0;

	while ((i = getopt(argc, argv, "o:d")) != -1) {
		switch (i) {
		case 'o':
			destdir = optarg;
			break;

		case 'd':
			dedup = 1;
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

	return 0;
}
