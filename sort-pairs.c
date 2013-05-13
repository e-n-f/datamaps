#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string.h>
#include <math.h>

#define LEVELS 24
#define BYTES (2 * 2 * LEVELS / 8)

int quadcmp(const void *v1, const void *v2) {
	const unsigned char *q1 = v1;
	const unsigned char *q2 = v2;

	int i;
	for (i = 2 * 2 * LEVELS / 8 - 1; i >= 0; i--) {
		int diff = (int) q1[i] - (int) q2[i];

		if (diff != 0) {
			return diff;
		}
	}

	return 0;
}

int main(int argc, char **argv) {
	if (argc != 2) {
		fprintf(stderr, "Usage: %s dir\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	int level;
	for (level = 0; level < LEVELS; level++) {
		char fname[strlen(argv[1]) + 3 + 5 + 1];
		sprintf(fname, "%s/%d", argv[1], level);
		fprintf(stderr, "%s\n", fname);

		int fd = open(fname, O_RDONLY);
		if (fd < 0) {
			perror(fname);
			exit(EXIT_FAILURE);
		}

		struct stat st;
		if (fstat(fd, &st) < 0) {
			perror("stat");
			exit(EXIT_FAILURE);
		}

		void *map = mmap(NULL, st.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
		if (map == MAP_FAILED) {
			perror("mmap");
			exit(EXIT_FAILURE);
		}

		qsort(map, st.st_size / BYTES, BYTES, quadcmp);

		sprintf(fname, "%s/%d.sort", argv[1], level);
		fprintf(stderr, "%s\n", fname);
		int out = open(fname, O_RDWR | O_CREAT, 0666);
		if (out < 0) {
			perror(fname);
			exit(EXIT_FAILURE);
		}

		size_t off = 0;
		while (off < st.st_size) {
			ssize_t written = write(out, map + off, st.st_size - off);

			if (written < 0) {
				perror("write");
				exit(1);
			}

			off += written;
		}

		munmap(map, st.st_size);
		close(fd);
		close(out);
	}

	return 0;
}
