struct graphics;

struct graphics *graphics_init(int width, int height);
void out(struct graphics *graphics, int transparency, double gamma, int invert, int color, int color2, int saturate, int mask);

int drawClip(double x0, double y0, double x1, double y1, struct graphics *graphics, double bright, double hue, int antialias, double thick);
void drawPixel(double x, double y, struct graphics *graphics, double bright, double hue);
void drawBrush(double x, double y, struct graphics *graphics, double bright, double brush, double hue);
