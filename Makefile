all: encode render

ENCODE_OBJS = encode.o util.o
RENDER_OBJS = render.o util.o

encode: $(ENCODE_OBJS)
	cc -g -Wall -O3 -o $@ $^ -lm

render: $(RENDER_OBJS)
	cc -g -Wall -O3 -o $@ $^ -lm -lpng16 -lz

.c.o:
	cc -g -Wall -O3 -c $<
