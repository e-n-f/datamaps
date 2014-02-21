#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string.h>
#include <stddef.h>
#include <math.h>
#include <dirent.h>
#include <ctype.h>
#include <limits.h>
#include "util.h"
#include "graphics.h"
#include "clip.h"
#include "dump.h"

int dot_base = 13;
double dot_bright = 0.05917;
double dot_ramp = 1.23;

double point_size = 1;
int gaussian = 0;

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

struct file {
	char *name;
	int mapbits;
	int metabits;
	int maxn;
	int bytes;
	int version;
};

void do_tile(struct graphics *gc, struct file *f, unsigned int z_draw, unsigned int x_draw, unsigned int y_draw, struct color_range *colors, int gps, int dump, int pass, int xoff, int yoff, int assemble);

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

int process(struct file *f, int components, int z_lookup, unsigned char *startbuf, unsigned char *endbuf, int z_draw, int x_draw, int y_draw, struct graphics *gc, int dump, int gps, struct color_range *colors, int xoff, int yoff) {
	char fn[strlen(f->name) + 1 + 5 + 1 + 5 + 1];
	int justdots = 0;

	if (components == 0) {
		sprintf(fn, "%s/%d,%d", f->name, components, z_lookup);
		components = 1;
	} else if (components == 1) {
		sprintf(fn, "%s/1,0", f->name);
		justdots = 1;
	} else {
		sprintf(fn, "%s/%d,%d", f->name, components, z_lookup);
	}

	int bytes = bytesfor(f->mapbits, f->metabits, components, z_lookup);
	int ret = 0;

	struct tilecontext tc;
	tc.z = z_draw;
	tc.x = x_draw;
	tc.y = y_draw;
	tc.xoff = xoff;
	tc.yoff = yoff;

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

	int xfd = 0;
	struct stat xst;
	unsigned char *xmap = NULL;

	if (f->version >= 2) {
		sprintf(fn, "%s/extra", f->name);
		int xfd = open(fn, O_RDONLY);
		if (xfd < 0) {
			perror(fn);
			return ret;
		}

		if (fstat(xfd, &xst) < 0) {
			perror("stat");
			exit(EXIT_FAILURE);
		}

		xmap = mmap(NULL, xst.st_size, PROT_READ, MAP_SHARED, xfd, 0);
		if (xmap == MAP_FAILED) {
			perror("mmap");
			exit(EXIT_FAILURE);
		}
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
	if (justdots) {
		bright1 = dot_bright;

		if (z_draw > dot_base) {
			step = 1;
			brush = exp(log(2.0) * (z_draw - dot_base));
			bright1 *= exp(log(dot_ramp) * (z_draw - dot_base));
		} else {
			step = floor(exp(log(exponent) * (dot_base - z_draw)) + .5);
			bright1 *= exp(log(dot_ramp) * (z_draw - dot_base));
			bright1 = bright1 * step / (1 << (dot_base - z_draw));
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

	for (; start < end; start += step * bytes) {
		unsigned int x0[components], y0[components];
		unsigned int *x = x0, *y = y0;
		unsigned long long meta = 0;

		buf2xys(start, f->mapbits, f->metabits, z_lookup, components, x0, y0, &meta);

		int additional = 0;
		unsigned char *cp = NULL;
		if (f->metabits > 0 && f->version >= 2) {
			cp = xmap + meta;
			additional = decodeSigned(&cp);

			int type = additional & GEOM_TYPE_MASK;
			additional = (additional >> GEOM_TYPE_BITS) - 1;
		}

		unsigned int xa[components + additional], ya[components + additional];
		if (f->metabits > 0 && f->version >= 2) {
			x = xa;
			y = ya;
			xa[0] = x0[0];
			ya[0] = y0[0];

			int i;
			int s = 32 - f->mapbits / 2;
			for (i = 1; i < components + additional; i++) {
				xa[i] = xa[i - 1] + (decodeSigned(&cp) << s);
				ya[i] = ya[i - 1] + (decodeSigned(&cp) << s);

			}
		}

		double xd[components + additional], yd[components + additional];
		int k;

		if (!dump && z_draw >= f->mapbits / 2 - 8) {
			// Add noise below the bottom of the file resolution
			// so that it looks less gridded when overzoomed

			int j;
			for (j = 0; j < components + additional; j++) {
				int noisebits = 32 - f->mapbits / 2;
				int i;

				for (i = 0; i < noisebits; i++) {
					x[j] |= ((y[j] >> (2 * noisebits - 1 - i)) & 1) << i;
					y[j] |= ((x[j] >> (2 * noisebits - 1 - i)) & 1) << i;
				}
			}
		}

		double hue = -1;
		if (f->metabits > 0 && colors->active) {
			hue = (((double) meta - colors->meta1) / (colors->meta2 - colors->meta1) * (colors->hue2 - colors->hue1) + colors->hue1) / 360;
			while (hue < 0) {
				hue++;
			}
			while (hue > 1) {
				hue--;
			}
		}

		double bright = bright1;

		for (k = 0; k < components + additional; k++) {
			wxy2fxy(x[k], y[k], &xd[k], &yd[k], z_draw, x_draw, y_draw);
		}

		if (dump) {
			int should = 0;

			if (justdots) {
				should = 1;
			} else {
				for (k = 1; k < components + additional; k++) {
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
				dump_out(dump, x, y, components + additional, f->metabits, meta);
			}
		} else if (justdots) {
			if (!antialias) {
				xd[0] = ((int) (xd[0] * tilesize) + .5) / tilesize;
				yd[0] = ((int) (yd[0] * tilesize) + .5) / tilesize;
			}

			if (circle > 0) {
				if (size < .5) {
					if (b <= 1) {
						drawPixel((xd[0] * tilesize - .5) + xoff, (yd[0] * tilesize - .5) + yoff, gc, bright * b * meta / innerstep, hue, meta, &tc);
					} else {
						drawBrush((xd[0] * tilesize) + xoff, (yd[0] * tilesize) + yoff, gc, bright * meta / innerstep, b, hue, meta, gaussian, &tc);
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
								drawPixel(xp - .5, yp - .5, gc, bright * b, hue, meta, &tc);
							} else {
								drawBrush(xp, yp, gc, bright, b, hue, meta, gaussian, &tc);
								ret = 1;
							}
						}
					}
				}
			} else {
				if (b <= 1) {
					drawPixel((xd[0] * tilesize - .5) + xoff, (yd[0] * tilesize - .5) + yoff, gc, bright * b, hue, meta, &tc);
				} else {
					drawBrush((xd[0] * tilesize) + xoff, (yd[0] * tilesize) + yoff, gc, bright, b, hue, meta, gaussian, &tc);
					ret = 1;
				}
			}
		} else {
			for (k = 1; k < components + additional; k++) {
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
					drawClip(xd[k - 1] * tilesize + xoff, yd[k - 1] * tilesize + yoff, xd[k] * tilesize + xoff, yd[k] * tilesize + yoff, gc, bright1, hue, meta, antialias, thick1, &tc);

					wxy2fxy(x[k], y[k], &xd[k], &yd[k], z_draw, x_draw, y_draw);
					wxy2fxy(xk1 + (1LL << 32), y[k - 1], &xd[k - 1], &yd[k - 1], z_draw, x_draw, y_draw);
					drawClip(xd[k - 1] * tilesize + xoff, yd[k - 1] * tilesize + yoff, xd[k] * tilesize + xoff, yd[k] * tilesize + yoff, gc, bright1, hue, meta, antialias, thick1, &tc);

					wxy2fxy(x[k - 1], y[k - 1], &xd[k - 1], &yd[k - 1], z_draw, x_draw, y_draw);
				} else if (xk1 - xk >= (1LL << 31)) {
					wxy2fxy(xk1 - (1LL << 32), y[k - 1], &xd[k - 1], &yd[k - 1], z_draw, x_draw, y_draw);
					drawClip(xd[k - 1] * tilesize + xoff, yd[k - 1] * tilesize + yoff, xd[k] * tilesize + xoff, yd[k] * tilesize + yoff, gc, bright1, hue, meta, antialias, thick1, &tc);

					wxy2fxy(x[k - 1], y[k - 1], &xd[k - 1], &yd[k - 1], z_draw, x_draw, y_draw);
					wxy2fxy(xk + (1LL << 32), y[k], &xd[k], &yd[k], z_draw, x_draw, y_draw);
					drawClip(xd[k - 1] * tilesize + xoff, yd[k - 1] * tilesize + yoff, xd[k] * tilesize + xoff, yd[k] * tilesize + yoff, gc, bright1, hue, meta, antialias, thick1, &tc);

					wxy2fxy(x[k], y[k], &xd[k], &yd[k], z_draw, x_draw, y_draw);
				} else {
					drawClip(xd[k - 1] * tilesize + xoff, yd[k - 1] * tilesize + yoff, xd[k] * tilesize + xoff, yd[k] * tilesize + yoff, gc, bright1, hue, meta, antialias, thick1, &tc);
				}
			}
		}
	}

	munmap(map, st.st_size);
	close(fd);

	if (f->version >= 2) {
		munmap(xmap, xst.st_size);
		close(xfd);
	}
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

void quote(FILE *fp, char *s) {
	fprintf(fp, "\"");
	for (; *s != '\0'; s++) {
		if (*s == '\\' || *s == '\"') {
			fputc('\\', fp);
			fputc(*s, fp);
		} else if (*s < ' ') {
			fprintf(fp, "\\u%04x", *s);
		} else {
			fputc(*s, fp);
		}
	}
	fprintf(fp, "\"");
}

void prep(char *outdir, int z, int x, int y, char *filetype, char *fname) {
	if (outdir == NULL) {
		return;
	}

	char path[strlen(outdir) + 12 + 12 + 12 + 5];

	sprintf(path, "%s", outdir);
	mkdir(path, 0777);

	// This is stupid, but we don't know how deep
	// the enumeration will go from here
	int maxzoom = z, minzoom = z;
	DIR *d = opendir(outdir);
	if (d != NULL) {
		struct dirent *de;
		while ((de = readdir(d)) != NULL) {
			if (isdigit(*de->d_name)) {
				int n = atoi(de->d_name);
				if (n > maxzoom) {
					maxzoom = n;
				}
				if (n < minzoom) {
					minzoom = n;
				}
			}
		}
		closedir(d);
	}

	sprintf(path, "%s/metadata.json", outdir);
	FILE *fp = fopen(path, "w");

	if (fp == NULL) {
		perror(path);
		exit(EXIT_FAILURE);
	}

	fprintf(fp, "{\n");

	fprintf(fp, "\"name\": ");
	quote(fp, outdir);
	fprintf(fp, ",\n");

	fprintf(fp, "\"description\": ");
	quote(fp, fname);
	fprintf(fp, ",\n");
	
	fprintf(fp, "\"version\": 1,\n");
	fprintf(fp, "\"minzoom\": %d,\n", minzoom);
	fprintf(fp, "\"maxzoom\": %d,\n", maxzoom);
	fprintf(fp, "\"type\": \"overlay\",\n");

	if (strcmp(filetype, "pbf") == 0) {
		fprintf(fp, "\"json\": \"{");
		fprintf(fp, "\\\"vector_layers\\\": [ { \\\"id\\\": \\\"points\\\", \\\"description\\\": \\\"\\\", \\\"minzoom\\\": %d, \\\"maxzoom\\\": %d, \\\"fields\\\": {\\\"meta\\\": \\\"Number\\\" } }, { \\\"id\\\": \\\"lines\\\", \\\"description\\\": \\\"\\\", \\\"minzoom\\\": %d, \\\"maxzoom\\\": %d, \\\"fields\\\": {\\\"meta\\\": \\\"Number\\\" } } ]", minzoom, maxzoom, minzoom, maxzoom);
		fprintf(fp, "}\",\n");
	}

	fprintf(fp, "\"format\": \"%s\"\n", filetype); // no trailing comma
	fprintf(fp, "}\n");

	fclose(fp);

	sprintf(path, "%s/%d", outdir, z);
	mkdir(path, 0777);

	sprintf(path, "%s/%d/%d", outdir, z, x);
	mkdir(path, 0777);

	sprintf(path, "%s/%d/%d/%d.%s", outdir, z, x, y, filetype);
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
	int vector_styles = 0;
	int leaflet_retina = 0;
	char *filetype;

	colors.active = 0;

	int nfiles = 0;
	struct file files[argc];

	while ((i = getopt(argc, argv, "t:dDgC:B:G:O:M:aAwc:l:L:smf:S:T:o:x:e:p:vr")) != -1) {
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
			if (exponent < 1) {
				fprintf(stderr, "Exponent can't be less than 1: %f\n", exponent);
				usage(argv);
			}
			break;

		case 'p':
			if (sscanf(optarg, "g%lf", &point_size) == 1) {
				gaussian = 1;
			} else if (sscanf(optarg, "%lf", &point_size) != 1) {
				fprintf(stderr, "Can't understand -%c %s\n", i, optarg);
				usage(argv);
			}
			break;

		case 'v':
			vector_styles = 1;
			break;

		case 'r':
			leaflet_retina = 1;
			break;

		default:
			fprintf(stderr, "Unknown option %c\n", i);
			usage(argv);
		}
	}

	if (vector_styles) {
		printf("Map {\n");
		if (invert) {
			printf("  background-color: rgba(255,255,255,%.3f);\n", transparency / 255.0);
		} else {
			printf("  background-color: rgba(0,0,0,%.3f);\n", transparency / 255.0);
		}
		printf("}\n\n");

		printf("#points {\n");
		if (invert) {
			printf("  line-color: #000000;\n");
		} else {
			printf("  line-color: #FFFFFF;\n");
		}
		printf("  line-cap: round;\n");
		printf("  line-width: %.3f;\n", 2 * sqrt(1 / M_PI)); // diameter of circle with area 1

		if (color != -1 || color2 != -1) {
			int r1 = (color >> 16)  & 0xFF;
			int g1 = (color >> 8)   & 0xFF;
			int b1 = (color)        & 0xFF;

			int r2 = (color2 >> 16) & 0xFF;
			int g2 = (color2 >> 8)  & 0xFF;
			int b2 = (color2)       & 0xFF;

			if (color == -1) {
				r1 = g1 = b1 = 128;
			}
			if (color2 == -1) {
				if (invert) {
					r2 = g2 = b2 = 0;
				} else {
					r2 = g2 = b2 = 255;
				}
			}

			// Color 1 is repeated so the bottom half of the range
			// all has that hue. The middle will only have half
			// alpha instead of fully reaching the middle color
			// as intended.

			printf("  image-filters: colorize-alpha(");
			printf("#%02X%02X%02X, ", r1, g1, b1);
			printf("#%02X%02X%02X, ", r1, g1, b1);
			printf("#%02X%02X%02X);\n", r2, g2, b2);
		}

		// steps to get to full brightness with dot_bright
		double steps = 1.0 / dot_bright;
		// steps to get to half brightness, taking gamma into account
		double halfsteps = steps * exp(log(.5) / display_gamma);
		// alpha to get to half brightness with same number of steps
		double alpha = 1 - exp(log(.5) / halfsteps);
		printf("  line-opacity: %.3f;\n", alpha);
		printf("\n");

		int i;
		for (i = 0; i <= dot_base; i++) {
			printf("  [zoom >= %2d] {", i);

			// dot brightness decreases by ramp with each zoom
			double steps = 1.0 / (dot_bright * exp(log(dot_ramp) * (i - dot_base)));
			double halfsteps = steps * exp(log(.5) / display_gamma);
			double alpha = 1 - exp(log(.5) / halfsteps);

			printf(" line-opacity: %7.3f;", alpha);
			printf(" }\n");
		}
		for (i = dot_base + 1; i < 23; i++) {
			printf("  [zoom >= %2d] {", i);

			// dot brightness increases by ramp with each zoom
			double steps = 1.0 / (dot_bright * exp(log(dot_ramp) * (i - dot_base)));
			double halfsteps = steps * exp(log(.5) / display_gamma);
			double alpha = 1 - exp(log(.5) / halfsteps);

			printf(" line-opacity: %7.3f;", alpha);

			// area doubles with each zoom
			printf(" line-width: %7.3f;", 2 * sqrt((1 << (i - dot_base)) / M_PI));
			printf(" }\n");
		}

		printf("}\n");

		return EXIT_SUCCESS;
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
		if (fgets(s, 2000, f) == NULL || sscanf(s, "%d", &files[i].version) != 1) {
			fprintf(stderr, "%s: Unknown version %s", meta, s);
			exit(EXIT_FAILURE);
		}
		if (files[i].version > 2) {
			fprintf(stderr, "%s: Version too large: %s", meta, s);
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
		unsigned wx1, wy1, wx2, wy2;

		latlon2tile(atof(argv[optind + 2]), atof(argv[optind + 3]), 32, &wx1, &wy1);
		latlon2tile(atof(argv[optind + 4]), atof(argv[optind + 5]), 32, &wx2, &wy2);

		if (wx1 > wx2) {
			unsigned t = wx2;
			wx2 = wx1;
			wx1 = t;
		}

		if (wy1 > wy2) {
			unsigned t = wy2;
			wy2 = wy1;
			wy1 = t;
		}

		double fx1, fy1, fx2, fy2;

		unsigned x1 = (long long) wx1 >> (32 - z_draw);
		unsigned y1 = (long long) wy1 >> (32 - z_draw);

		unsigned x2 = (long long) wx2 >> (32 - z_draw);
		unsigned y2 = (long long) wy2 >> (32 - z_draw);

		wxy2fxy(wx1, wy1, &fx1, &fy1, z_draw, x1, y1);
		wxy2fxy(wx2, wy2, &fx2, &fy2, z_draw, x2, y2);

		fprintf(stderr, "making zoom %u: %u/%u to %u/%u\n", z_draw, x1, y1, x2, y2);
		fprintf(stderr, "that's %f by %f\n", tilesize * (x2 - x1 + fx2 - fx1), tilesize * (y2 - y1 + fy2 - fy1));

		double stride = (x2 - x1 + fx2 - fx1) * tilesize;
		struct graphics *gc = NULL;

		if (!dump) {
			if (stride * (y2 - y1 + fy2 - fy1) * tilesize > 10000 * 10000) {
				fprintf(stderr, "Image too big\n");
				exit(EXIT_FAILURE);
			}

			gc = graphics_init((x2 - x1 + fx2 - fx1) * tilesize, (y2 - y1 + fy2 - fy1) * tilesize, &filetype);
		}

		unsigned int x, y;
		for (x = x1; x <= x2; x++) {
			for (y = y1; y <= y2; y++) {
				fprintf(stderr, "%u/%u/%u\r", z_draw, x, y);

				for (i = 0; i < nfiles; i++) {
					do_tile(gc, &files[i], z_draw, x, y, &colors, gps, dump, i, (x - x1 - fx1) * tilesize, (y - y1 - fy1) * tilesize, assemble);
				}
			}
		}

		if (!dump) {
			fprintf(stderr, "output: %d by %d\n", (int) (tilesize * (x2 - x1 + fx2 - fx1)), (int) (tilesize * (y2 - y1 + fy2 - fy1)));
			prep(outdir, z_draw, x1, y1, filetype, files[0].name);
			out(gc, transparency, display_gamma, invert, color, color2, saturate, mask);
		}
	} else {
		struct graphics *gc = graphics_init(tilesize, tilesize, &filetype);

		unsigned int x_draw = atoi(argv[optind + 2]);
		unsigned int y_draw = atoi(argv[optind + 3]);

		unsigned int x_draw_render = x_draw;
		unsigned int y_draw_render = y_draw;
		unsigned int z_draw_render = z_draw;
		int xoff = 0;
		int yoff = 0;

		if (leaflet_retina && z_draw > 0) {
			if (x_draw % 2 != 0) {
				xoff -= tilesize;
			}
			if (y_draw % 2 != 0) {
				yoff -= tilesize;
			}

			x_draw_render /= 2;
			y_draw_render /= 2;
			z_draw_render--;

			tilesize *= 2;
		}

		for (i = 0; i < nfiles; i++) {
			do_tile(gc, &files[i], z_draw_render, x_draw_render, y_draw_render, &colors, gps, dump, i, xoff, yoff, assemble);
		}

		if (!dump) {
			prep(outdir, z_draw, x_draw, y_draw, filetype, files[0].name);
			out(gc, transparency, display_gamma, invert, color, color2, saturate, mask);
		}
	}

	if (dump) {
		dump_end(dump);
	}

	return 0;
}

void do_tile(struct graphics *gc, struct file *f, unsigned int z_draw, unsigned int x_draw, unsigned int y_draw,
		struct color_range *colors, int gps, int dump, int pass,
		int xoff, int yoff, int assemble) {
	int i;

	// Do the single-point case

	unsigned char startbuf[f->bytes];
	unsigned char endbuf[f->bytes];
	zxy2bufs(z_draw, x_draw, y_draw, startbuf, endbuf, f->bytes);
	int further = process(f, 1, z_draw, startbuf, endbuf, z_draw, x_draw, y_draw, gc, dump, gps, colors, xoff, yoff);

	// When overzoomed, also look up the adjacent tile
	// to keep from drawing partial circles.

	if ((further || circle > 0) && !dump && !assemble) {
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
					zxy2bufs(z_draw, xx, yy, startbuf, endbuf, f->bytes);
					process(f, 1, z_draw, startbuf, endbuf, z_draw, x_draw, y_draw, gc, dump, gps, colors, xoff, yoff);
				}
			}
		}
	}

	// In version 1, segments with 2 through N points for each zoom level
	// are in separate files numbered 2 through N.
	//
	// In version 2, all multipoint segments for a zoom level are in the
	// same file (0), with the other points indexed through the metadata.
	//
	// This should address bad performance problems when there are many
	// different sizes of polylines, at the expense of some storage size.

	int start, end;
	if (f->version < 2) {
		start = 2;
		end = f->maxn;
	} else {
		start = 0;
		end = 0;
	}

	// Do the zoom levels numbered greater than this one.
	//
	// For zoom levels greater than this one, we look up the entire area
	// of the tile we are drawing, which will end up being multiple tiles
	// of the higher zoom.

	int z_lookup;
	for (z_lookup = z_draw + 1; (dump || z_lookup < z_draw + 9) && z_lookup <= f->mapbits / 2; z_lookup++) {
		for (i = start; i <= end; i++) {
			int bytes = bytesfor(f->mapbits, f->metabits, i, z_lookup);

			unsigned char startbuf[bytes];
			unsigned char endbuf[bytes];
			zxy2bufs(z_draw, x_draw, y_draw, startbuf, endbuf, bytes);
			process(f, i, z_lookup, startbuf, endbuf, z_draw, x_draw, y_draw, gc, dump, gps, colors, xoff, yoff);
		}
	}

	// For zoom levels numbered less than this one, each stage looks up a
	// larger area for potential overlaps.

	int x_lookup, y_lookup;
	for (z_lookup = z_draw, x_lookup = x_draw, y_lookup = y_draw;
	     z_lookup >= 0;
	     z_lookup--, x_lookup /= 2, y_lookup /= 2) {
		for (i = start; i <= end; i++) {
			int bytes = bytesfor(f->mapbits, f->metabits, i, z_lookup);

			unsigned char startbuf[bytes];
			unsigned char endbuf[bytes];
			zxy2bufs(z_lookup, x_lookup, y_lookup, startbuf, endbuf, bytes);
			process(f, i, z_lookup, startbuf, endbuf, z_draw, x_draw, y_draw, gc, dump, gps, colors, xoff, yoff);
		}
	}
}
