all: encode render enumerate merge

PNG_CFLAGS=$(shell pkg-config libpng --cflags)
PNG_LDFLAGS=$(shell pkg-config libpng --libs)

ifeq ($(PNG_LDFLAGS),)
PNG_LDFLAGS=-lpng
endif

ENCODE_OBJS = encode.o util.o
RENDER_CORE_OBJS = render.o util.o clip.o dump.o
ENUMERATE_OBJS = enumerate.o util.o dump.o
MERGE_OBJS = merge.o util.o

RENDER_VECTOR_OBJS = vector_tile.pb.o vector.o
RENDER_PNG_OBJS = graphics.o
RENDER_RASTER_OBJS = raster.o

encode: $(ENCODE_OBJS)
	$(CC) -g -Wall -O3 -o $@ $^ -lm

render: $(RENDER_CORE_OBJS) $(RENDER_PNG_OBJS)
	$(CC) -g -Wall -O3 -o $@ $^ -lm -lz $(PNG_LDFLAGS)

render-vector: $(RENDER_CORE_OBJS) $(RENDER_VECTOR_OBJS)
	$(CC) -g -Wall -O3 -o $@ $^ -lm -lz -lprotobuf-lite

render-raster: $(RENDER_CORE_OBJS) $(RENDER_RASTER_OBJS)
	$(CC) -g -Wall -O3 -o $@ $^ -lm

enumerate: $(ENUMERATE_OBJS)
	$(CC) -g -Wall -O3 -o $@ $^ -lm

merge: $(MERGE_OBJS)
	$(CC) -g -Wall -O3 -o $@ $^ -lm

vector_tile.pb.cc vector_tile.pb.h: vector_tile.proto
	protoc --cpp_out=. vector_tile.proto

.c.o:
	$(CC) -g -Wall -O3 $(PNG_CFLAGS) -c $<

%.o: %.cc
	g++ -g -Wall -O3 -c $<

clean:
	rm -f encode
	rm -f render
	rm -f enumerate
	rm -f merge
	rm -f *.o
