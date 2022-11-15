#include <u.h>
#include <libc.h>
#include <json.h>

#define BUFSZ 4096

char *readfile(int fd) {
	int n, nrv =0;
	char buf[BUFSZ];
	char *rv = 0;
	
	while((n = read(fd, buf, BUFSZ)) > 0) {
		if (n > 0) {
			rv = realloc(rv, nrv+n+1);
	
			strncpy(&rv[nrv], &buf[0], n);
			nrv += n;
			rv[nrv] = 0;
		}
	}
	return rv;
}
