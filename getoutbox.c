#include <u.h>
#include <libc.h>
#include <json.h>
#include <stdio.h>
#include <bio.h>
#include <ndb.h>

#include "readfile.h"
#include "uuid.h"
#include "outbox.h"
Ndb *actordb;

void main(int argc, char *argv[]) {
	if (argc != 2) {
		fprint(2, "usage: %s outboxURL\n", argv[0]);
		exits("usage");
	}
	fmtinstall('U', Ufmt);
	fmtinstall('N', Nfmt);

	JSONfmtinstall();
	outboxRetrievalStats stats = getoutbox(argv[1]);

	print("%d new posts, %d new replies\n", stats.nposts, stats.nreplies);
	exits("");
}
