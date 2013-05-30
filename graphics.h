void out(double *src, int *chroma, int width, int height, int transparency);

void drawClip(double x0, double y0, double x1, double y1, double *image, int *chroma, double bright, int meta);
void drawPixel(double x, double y, double *image, int *chroma, double bright, int meta);
void drawBrush(double x, double y, double *image, int *chroma, double bright, int brush, int meta);
