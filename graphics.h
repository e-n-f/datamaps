double *graphics_init();
void out(double *src, double *cx, double *cy, int width, int height, int transparency, double gamma, int invert, int color, int color2, int saturate, int mask);

int drawClip(double x0, double y0, double x1, double y1, double *image, double *cx, double *cy, double bright, double hue, int antialias, double thick);
void drawPixel(double x, double y, double *image, double *cx, double *cy, double bright, double hue);
void drawBrush(double x, double y, double *image, double *cx, double *cy, double bright, double brush, double hue);
