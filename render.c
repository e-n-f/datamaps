#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string.h>
#include <stddef.h>
#include <math.h>
#include "util.h"
#include "graphics.h"

int dot_base = 13;
double dot_bright = 0.05917;
double dot_ramp = 1.23;

double line_per_dot = 6.64;
double line_ramp = 1;
double line_thick = 1;

int gps_base = 16;
double gps_dist = 1600; // about 50 feet
double gps_ramp = 1.5;

double display_gamma = .5;

int antialias = 1;
double mercator = -1;
int multiplier = 1;

void do_tile(struct graphics *gc, unsigned int z_draw, unsigned int x_draw, unsigned int y_draw, int bytes, int colors, char *fname, int mapbits, int metabits, int gps, int dump, int maxn, int pass);

int process(char *fname, int components, int z_lookup, unsigned char *startbuf, unsigned char *endbuf, int z_draw, int x_draw, int y_draw, struct graphics *gc, int mapbits, int metabits, int dump, int gps, int colors) {
	int bytes = bytesfor(mapbits, metabits, components, z_lookup);
	int ret = 0;

	char fn[strlen(fname) + 1 + 5 + 1 + 5 + 1];

	if (components == 1) {
		sprintf(fn, "%s/1,0", fname);
	} else {
		sprintf(fn, "%s/%d,%d", fname, components, z_lookup);
	}

	int fd = open(fn, O_RDONLY);
	if (fd < 0) {
		// perror(fn);
		return ret;
	}

	struct stat st;
	if (fstat(fd, &st) < 0) {
		perror("stat");
		exit(EXIT_FAILURE);
	}

	unsigned char *map = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
	if (map == MAP_FAILED) {
		perror("mmap");
		exit(EXIT_FAILURE);
	}

	gSortBytes = bytes;
	unsigned char *start = search(startbuf, map, st.st_size / bytes, bytes, bufcmp);
	unsigned char *end = search(endbuf, map, st.st_size / bytes, bytes, bufcmp);

	end += bytes; // points to the last value in range; need the one after that

	if (memcmp(start, startbuf, bytes) < 0) {
		start += bytes; // if not exact match, points to element before match
	}

	int step = 1;
	double brush = 1;
	double thick = line_thick;
	double bright1;
	if (components == 1) {
		bright1 = dot_bright;

		if (z_draw > dot_base) {
			step = 1;
			brush = 1 << (multiplier * (z_draw - dot_base));
		} else {
			step = 1 << (multiplier * (dot_base - z_draw));
		}

		bright1 *= exp(log(dot_ramp) * (z_draw - dot_base));
	} else {
		bright1 = dot_bright * line_per_dot / line_thick;

		if (line_ramp >= 1) {
			thick *= exp(log(line_ramp) * (z_draw - dot_base));
			bright1 *= exp(log(dot_ramp / line_ramp) * (z_draw - dot_base));
		} else {
			bright1 *= exp(log(dot_ramp) * (z_draw - dot_base));
		}
	}

	if (dump) {
		step = 1;
	} else {
		// Align to step size so each zoom is a superset of the previous
		start = (start - map) / (step * bytes) * (step * bytes) + map;
	}

	for (; start < end; start += step * bytes) {
		unsigned int x[components], y[components];
		double xd[components], yd[components];
		int k;
		unsigned int meta = 0;

		buf2xys(start, mapbits, metabits, z_lookup, components, x, y, &meta);

		if (!dump && z_draw >= mapbits / 2 - 8) {
			// Add noise below the bottom of the file resolution
			// so that it looks less gridded when overzoomed

			int j;
			for (j = 0; j < components; j++) {
				int noisebits = 32 - mapbits / 2;
				int i;

				for (i = 0; i < noisebits; i++) {
					x[j] |= ((y[j] >> (2 * noisebits - 1 - i)) & 1) << i;
					y[j] |= ((x[j] >> (2 * noisebits - 1 - i)) & 1) << i;
				}
			}
		}

		double hue = -1;
		if (metabits > 0 && colors > 0) {
			hue = (double) meta / colors;
		}

		double bright = bright1;
		if (mercator >= 0) {
			double lat, lon;
			tile2latlon(x[0], y[0], 32, &lat, &lon);
			double rat = cos(lat * M_PI / 180);
			double base = cos(mercator * M_PI / 180);
			bright /= rat * rat / (base * base);
		}

		for (k = 0; k < components; k++) {
			wxy2fxy(x[k], y[k], &xd[k], &yd[k], z_draw, x_draw, y_draw);
		}

		if (dump) {
			int should = 0;

			if (components == 1) {
				should = 1;
			} else {
				for (k = 1; k < components; k++) {
					if (drawClip(xd[k - 1], yd[k - 1], xd[k], yd[k], NULL, 0, 0, 0, 0)) {
						should = 1;
						break;
					}
				}
			}

			if (should) {
				for (k = 0; k < components; k++) {
					double lat, lon;
					tile2latlon(x[k], y[k], 32, &lat, &lon);

					printf("%lf,%lf ", lat, lon);
				}

				if (metabits != 0) {
					printf("%d:%d ", metabits, meta);
				}

				printf("// ");

				for (k = 0; k < components; k++) {
					printf("%08x %08x ", x[k], y[k]);
				}

				printf("\n");
			}
		} else if (components == 1) {
			if (!antialias) {
				xd[0] = (int) xd[0] + .5;
				yd[0] = (int) yd[0] + .5;
			}

			if (brush <= 1) {
				drawPixel(xd[0] - .5, yd[0] - .5, gc, bright * brush, hue);
			} else {
				drawBrush(xd[0], yd[0], gc, bright, brush, hue);
				ret = 1;
			}
		} else {
			for (k = 1; k < components; k++) {
				double bright1 = bright;

				long long xk1 = x[k - 1];
				long long xk = x[k];

				if (gps) {
					double xdist = (long long) x[k] - (long long) x[k - 1];
					double ydist = (long long) y[k] - (long long) y[k - 1];
					double dist = sqrt(xdist * xdist + ydist * ydist);

					double min = gps_dist;
					min = min * exp(log(gps_ramp) * (gps_base - z_draw));

					if (dist > min) {
						bright1 /= (dist / min);
					}

					if (bright1 < .0025) {
						continue;
					}
				}

				if (xk - xk1 >= (1LL << 31)) {
					wxy2fxy(xk - (1LL << 32), y[k], &xd[k], &yd[k], z_draw, x_draw, y_draw);
					drawClip(xd[k - 1], yd[k - 1], xd[k], yd[k], gc, bright1, hue, antialias, thick);

					wxy2fxy(x[k], y[k], &xd[k], &yd[k], z_draw, x_draw, y_draw);
					wxy2fxy(xk1 + (1LL << 32), y[k - 1], &xd[k - 1], &yd[k - 1], z_draw, x_draw, y_draw);
					drawClip(xd[k - 1], yd[k - 1], xd[k], yd[k], gc, bright1, hue, antialias, thick);

					wxy2fxy(x[k - 1], y[k - 1], &xd[k - 1], &yd[k - 1], z_draw, x_draw, y_draw);
				} else if (xk1 - xk >= (1LL << 31)) {
					wxy2fxy(xk1 - (1LL << 32), y[k - 1], &xd[k - 1], &yd[k - 1], z_draw, x_draw, y_draw);
					drawClip(xd[k - 1], yd[k - 1], xd[k], yd[k], gc, bright1, hue, antialias, thick);

					wxy2fxy(x[k - 1], y[k - 1], &xd[k - 1], &yd[k - 1], z_draw, x_draw, y_draw);
					wxy2fxy(xk + (1LL << 32), y[k], &xd[k], &yd[k], z_draw, x_draw, y_draw);
					drawClip(xd[k - 1], yd[k - 1], xd[k], yd[k], gc, bright1, hue, antialias, thick);

					wxy2fxy(x[k], y[k], &xd[k], &yd[k], z_draw, x_draw, y_draw);
				} else {
					drawClip(xd[k - 1], yd[k - 1], xd[k], yd[k], gc, bright1, hue, antialias, thick);
				}
			}
		}
	}

	munmap(map, st.st_size);
	close(fd);
	return ret;
}

void *fmalloc(size_t size) {
	void *p = malloc(size);
	if (p == NULL) {
		perror("malloc");
		exit(EXIT_FAILURE);
	}

	return p;
}

void usage(char **argv) {
	fprintf(stderr, "Usage: %s [-t transparency] [-dga] [-C colors] [-B zoom:level:ramp] [-G gamma] [-O offset] [-M latitude] [-l lineramp] file z x y\n", argv[0]);
	fprintf(stderr, "Usage: %s -A [-t transparency] [-dga] [-C colors] [-B zoom:level:ramp] [-G gamma] [-O offset] [-M latitude] [-l lineramp] file z minlat minlon maxlat maxlon\n", argv[0]);
	exit(EXIT_FAILURE);
}

int main(int argc, char **argv) {
	int i;
	extern int optind;
	extern char *optarg;

	int transparency = 255;
	int dump = 0;
	int gps = 0;
	int colors = 0;
	int assemble = 0;
	int invert = 0;
	int color = -1;
	int color2 = -1;
	int saturate = 1;
	int mask = 0;

	struct file {
		char *name;
		int mapbits;
		int metabits;
		int maxn;
		int bytes;
	};

	int nfiles = 0;
	struct file files[argc];

	while ((i = getopt(argc, argv, "t:dgC:B:G:O:M:a41Awc:l:L:smf:S:")) != -1) {
		switch (i) {
		case 't':
			transparency = atoi(optarg);
			break;

		case 'm':
			mask = 1;
			break;

		case 's':
			saturate = 0;
			break;

		case 'd':
			dump = 1;
			break;

		case 'g':
			gps = 1;
			break;

		case 'C':
			colors = atoi(optarg);
			break;

		case 'c':
			color = strtoul(optarg, NULL, 16);
			break;

		case 'S':
			color2 = strtoul(optarg, NULL, 16);
			break;

		case 'B':
			if (sscanf(optarg, "%d:%lf:%lf", &dot_base, &dot_bright, &dot_ramp) != 3) {
				usage(argv);
			}
			break;

		case 'O':
			if (sscanf(optarg, "%d:%lf:%lf", &gps_base, &gps_dist, &gps_ramp) != 3) {
				usage(argv);
			}
			break;

		case 'G':
			if (sscanf(optarg, "%lf", &display_gamma) != 1) {
				usage(argv);
			}
			break;

		case 'l':
			if (sscanf(optarg, "%lf", &line_ramp) != 1) {
				usage(argv);
			}
			break;

		case 'L':
			if (sscanf(optarg, "%lf", &line_thick) != 1) {
				usage(argv);
			}
			break;

		case 'a':
			antialias = 0;
			break;

		case 'M':
			if (sscanf(optarg, "%lf", &mercator) != 1) {
				usage(argv);
			}
			break;

		case '1':
			multiplier = 0; // log2(1)
			break;

		case '4':
			multiplier = 2; // log2(2)
			break;

		case 'A':
			assemble = 1;
			break;

		case 'w':
			invert = 1;
			break;

		case 'f':
			files[nfiles++].name = optarg;
			break;

		default:
			usage(argv);
		}
	}

	if (assemble) {
		if (argc - optind != 6) {
			usage(argv);
		}
	} else {
		if (argc - optind != 4) {
			usage(argv);
		}
	}

	files[nfiles++].name = argv[optind];
	unsigned int z_draw = atoi(argv[optind + 1]);

	for (i = 0; i < nfiles; i++) {
		char meta[strlen(files[i].name) + 1 + 4 + 1];
		sprintf(meta, "%s/meta", files[i].name);
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
		if (fgets(s, 2000, f) == NULL || sscanf(s, "%d %d %d", &files[i].mapbits, &files[i].metabits, &files[i].maxn) != 3) {
			fprintf(stderr, "%s: couldn't find size declaration", meta);
			exit(EXIT_FAILURE);
		}
		fclose(f);

		files[i].bytes = (files[i].mapbits + files[i].metabits + 7) / 8;
	}

	struct graphics *gc = graphics_init(256, 256);

	if (assemble) {
#if 0
		unsigned x1, y1, x2, y2;

		latlon2tile(atof(argv[optind + 2]), atof(argv[optind + 3]), z_draw, &x1, &y1);
		latlon2tile(atof(argv[optind + 4]), atof(argv[optind + 5]), z_draw, &x2, &y2);

		if (x1 > x2) {
			unsigned t = x2;
			x2 = x1;
			x1 = t;
		}

		if (y1 > y2) {
			unsigned t = y2;
			y2 = y1;
			y1 = t;
		}

		fprintf(stderr, "making zoom %u: %u/%u to %u/%u\n", z_draw, x1, y1, x2, y2);
		fprintf(stderr, "that's %d by %d\n", 256 * (x2 - x1 + 1), 256 * (y2 - y1 + 1));

		int stride = (x2 - x1 + 1) * 256;
		double *image2 = NULL, *cx2 = NULL, *cy2 = NULL;

		if (!dump) {
			if (stride * (y2 - y1 + 1) * 256 > 10000 * 10000) {
				fprintf(stderr, "Image too big\n");
				exit(EXIT_FAILURE);
			}

			image2 = fmalloc((y2 - y1 + 1) * 256 * stride * sizeof(double));
			cx2 = fmalloc((y2 - y1 + 1) * 256 * stride * sizeof(double));
			cy2 = fmalloc((y2 - y1 + 1) * 256 * stride * sizeof(double));
		}

		unsigned int x, y;
		for (x = x1; x <= x2; x++) {
			for (y = y1; y <= y2; y++) {
				fprintf(stderr, "%u/%u/%u\r", z_draw, x, y);
				memset(image, 0, 256 * 256 * sizeof(double));

				for (i = 0; i < nfiles; i++) {
					do_tile(image, cx, cy, z_draw, x, y, files[i].bytes, colors, files[i].name, files[i].mapbits, files[i].metabits, gps, dump, files[i].maxn, i);
				}

				if (!dump) {
					int xx, yy;
					for (xx = 0; xx < 256; xx++) {
						for (yy = 0; yy < 256; yy++) {
							image2[stride * (256 * (y - y1) + yy) + (256 * (x - x1) + xx)] =
								image[256 * yy + xx];
							cx2[stride * (256 * (y - y1) + yy) + (256 * (x - x1) + xx)] =
								cx[256 * yy + xx];
							cy2[stride * (256 * (y - y1) + yy) + (256 * (x - x1) + xx)] =
								cy[256 * yy + xx];
						}
					}
				}
			}
		}

		if (!dump) {
			fprintf(stderr, "output: %d by %d\n", 256 * (x2 - x1 + 1), 256 * (y2 - y1 + 1));
			out(image2, cx2, cy2, 256 * (x2 - x1 + 1), 256 * (y2 - y1 + 1), transparency, display_gamma, invert, color, color2, saturate, mask);
		}
#endif
	} else {
		unsigned int x_draw = atoi(argv[optind + 2]);
		unsigned int y_draw = atoi(argv[optind + 3]);

		for (i = 0; i < nfiles; i++) {
			do_tile(gc, z_draw, x_draw, y_draw, files[i].bytes, colors, files[i].name, files[i].mapbits, files[i].metabits, gps, dump, files[i].maxn, i);
		}

		if (!dump) {
			out(gc, transparency, display_gamma, invert, color, color2, saturate, mask);
		}
	}

	return 0;
}

void do_tile(struct graphics *gc, unsigned int z_draw, unsigned int x_draw, unsigned int y_draw,
		int bytes, int colors, char *fname, int mapbits, int metabits, int gps, int dump, int maxn, int pass) {
	int i;

	// Do the single-point case

	unsigned char startbuf[bytes];
	unsigned char endbuf[bytes];
	zxy2bufs(z_draw, x_draw, y_draw, startbuf, endbuf, bytes);
	int further = process(fname, 1, z_draw, startbuf, endbuf, z_draw, x_draw, y_draw, gc, mapbits, metabits, dump, gps, colors);

	// When overzoomed, also look up the adjacent tile
	// to keep from drawing partial circles.

	if (further && !dump) {
		if (x_draw > 0) {
			zxy2bufs(z_draw, x_draw - 1, y_draw, startbuf, endbuf, bytes);
			process(fname, 1, z_draw, startbuf, endbuf, z_draw, x_draw, y_draw, gc, mapbits, metabits, dump, gps, colors);
		}

		if (y_draw > 0) {
			zxy2bufs(z_draw, x_draw, y_draw - 1, startbuf, endbuf, bytes);
			process(fname, 1, z_draw, startbuf, endbuf, z_draw, x_draw, y_draw, gc, mapbits, metabits, dump, gps, colors);

			if (x_draw > 0) {
				zxy2bufs(z_draw, x_draw - 1, y_draw - 1, startbuf, endbuf, bytes);
				process(fname, 1, z_draw, startbuf, endbuf, z_draw, x_draw, y_draw, gc, mapbits, metabits, dump, gps, colors);
			}
		}
	}

	// Do the zoom levels numbered greater than this one.
	//
	// For zoom levels greater than this one, we look up the entire area
	// of the tile we are drawing, which will end up being multiple tiles
	// of the higher zoom.

	int z_lookup;
	for (z_lookup = z_draw + 1; (dump || z_lookup < z_draw + 9) && z_lookup <= mapbits / 2; z_lookup++) {
		for (i = 2; i <= maxn; i++) {
			int bytes = bytesfor(mapbits, metabits, i, z_lookup);

			unsigned char startbuf[bytes];
			unsigned char endbuf[bytes];
			zxy2bufs(z_draw, x_draw, y_draw, startbuf, endbuf, bytes);
			process(fname, i, z_lookup, startbuf, endbuf, z_draw, x_draw, y_draw, gc, mapbits, metabits, dump, gps, colors);
		}
	}

	// For zoom levels numbered less than this one, each stage looks up a
	// larger area for potential overlaps.

	int x_lookup, y_lookup;
	for (z_lookup = z_draw, x_lookup = x_draw, y_lookup = y_draw;
	     z_lookup >= 0;
	     z_lookup--, x_lookup /= 2, y_lookup /= 2) {
		for (i = 2; i <= maxn; i++) {
			int bytes = bytesfor(mapbits, metabits, i, z_lookup);

			unsigned char startbuf[bytes];
			unsigned char endbuf[bytes];
			zxy2bufs(z_lookup, x_lookup, y_lookup, startbuf, endbuf, bytes);
			process(fname, i, z_lookup, startbuf, endbuf, z_draw, x_draw, y_draw, gc, mapbits, metabits, dump, gps, colors);
		}
	}

}
