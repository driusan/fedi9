#include <u.h>
#include <libc.h>
#include "uuid.h"

void main(int, char *[]) {
	uuid_t* u = newuuid();
	fmtinstall('U', Ufmt);
	print("%U\n", *u);
	exits("");
}
