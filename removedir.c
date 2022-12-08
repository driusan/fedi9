#include <u.h>
#include <libc.h>

#include <fcall.h>
#include <thread.h>
#include <9p.h>

#include "removedir.h"

void removedir(File *f, int removeFile) {
	if (f == nil) {
		return;
	}
	incref(f);
	Readdir *dir = opendirfile(f);
	assert(dir != nil);
	uchar *dbuf = malloc(1024);
	int n, off=0;
	while((n = readdirfile(dir, dbuf, 1024, off)) > 0) {
		off+=n;
		Dir d;
		char *strs = malloc(1024);
		int remn = n;
		uchar *p = dbuf;
		while (remn > 0) {
			int y = convM2D(p, n, &d, strs);
			if (d.name != nil) {
				File *child = walkfile(f, d.name);
				incref(child);
				if ((d.mode & DMDIR) != 0) {
					removedir(child, 1);
				} else {
					assert(removefile(child) == 0);
				}
			
			}
			p+= y;
			remn -= y;
		}
		free(strs);
	}
	closedirfile(dir);
	if (removeFile)	assert(removefile(f) == 0);
	free(dbuf);

}
