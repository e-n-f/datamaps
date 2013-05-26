all: encode render

encode: encode.c
	cc -g -Wall -O3 -o $@ $< -lm

render: render.c
	cc -g -Wall -O3 -o $@ $< -lm -lpng16 -lz
