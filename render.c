#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string.h>
#include <stddef.h>
#include <math.h>
#include <limits.h>
#include "util.h"
#include "graphics.h"
#include "clip.h"
#include "dump.h"

int dot_base = 13;
double dot_bright = 0.05917;
double dot_ramp = 1.23;

double point_size = 1;

double line_per_dot = 6.64;
double line_ramp = 1;
double line_thick = 1;

int gps_base = 16;
double gps_dist = 1600; // about 50 feet
double gps_ramp = 1.5;

double display_gamma = .5;

int antialias = 1;
double mercator = -1;
double exponent = 2;

int tilesize = 256;

float circle = -1;

struct color_range {
	long long meta1;
	double hue1;

	long long meta2;
	double hue2;

	int active;
};

void do_tile(struct graphics *gc, unsigned int z_draw, unsigned int x_draw, unsigned int y_draw, int bytes, struct color_range *colors, char *fname, int mapbits, int metabits, int gps, int dump, int maxn, int pass, int xoff, int yoff);

static double cloudsize(int z_draw, int x_draw, int y_draw) {
	double lat, lon;
	tile2latlon((x_draw + .5) * (1LL << (32 - z_draw)),
		    (y_draw + .5) * (1LL << (32 - z_draw)),
		    32, &lat, &lon);
	double rat = cos(lat * M_PI / 180);

	double size = circle * .00000274;  // in degrees
	size /= rat;                       // adjust for latitude
	size /= 360.0 / (1 << z_draw);     // convert to tiles

	return size;
}

int process(char *fname, int components, int z_lookup, unsigned char *startbuf, unsigned char *endbuf, int z_draw, int x_draw, int y_draw, struct graphics *gc, int mapbits, int metabits, int dump, int gps, struct color_range *colors, int xoff, int yoff) {
	int bytes = bytesfor(mapbits, metabits, components, z_lookup);
	int ret = 0;

	char fn[strlen(fname) + 1 + 5 + 1 + 5 + 1];

	struct tilecontext tc;
	tc.z = z_draw;
	tc.x = x_draw;
	tc.y = y_draw;
	tc.xoff = xoff;
	tc.yoff = yoff;

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
			brush = exp(log(2.0) * (z_draw - dot_base));
			bright1 *= exp(log(dot_ramp) * (z_draw - dot_base));
		} else {
			step = exp(log(exponent) * (dot_base - z_draw));
			bright1 *= exp(log(dot_ramp * 2.0 / exponent) * (z_draw - dot_base));
		}

		bright1 /= point_size;
		brush *= point_size;
	} else {
		bright1 = dot_bright * line_per_dot / line_thick;

		if (line_ramp >= 1) {
			thick *= exp(log(line_ramp) * (z_draw - dot_base));
			bright1 *= exp(log(dot_ramp / line_ramp) * (z_draw - dot_base));
		} else {
			bright1 *= exp(log(dot_ramp) * (z_draw - dot_base));
		}
	}

	if (mercator >= 0) {
		double lat, lon;
		tile2latlon((x_draw + .5) * (1LL << (32 - z_draw)),
			    (y_draw + .5) * (1LL << (32 - z_draw)),
			    32, &lat, &lon);
		double rat = cos(lat * M_PI / 180);

		double base = cos(mercator * M_PI / 180);
		brush /= rat * rat / (base * base);
	}

	if (dump) {
		step = 1;
	} else {
		// Align to step size so each zoom is a superset of the previous
		start = (start - map + (step * bytes - 1)) / (step * bytes) * (step * bytes) + map;
	}

	double size = cloudsize(z_draw, x_draw, y_draw);
	int innerstep = 1;
	long long todo = 0;

	size *= tilesize;                  // convert to pixels

	if (circle > 0) {
		// An additional 4 zoom levels without skipping
		// XXX Why 4?
		if (step > 1 && size > .0625) {
			innerstep = step;
			step = 1;
		}
	}

	const double b = brush * (tilesize / 256.0) * (tilesize / 256.0);
	double radius;

	radius = sqrt(b / M_PI);

	for (; start < end; start += step * bytes) {
		unsigned int x[components], y[components];
		double xd[components], yd[components];
		int k;
		unsigned long long meta = 0;

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
		if (metabits > 0 && colors->active) {
			hue = (((double) meta - colors->meta1) / (colors->meta2 - colors->meta1) * (colors->hue2 - colors->hue1) + colors->hue1) / 360;
			while (hue < 0) {
				hue++;
			}
			while (hue > 1) {
				hue--;
			}
		}

		double bright = bright1;

		for (k = 0; k < components; k++) {
			wxy2fxy(x[k], y[k], &xd[k], &yd[k], z_draw, x_draw, y_draw);
		}

		if (dump) {
			int should = 0;

			if (components == 1) {
				should = 1;
			} else {
				for (k = 1; k < components; k++) {
					double x1 = xd[k - 1];
					double y1 = yd[k - 1];
					double x2 = xd[k];
					double y2 = yd[k];

					if (clip(&x1, &y1, &x2, &y2, 0, 0, 1, 1)) {
						should = 1;
						break;
					}
				}
			}

			if (should) {
				dump_out(dump, x, y, components, metabits, meta);
			}
		} else if (components == 1) {
			if (!antialias) {
				xd[0] = ((int) (xd[0] * tilesize) + .5) / tilesize;
				yd[0] = ((int) (yd[0] * tilesize) + .5) / tilesize;
			}

			if (circle > 0) {
				if (size < .5) {
					if (b <= 1) {
						drawPixel((xd[0] * tilesize - .5) + xoff, (yd[0] * tilesize - .5) + yoff, gc, bright * b * meta / innerstep, hue, &tc);
					} else {
						drawBrush((xd[0] * tilesize) + xoff - radius, (yd[0] * tilesize) + yoff - radius, gc, bright * meta / innerstep, b, hue, &tc);
						ret = 1;
					}
				} else {
					double xc = (xd[0] * tilesize) + xoff;
					double yc = (yd[0] * tilesize) + yoff;

					if (xc + size >= 0 &&
					    yc + size >= 0 &&
					    xc - size <= tilesize &&
					    yc - size <= tilesize) {
						srand(x[0] * 37 + y[0]);

						for (todo += meta; todo > 0; todo -= innerstep) {
							double r = sqrt(((double) (rand() & (INT_MAX - 1))) / (INT_MAX));
							double ang = ((double) (rand() & (INT_MAX - 1))) / (INT_MAX) * 2 * M_PI;

							double xp = xc + size * r * cos(ang);
							double yp = yc + size * r * sin(ang);

							if (b <= 1) {
								drawPixel(xp - .5, yp - .5, gc, bright * b, hue, &tc);
							} else {
								drawBrush(xp - radius, yp - radius, gc, bright, b, hue, &tc);
								ret = 1;
							}
						}
					}
				}
			} else {
				if (b <= 1) {
					drawPixel((xd[0] * tilesize - .5) + xoff, (yd[0] * tilesize - .5) + yoff, gc, bright * b, hue, &tc);
				} else {
					drawBrush((xd[0] * tilesize) + xoff - radius, (yd[0] * tilesize) + yoff - radius, gc, bright, b, hue, &tc);
					ret = 1;
				}
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

				double thick1 = thick * tilesize / 256.0;

				if (xk - xk1 >= (1LL << 31)) {
					wxy2fxy(xk - (1LL << 32), y[k], &xd[k], &yd[k], z_draw, x_draw, y_draw);
					drawClip(xd[k - 1] * tilesize + xoff, yd[k - 1] * tilesize + yoff, xd[k] * tilesize + xoff, yd[k] * tilesize + yoff, gc, bright1, hue, antialias, thick1, &tc);

					wxy2fxy(x[k], y[k], &xd[k], &yd[k], z_draw, x_draw, y_draw);
					wxy2fxy(xk1 + (1LL << 32), y[k - 1], &xd[k - 1], &yd[k - 1], z_draw, x_draw, y_draw);
					drawClip(xd[k - 1] * tilesize + xoff, yd[k - 1] * tilesize + yoff, xd[k] * tilesize + xoff, yd[k] * tilesize + yoff, gc, bright1, hue, antialias, thick1, &tc);

					wxy2fxy(x[k - 1], y[k - 1], &xd[k - 1], &yd[k - 1], z_draw, x_draw, y_draw);
				} else if (xk1 - xk >= (1LL << 31)) {
					wxy2fxy(xk1 - (1LL << 32), y[k - 1], &xd[k - 1], &yd[k - 1], z_draw, x_draw, y_draw);
					drawClip(xd[k - 1] * tilesize + xoff, yd[k - 1] * tilesize + yoff, xd[k] * tilesize + xoff, yd[k] * tilesize + yoff, gc, bright1, hue, antialias, thick1, &tc);

					wxy2fxy(x[k - 1], y[k - 1], &xd[k - 1], &yd[k - 1], z_draw, x_draw, y_draw);
					wxy2fxy(xk + (1LL << 32), y[k], &xd[k], &yd[k], z_draw, x_draw, y_draw);
					drawClip(xd[k - 1] * tilesize + xoff, yd[k - 1] * tilesize + yoff, xd[k] * tilesize + xoff, yd[k] * tilesize + yoff, gc, bright1, hue, antialias, thick1, &tc);

					wxy2fxy(x[k], y[k], &xd[k], &yd[k], z_draw, x_draw, y_draw);
				} else {
					drawClip(xd[k - 1] * tilesize + xoff, yd[k - 1] * tilesize + yoff, xd[k] * tilesize + xoff, yd[k] * tilesize + yoff, gc, bright1, hue, antialias, thick1, &tc);
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

void prep(char *outdir, int z, int x, int y) {
	if (outdir == NULL) {
		return;
	}

	char path[strlen(outdir) + 12 + 12 + 12 + 5];

	sprintf(path, "%s", outdir);
	mkdir(path, 0777);

	sprintf(path, "%s/%d", outdir, z);
	mkdir(path, 0777);

	sprintf(path, "%s/%d/%d", outdir, z, x);
	mkdir(path, 0777);

	sprintf(path, "%s/%d/%d/%d.png", outdir, z, x, y);
	if (freopen(path, "wb", stdout) == NULL) {
		perror(path);
		exit(EXIT_FAILURE);
	}
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
	struct color_range colors;
	int assemble = 0;
	int invert = 0;
	int color = -1;
	int color2 = -1;
	int saturate = 1;
	int mask = 0;
	char *outdir = NULL;

	colors.active = 0;

	struct file {
		char *name;
		int mapbits;
		int metabits;
		int maxn;
		int bytes;
	};

	int nfiles = 0;
	struct file files[argc];

	while ((i = getopt(argc, argv, "t:dDgC:B:G:O:M:aAwc:l:L:smf:S:T:o:x:e:p:")) != -1) {
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

		case 'D':
			dump = 2;
			break;

		case 'g':
			gps = 1;
			break;

		case 'C':
			if (sscanf(optarg, "%lld:%lf:%lld:%lf",
				&colors.meta1, &colors.hue1,
				&colors.meta2, &colors.hue2) == 4) {
				colors.active = 1;
			} else if (sscanf(optarg, "%lld", &colors.meta2) == 1) {
				colors.meta1 = 0;
				colors.hue1 = 0;
				colors.hue2 = 360;
				colors.active = 1;
			} else {
				fprintf(stderr, "Can't understand -%c %s\n", i, optarg);
				usage(argv);
			}

			break;

		case 'c':
			color = strtoul(optarg, NULL, 16);
			break;

		case 'S':
			color2 = strtoul(optarg, NULL, 16);
			break;

		case 'B':
			if (sscanf(optarg, "%d:%lf:%lf", &dot_base, &dot_bright, &dot_ramp) != 3) {
				fprintf(stderr, "Can't understand -B %s\n", optarg);
				usage(argv);
			}
			break;

		case 'O':
			if (sscanf(optarg, "%d:%lf:%lf", &gps_base, &gps_dist, &gps_ramp) != 3) {
				fprintf(stderr, "Can't understand -O %s\n", optarg);
				usage(argv);
			}
			break;

		case 'G':
			if (sscanf(optarg, "%lf", &display_gamma) != 1) {
				fprintf(stderr, "Can't understand -G %s\n", optarg);
				usage(argv);
			}
			break;

		case 'l':
			if (sscanf(optarg, "%lf", &line_ramp) != 1) {
				fprintf(stderr, "Can't understand -l %s\n", optarg);
				usage(argv);
			}
			break;

		case 'L':
			if (sscanf(optarg, "%lf", &line_thick) != 1) {
				fprintf(stderr, "Can't understand -L %s\n", optarg);
				usage(argv);
			}
			break;

		case 'a':
			antialias = 0;
			break;

		case 'M':
			if (sscanf(optarg, "%lf", &mercator) != 1) {
				fprintf(stderr, "Can't understand -M %s\n", optarg);
				usage(argv);
			}
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

		case 'T':
			tilesize = atoi(optarg);
			break;

		case 'o':
			outdir = optarg;
			break;

		case 'x':
			{
				char unit;

				if (sscanf(optarg, "c%f%c", &circle, &unit) != 2) {
					fprintf(stderr, "Can't understand -x %s\n", optarg);
					usage(argv);
				} else {
					if (unit == 'm') {
						circle *= 3.28; // meters to feet
					} else if (unit == 'f') {
						;
					} else {
						fprintf(stderr, "Can't understand unit in -x %s\n", optarg);
						usage(argv);
					}
				}
			}
			break;

		case 'e':
			if (sscanf(optarg, "%lf", &exponent) != 1) {
				fprintf(stderr, "Can't understand -%c %s\n", i, optarg);
				usage(argv);
			}
			break;

		case 'p':
			if (sscanf(optarg, "%lf", &point_size) != 1) {
				fprintf(stderr, "Can't understand -%c %s\n", i, optarg);
				usage(argv);
			}
			break;

		default:
			fprintf(stderr, "Unknown option %c\n", i);
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

	if (dump) {
		dump_begin(dump);
	}

	if (assemble) {
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
		fprintf(stderr, "that's %d by %d\n", tilesize * (x2 - x1 + 1), tilesize * (y2 - y1 + 1));

		int stride = (x2 - x1 + 1) * tilesize;
		struct graphics *gc = NULL;

		if (!dump) {
			if (stride * (y2 - y1 + 1) * tilesize > 10000 * 10000) {
				fprintf(stderr, "Image too big\n");
				exit(EXIT_FAILURE);
			}

			gc = graphics_init((x2 - x1 + 1) * tilesize, (y2 - y1 + 1) * tilesize);
		}

		unsigned int x, y;
		for (x = x1; x <= x2; x++) {
			for (y = y1; y <= y2; y++) {
				fprintf(stderr, "%u/%u/%u\r", z_draw, x, y);

				for (i = 0; i < nfiles; i++) {
					do_tile(gc, z_draw, x, y, files[i].bytes, &colors, files[i].name, files[i].mapbits, files[i].metabits, gps, dump, files[i].maxn, i, (x - x1) * tilesize, (y - y1) * tilesize);
				}
			}
		}

		if (!dump) {
			fprintf(stderr, "output: %d by %d\n", tilesize * (x2 - x1 + 1), tilesize * (y2 - y1 + 1));
			prep(outdir, z_draw, x1, y1);
			out(gc, transparency, display_gamma, invert, color, color2, saturate, mask);
		}
	} else {
		struct graphics *gc = graphics_init(tilesize, tilesize);

		unsigned int x_draw = atoi(argv[optind + 2]);
		unsigned int y_draw = atoi(argv[optind + 3]);

		for (i = 0; i < nfiles; i++) {
			do_tile(gc, z_draw, x_draw, y_draw, files[i].bytes, &colors, files[i].name, files[i].mapbits, files[i].metabits, gps, dump, files[i].maxn, i, 0, 0);
		}

		if (!dump) {
			prep(outdir, z_draw, x_draw, y_draw);
			out(gc, transparency, display_gamma, invert, color, color2, saturate, mask);
		}
	}

	if (dump) {
		dump_end(dump);
	}

	return 0;
}

void do_tile(struct graphics *gc, unsigned int z_draw, unsigned int x_draw, unsigned int y_draw,
		int bytes, struct color_range *colors, char *fname, int mapbits, int metabits, int gps, int dump, int maxn, int pass,
		int xoff, int yoff) {
	int i;

	// Do the single-point case

	unsigned char startbuf[bytes];
	unsigned char endbuf[bytes];
	zxy2bufs(z_draw, x_draw, y_draw, startbuf, endbuf, bytes);
	int further = process(fname, 1, z_draw, startbuf, endbuf, z_draw, x_draw, y_draw, gc, mapbits, metabits, dump, gps, colors, xoff, yoff);

	// When overzoomed, also look up the adjacent tile
	// to keep from drawing partial circles.

	if ((further || circle > 0) && !dump) {
		int above = 1;
		int below = 1;

		if (circle > 0) {
			double size = cloudsize(z_draw, x_draw, y_draw);
			above = size + 1;
			below = size + 1;
		}
		
		int xx, yy;

		for (xx = x_draw - above; xx <= x_draw + below; xx++) {
			for (yy = y_draw - above; yy <= y_draw + below; yy++) {
				if (x_draw != xx || y_draw != yy) {
					zxy2bufs(z_draw, xx, yy, startbuf, endbuf, bytes);
					process(fname, 1, z_draw, startbuf, endbuf, z_draw, x_draw, y_draw, gc, mapbits, metabits, dump, gps, colors, xoff, yoff);
				}
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
			process(fname, i, z_lookup, startbuf, endbuf, z_draw, x_draw, y_draw, gc, mapbits, metabits, dump, gps, colors, xoff, yoff);
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
			process(fname, i, z_lookup, startbuf, endbuf, z_draw, x_draw, y_draw, gc, mapbits, metabits, dump, gps, colors, xoff, yoff);
		}
	}
}
