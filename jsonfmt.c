#include <u.h>
#include <libc.h>
#include <json.h>

#include <bio.h>
#include <ndb.h>

Ndb *actordb;

#include "readfile.h"

void main(int argc, char *argv[]) {
	if (argc != 1) {
		fprint(2, "usage: %s \n", argv[0]);
		exits("usage");
	}
	char *buf = readfile(0);
	JSON* root = jsonparse(buf);
	if (root == nil) {
		exits("badjson");
	}
	JSONfmtinstall();
	print("%J\n", root);
	free(root);
	exits("");
}
