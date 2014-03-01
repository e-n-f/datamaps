struct dump_meta;

void dump_begin(int dump);
void dump_end(int dump);
void dump_out(int dump, unsigned int *x, unsigned int *y, int components, int metabits, long long meta, struct dump_meta *data, int ndata);

