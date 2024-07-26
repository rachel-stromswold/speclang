#include <math.h>
#include <stdio.h>
#include "speclang.h"

int main(int argc, const char** argv) {
    if (argc < 2)
	fprintf(stderr, "usage: spcl <filename>\n");
    char* fname = argv[1];
    //if additional arguments were supplied, pass them
    if (argc > 2) {
	argc -= 2;
	argv = argv+2;
    } else {
	argc = 0;
	argv = NULL;
    }
    //actually call
    /*spcl_val v = spcl_inst_from_file(fname, argc, argv);
    if (v.type == VAL_ERR) {
	cleanup_spcl_val(&v);
	return 1;
    }
    cleanup_spcl_val(&v);
    return 0;*/
}
