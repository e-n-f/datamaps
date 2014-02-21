struct dump_meta {
	int type;
	unsigned char *key;
	unsigned char *string_value;
	long long int_value;
};

void dump_begin(int dump);
void dump_end(int dump);
void dump_out(int dump, unsigned int *x, unsigned int *y, int components, int metabits, long long meta, struct dump_meta *data, int ndata);

