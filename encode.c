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

int mapbits = 2 * (16 + 8); // zoom level 16
int metabits = 0;

#define MAX_INPUT 2000

struct file {
	int legs;
	int level;
	FILE *f;

	struct file *next;
};

// http://wiki.openstreetmap.org/wiki/Slippy_map_tilenames
void latlon2tile(double lat, double lon, int zoom, unsigned int *x, unsigned int *y) {
	double lat_rad = lat * M_PI / 180;
	unsigned long long n = 1LL << zoom;

	*x = n * ((lon + 180) / 360);
	*y = n * (1 - (log(tan(lat_rad) + 1/cos(lat_rad)) / M_PI)) / 2;
}

void xy2buf(unsigned int x32, unsigned int y32, unsigned char *buf, int *offbits, int n, int skip) {
	int i;

	for (i = 31 - skip; i > 31 - n / 2; i--) {
		// Bits come from x32 and y32 high-bit first

		int xb = (x32 >> i) & 1;
		int yb = (y32 >> i) & 1;

		// And go into the buffer high-bit first

		buf[*offbits / 8] |= yb << (7 - (*offbits % 8));
		(*offbits)++;
		buf[*offbits / 8] |= xb << (7 - (*offbits % 8));
		(*offbits)++;
	}
}

void meta2buf(int bits, long long data, unsigned char *buf, int *offbits, int max) {
	int i;

	for (i = bits - 1; i >= 0 && *offbits < max; i--) {
		int b = (data >> i) & 1;
		buf[*offbits / 8] |= b << (7 - (*offbits % 8));
		(*offbits)++;
	}
}

void buf2xys(unsigned char *buf, int mapbits, int skip, int n, unsigned int *x, unsigned int *y) {
	int i, j;
	int offbits = 0;

	for (i = 0; i < n; i++) {
		x[i] = 0;
		y[i] = 0;
	}

	// First pull off the common bits

	for (i = 31; i > 31 - skip; i--) {
		int y0 = (buf[offbits / 8] >> (7 - offbits % 8)) & 1;
		offbits++;
		int x0 = (buf[offbits / 8] >> (7 - offbits % 8)) & 1;
		offbits++;

		for (j = 0; j < n; j++) {
			x[j] |= x0 << i;
			y[j] |= y0 << i;
		}
	}

	// and then the remainder for each component

	for (j = 0; j < n; j++) {
		for (i = 31 - skip; i > 31 - mapbits / 2; i--) {
			int y0 = (buf[offbits / 8] >> (7 - offbits % 8)) & 1;
			offbits++;
			int x0 = (buf[offbits / 8] >> (7 - offbits % 8)) & 1;
			offbits++;

			x[j] |= x0 << i;
			y[j] |= y0 << i;
		}
	}
}

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

		int bits = mapbits + metabits;
		for (i = 1; i < n; i++) {
			bits += mapbits - 2 * common;
		}

		int bytes = (bits + 7) / 8;
		unsigned char buf[bytes];
		memset(buf, 0, bytes);

		int off = 0;
		xy2buf(x[0], y[0], buf, &off, mapbits, 0);
		for (i = 1; i < n; i++) {
			xy2buf(x[i], y[i], buf, &off, mapbits, common);
		}

		for (i = 0; i < m; i++) {
			meta2buf(metasize[i], meta[i], buf, &off, bits);
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

static int gSortBytes;
int bufcmp(const void *v1, const void *v2) {
	return memcmp(v1, v2, gSortBytes);
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

		int fd = open(fn, O_RDONLY);
		if (fd < 0) {
			perror(fn);
			exit(EXIT_FAILURE);
		}

		struct stat st;
		if (fstat(fd, &st) < 0) {
			perror("stat");
			exit(EXIT_FAILURE);
		}

		void *map = mmap(NULL, st.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
		if (map == MAP_FAILED) {
			perror("mmap");
			exit(EXIT_FAILURE);
		}

		int bits = mapbits + metabits;
		for (i = 1; i < files->legs; i++) {
			bits += mapbits - 2 * files->level;
		}
		int bytes = (bits + 7) / 8;
		gSortBytes = bytes;

		fprintf(stderr,
		 	"Sorting %lld shapes of %d point(s), zoom level %d\n",
			(long long) st.st_size / bytes,
			files->legs, files->level);

		qsort(map, st.st_size / bytes, bytes, bufcmp);

		if (unlink(fn) != 0) {
			perror("unlink");
			exit(EXIT_FAILURE);
		}

		int out = open(fn, O_RDWR | O_CREAT, 0666);
		if (out < 0) {
			perror(fn);
			exit(EXIT_FAILURE);
		}

		size_t off = 0;
		while (off < st.st_size) {
			ssize_t written = write(out, map + off, st.st_size - off);

			if (written < 0) {
				perror("write");
				exit(1);
			}

			off += written;
		}

		munmap(map, st.st_size);
		close(fd);
		close(out);
	}

	return 0;
}
