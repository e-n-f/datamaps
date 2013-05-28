extern int gSortBytes;
int bufcmp(const void *v1, const void *v2);
void *search(const void *key, const void *base, size_t nel, size_t width, int (*cmp)(const void *, const void *));

void latlon2tile(double lat, double lon, int zoom, unsigned int *x, unsigned int *y);
void tile2latlon(unsigned int x, unsigned int y, int zoom, double *lat, double *lon);

void wxy2fxy(long long wx, long long wy, double *ox, double *oy, int z, int x, int y);
void xy2buf(unsigned int x32, unsigned int y32, unsigned char *buf, int *offbits, int n, int skip);
void zxy2bufs(unsigned int z, unsigned int x, unsigned int y, unsigned char *startbuf, unsigned char *endbuf, int bytes);
void buf2xys(unsigned char *buf, int mapbits, int skip, int n, unsigned int *x, unsigned int *y);
void meta2buf(int bits, long long data, unsigned char *buf, int *offbits, int max);

int bytesfor(int mapbits, int metabits, int components, int z_lookup);
