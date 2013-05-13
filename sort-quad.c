#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string.h>
#include <math.h>

int quadcmp(const void *v1, const void *v2) {
	const unsigned long long *q1 = v1;
	const unsigned long long *q2 = v2;

	if (*q1 > *q2) {
		return 1;
	} else if (*q1 == *q2) {
		return 0;
	} else {
		return -1;
	}
}

int main(int argc, char **argv) {
	if (argc != 2) {
		fprintf(stderr, "Usage: %s file.quad\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	int fd = open(argv[1], O_RDONLY);
	if (fd < 0) {
		perror(argv[1]);
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

	qsort(map, st.st_size / sizeof(unsigned long long),
		sizeof(unsigned long long), quadcmp);

	size_t off = 0;
	while (off < st.st_size) {
		ssize_t written = write(1, map + off, st.st_size - off);

		if (written < 0) {
			perror("write");
			exit(1);
		}

		off += written;
	}

	return 0;
}
