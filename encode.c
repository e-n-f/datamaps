#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <string.h>
#include <fcntl.h>
#include "util.h"

int mapbits = 2 * (16 + 8); // zoom level 16
int metabits = 0;

#define MAX_INPUT 2000

struct file {
	int legs;
	int level;
	FILE *f;

	struct file *next;
};

void usage(char *name) {
	fprintf(stderr, "Usage: %s [-z zoom] [-m metadata-bits] -o destdir [file ...]\n",
		name);
}

void read_file(FILE *f, char *destdir, struct file **files, int *maxn) {
	char s[MAX_INPUT];
	double lat[MAX_INPUT], lon[MAX_INPUT];
	int metasize[MAX_INPUT];
	long long meta[MAX_INPUT];
	unsigned int x[MAX_INPUT], y[MAX_INPUT];
	unsigned long long seq = 0;

	while (fgets(s, MAX_INPUT, f)) {
		char *cp = s;
		int n = 0, m = 0;

		if (seq % 100000 == 0) {
			fprintf(stderr, "Read %lld records\r", seq);
		}
		seq++;

		while (1) {
			if (sscanf(cp, "%lf,%lf", &lat[n], &lon[n]) == 2) {
				n++;
				while (*cp != '\0' && *cp != ' ') {
					cp++;
				}
				while (*cp == ' ') {
					cp++;
				}
			} else if (sscanf(cp, "%d:%lld", &metasize[m], &meta[m]) == 2) {
				m++;
				while (*cp != '\0' && *cp != ' ') {
					cp++;
				}
				while (*cp == ' ') {
					cp++;
				}
			} else {
				break;
			}
		}

		if (n == 0) {
			fprintf(stderr, "No valid points in %s", s);
			continue;
		}

		// Project each point to web mercator

		int i;
		for (i = 0; i < n; i++) {
			latlon2tile(lat[i], lon[i], 32, &x[i], &y[i]);
		}

		// If this is a polyline, find out how many leading bits in common
		// all the points have.

		int common = 0;
		if (n > 1) {
			int ok = 1;
			for (common = 0; ok && common < mapbits / 2; common++) {
				int x0 = x[0] & (1 << (31 - common));
				int y0 = y[0] & (1 << (31 - common));

				for (i = 1; i < n; i++) {
					if ((x[i] & (1 << (31 - common))) != x0 ||
					    (y[i] & (1 << (31 - common))) != y0) {
						ok = 0;
						break;
					}
				}

				if (!ok) {
					break;
				}
			}
		}

		if (n > *maxn) {
			*maxn = n;
		}

		int bytes = bytesfor(mapbits, metabits, n, common);
		unsigned char buf[bytes];
		memset(buf, 0, bytes);

		int off = 0;
		xy2buf(x[0], y[0], buf, &off, mapbits, 0);
		for (i = 1; i < n; i++) {
			xy2buf(x[i], y[i], buf, &off, mapbits, common);
		}

		for (i = 0; i < m; i++) {
			meta2buf(metasize[i], meta[i], buf, &off, bytes * 8);
		}

		struct file **fo;

		for (fo = files; *fo != NULL; fo = &((*fo)->next)) {
			if ((*fo)->legs == n && (*fo)->level == common) {
				break;
			}
		}

		if (*fo == NULL) {
			*fo = malloc(sizeof(struct file));

			if (*fo == NULL) {
				perror("malloc");
				exit(EXIT_FAILURE);
			}

			char fn[strlen(destdir) + 10 + 1 + 10 + 1];
			sprintf(fn, "%s/%d,%d", destdir, n, common);

			(*fo)->legs = n;
			(*fo)->level = common;
			(*fo)->f = fopen(fn, "w");

			if ((*fo)->f == NULL) {
				perror(fn);
				exit(EXIT_FAILURE);
			}
		}

		fwrite(buf, sizeof(char), bytes, (*fo)->f);
	}
}

struct merge {
	long long start;
	long long end;
	unsigned char *map;
	int bytes;
};

int mergecmp(const void *v1, const void *v2) {
	const struct merge *m1 = v1;
	const struct merge *m2 = v2;

	return memcmp(m1->map + m1->start, m2->map + m2->start, m1->bytes);
}

void merge(struct merge *merges, int nmerges, unsigned char *map, FILE *f) {
	while (1) {
		qsort(merges, nmerges, sizeof(struct merge *), mergecmp);
	}
}

int main(int argc, char **argv) {
	int i;
	extern int optind;
	extern char *optarg;
	char *destdir = NULL;

	while ((i = getopt(argc, argv, "z:m:o:")) != -1) {
		switch (i) {
		case 'z':
			mapbits = 2 * (atoi(optarg) + 8);
			break;

		case 'm':
			metabits = atoi(optarg);
			break;

		case 'o':
			destdir = optarg;
			break;

		default:
			usage(argv[0]);
			exit(EXIT_FAILURE);
		}
	}

	if (mapbits <= 8) {
		fprintf(stderr, "%s: Zoom level (-z) must be > 0\n", argv[0]);
		usage(argv[0]);
		exit(EXIT_FAILURE);
	}

	if (destdir == NULL) {
		fprintf(stderr, "%s: Must specify a directory with -o\n", argv[0]);
		usage(argv[0]);
		exit(EXIT_FAILURE);
	}

	if (mkdir(destdir, 0777) != 0) {
		perror(destdir);
		exit(EXIT_FAILURE);
	}

	struct file *files = NULL;
	int maxn = 0;

	if (optind == argc) {
		read_file(stdin, destdir, &files, &maxn);
	} else {
		for (i = optind; i < argc; i++) {
			FILE *f = fopen(argv[i], "r");
			if (f == NULL) {
				perror(argv[i]);
				exit(EXIT_FAILURE);
			}

			read_file(f, destdir, &files, &maxn);
			fclose(f);
		}
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

	for (; files != NULL; files = files->next) {
		fclose(files->f);

		char fn[strlen(destdir) + 10 + 1 + 10 + 1];
		sprintf(fn, "%s/%d,%d", destdir, files->legs, files->level);

		int fd = open(fn, O_RDWR);
		if (fd < 0) {
			perror(fn);
			exit(EXIT_FAILURE);
		}

		struct stat st;
		if (fstat(fd, &st) < 0) {
			perror("stat");
			exit(EXIT_FAILURE);
		}

		int bytes = bytesfor(mapbits, metabits, files->legs, files->level);
		gSortBytes = bytes;

		fprintf(stderr,
		 	"Sorting %lld shapes of %d point(s), zoom level %d\n",
			(long long) st.st_size / bytes,
			files->legs, files->level);

		int page = getpagesize();
		long long unit = (50 * 1024 * 1024 / bytes) * bytes;
		while (unit % page != 0) {
			unit += bytes;
		}

		int nmerges = (st.st_size + unit - 1) / unit;
		struct merge merges[nmerges];

		long long start;
		for (start = 0; start < st.st_size; start += unit) {
			long long end = start + unit;
			if (end > st.st_size) {
				end = st.st_size;
			}

			fprintf(stderr, "part %lld of %lld (start %lld end %lld)\n", start / unit + 1, nmerges, start, end);

			merges[start / unit].start = start;
			merges[start / unit].end = end;

			void *map = mmap(NULL, end - start, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, start);
			if (map == MAP_FAILED) {
				perror("mmap");
				exit(EXIT_FAILURE);
			}

			qsort(map, (end - start) / bytes, bytes, bufcmp);

			// Sorting and then copying avoids the need to
			// write out intermediate stages of the sort.

			void *map2 = mmap(NULL, end - start, PROT_READ | PROT_WRITE, MAP_SHARED, fd, start);
			if (map == MAP_FAILED) {
				perror("mmap (write)");
				exit(EXIT_FAILURE);
			}

			memcpy(map2, map, end - start);

			munmap(map, end - start);
			munmap(map2, end - start);
		}

		void *map = mmap(NULL, st.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
		if (map == MAP_FAILED) {
			perror("mmap");
			exit(EXIT_FAILURE);
		}

		if (unlink(fn) != 0) {
			perror("unlink");
			exit(EXIT_FAILURE);
		}

		FILE *f = fopen(fn, "w");
		if (f == NULL) {
			perror(fn);
			exit(EXIT_FAILURE);
		}

		for (i = 0; i < nmerges; i++) {
			merges[i].bytes = bytes;
			merges[i].map = map;
		}

		merge(merges, nmerges, map, f);

		munmap(map, st.st_size);
		fclose(f);
		close(fd);
	}

	return 0;
}
