all: encode render enumerate

ENCODE_OBJS = encode.o util.o
RENDER_OBJS = render.o util.o graphics.o
ENUMERATE_OBJS = enumerate.o util.o

encode: $(ENCODE_OBJS)
	cc -g -Wall -O3 -o $@ $^ -lm

render: $(RENDER_OBJS)
	cc -g -Wall -O3 -o $@ $^ -lm -lpng16 -lz

enumerate: $(ENUMERATE_OBJS)
	cc -g -Wall -O3 -o $@ $^ -lm

.c.o:
	cc -g -Wall -O3 -c $<
