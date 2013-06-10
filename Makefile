all: encode render enumerate

PNG_CFLAGS=$(shell pkg-config libpng16 --cflags)
PNG_LDFLAGS=$(shell pkg-config libpng16 --libs)

ENCODE_OBJS = encode.o util.o
RENDER_OBJS = render.o util.o graphics.o
ENUMERATE_OBJS = enumerate.o util.o

encode: $(ENCODE_OBJS)
	$(CC) -g -Wall -O3 -o $@ $^ -lm $(PNG_LDFLAGS)

render: $(RENDER_OBJS)
	$(CC) -g -Wall -O3 -o $@ $^ -lm -lz $(PNG_LDFLAGS)

enumerate: $(ENUMERATE_OBJS)
	$(CC) -g -Wall -O3 -o $@ $^ -lm $(PNG_LDFLAGS)

.c.o:
	$(CC) -g -Wall -O3 $(PNG_CFLAGS) -c $<

clean:
	rm -f encode
	rm -f render
	rm -f enumerate
	rm -f *.o
