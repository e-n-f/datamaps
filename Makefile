all: encode render enumerate merge

PNG_CFLAGS=$(shell pkg-config libpng16 --cflags)
PNG_LDFLAGS=$(shell pkg-config libpng16 --libs)

ENCODE_OBJS = encode.o util.o
RENDER_OBJS = render.o util.o vector_tile.pb.o vector.o clip.o
ENUMERATE_OBJS = enumerate.o util.o
MERGE_OBJS = merge.o util.o

encode: $(ENCODE_OBJS)
	$(CC) -g -Wall -O3 -o $@ $^ -lm $(PNG_LDFLAGS)

render: $(RENDER_OBJS)
	$(CC) -g -Wall -O3 -o $@ $^ -lm -lz $(PNG_LDFLAGS) -lprotobuf-lite

enumerate: $(ENUMERATE_OBJS)
	$(CC) -g -Wall -O3 -o $@ $^ -lm $(PNG_LDFLAGS)

merge: $(MERGE_OBJS)
	$(CC) -g -Wall -O3 -o $@ $^ -lm $(PNG_LDFLAGS)

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
