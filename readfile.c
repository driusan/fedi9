#include <u.h>
#include <libc.h>
#include <json.h>

#define BUFSZ 4096

int debug = 0;
char *readfile(int fd) {
	int n, nrv =0;
	// char buf[BUFSZ] was smashing the thread stack in fedi9/fs
	// so we alloc it on the heap.
	char *buf = malloc(BUFSZ); 

	char *rv = 0;
	while((n = read(fd, buf, BUFSZ)) > 0) {
		rv = realloc(rv, nrv+n+1);
	
		memcpy(&rv[nrv], buf, n);
		nrv += n;
		rv[nrv] = 0;
	}
	free(buf);
	return rv;
}
