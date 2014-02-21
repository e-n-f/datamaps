extern int gSortBytes;
int bufcmp(const void *v1, const void *v2);
void *search(const void *key, const void *base, size_t nel, size_t width, int (*cmp)(const void *, const void *));

void latlon2tile(double lat, double lon, int zoom, unsigned int *x, unsigned int *y);
void tile2latlon(unsigned int x, unsigned int y, int zoom, double *lat, double *lon);

void wxy2fxy(long long wx, long long wy, double *ox, double *oy, int z, int x, int y);
void xy2buf(unsigned int x32, unsigned int y32, unsigned char *buf, int *offbits, int n, int skip);
void zxy2bufs(unsigned int z, unsigned int x, unsigned int y, unsigned char *startbuf, unsigned char *endbuf, int bytes);
void buf2xys(const unsigned char *const buf, const int mapbits, const int metabits, const int skip, const int n, unsigned int *x, unsigned int *y, unsigned long long *meta);
void meta2buf(int bits, long long data, unsigned char *buf, int *offbits, int max);

int bytesfor(int mapbits, int metabits, int components, int z_lookup);

int writeSigned(FILE *fp, long long value);
long long decodeSigned(unsigned char **buf);

#define META_STRING 0
#define META_INTEGER -1
#define META_NUMBER -2

#define GEOM_TYPE_BITS 4
#define GEOM_TYPE_MASK 0x0F;

#define GEOM_POINT 0
#define GEOM_MULTIPOINT 1
#define GEOM_LINESTRING 2
#define GEOM_MULTILINESTRING 3
#define GEOM_POLYGON 4
#define GEOM_MULTIPOLYGON 5
#define GEOM_COLLECTION 6
