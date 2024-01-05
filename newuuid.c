#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ndb.h>

#include "uuid.h"

Ndb* actordb;
void main(int argc, char *argv[]) {
	uuid_t* u = newuuid();
	int newline = 1;
	if (argc > 1 && strcmp(argv[1], "-n") == 0) {
		newline = 0;
	}
	fmtinstall('U', Ufmt);
	if (newline)
		print("%U\n", *u);
	else
		print("%U", *u);
	exits("");
}
