#include <u.h>
#include <libc.h>

#include <json.h>

#include "readfile.h"

char* geturlhostname(char *url) {
	// we're too lazy to parse the url so we just let webfs
	// do it.
	char buf[1000];
	int fd = open("/mnt/web/clone", ORDWR);
	int n = read(fd, buf, 1000);
	assert(n > 0);
	int conn = atoi(buf);
	fprint(fd, "url %s\n", url);
	sprint(buf, "/mnt/web/%d/parsed/host", conn);
	int fd2 = open(buf, OREAD);
	char *result = readfile(fd2);
	close(fd2);
	close(fd);
	return result;
}

char* getfieldstr(JSON* data, char* field) {
	JSON* fieldj = jsonbyname(data, field);
	if (fieldj && fieldj->t != JSONNull) {
		assert(fieldj->t == JSONString);
		return strdup(fieldj->s);
	}
	return nil;
}

