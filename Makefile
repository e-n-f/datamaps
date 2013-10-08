all: encode render enumerate merge

PNG_CFLAGS=$(shell pkg-config libpng --cflags)
PNG_LDFLAGS=$(shell pkg-config libpng --libs)

ifeq ($(PNG_LDFLAGS),)
PNG_LDFLAGS=-lpng
endif

ENCODE_OBJS = encode.o util.o
RENDER_OBJS = render.o util.o graphics.o
ENUMERATE_OBJS = enumerate.o util.o
MERGE_OBJS = merge.o util.o

encode: $(ENCODE_OBJS)
	$(CC) -g -Wall -O3 -o $@ $^ -lm

render: $(RENDER_OBJS)
	$(CC) -g -Wall -O3 -o $@ $^ -lm -lz $(PNG_LDFLAGS)

enumerate: $(ENUMERATE_OBJS)
	$(CC) -g -Wall -O3 -o $@ $^ -lm

merge: $(MERGE_OBJS)
	$(CC) -g -Wall -O3 -o $@ $^ -lm

.c.o:
	$(CC) -g -Wall -O3 $(PNG_CFLAGS) -c $<

clean:
	rm -f encode
	rm -f render
	rm -f enumerate
	rm -f merge
	rm -f *.o
