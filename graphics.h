void out(double *src, double *cx, double *cy, int width, int height, int transparency, double gamma);

void drawClip(double x0, double y0, double x1, double y1, double *image, double *cx, double *cy, double bright, double hue);
void drawPixel(double x, double y, double *image, double *cx, double *cy, double bright, double hue);
void drawBrush(double x, double y, double *image, double *cx, double *cy, double bright, int brush, double hue);
