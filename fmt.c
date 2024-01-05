#include <u.h>
#include <libc.h>

#include <json.h>

#include <bio.h>
#include <ndb.h>

extern Ndb* actordb;
#include "utils.h"

static int toFmt(Fmt *f){
	JSON *j = va_arg(f->args, JSON*);
	if (j == nil) {
		return 0;
	}
	int n;
	switch(j->t){
	case JSONString:
		return fmtprint(f, "%M", j->s);
	case JSONNull:
		return 0;
	case JSONArray:
		n = 0;
		JSONEl *cur = j->first;
		while(cur != nil) {
			if (cur->next == nil) {
				n += fmtprint(f, "%M", cur->val->s);
			} else {
				n += fmtprint(f, "%M ", cur->val->s);
			}
			cur = cur->next;
		}
		return n;
	default:
		assert(0);
	}
	return 0;
}
static int mentionFmt(Fmt *f){
	char *url;
	Ndbs s;
	url = va_arg(f->args, char*);
	// fprint(2, "URL %s\n", url);
	if (url == nil || strcmp(url, "") == 0) {
		return 0;
	}
	if (strcmp(url, "https://www.w3.org/ns/activitystreams#Public") == 0) {
		return fmtprint(f, "Public");
	}

	char* preferredUsername = ndbgetvalue(actordb, &s, "id", url, "preferredUsername", nil);
	if (preferredUsername == nil) {
		return fmtprint(f, "%s", url);
	}
	char *host = geturlhostname(url);
	int n = fmtprint(f, "@%s@%s", preferredUsername, host);
	free(preferredUsername);
	free(host);
	return n;
}
void fmtinstalls(void){
	fmtinstall('M', mentionFmt);
	fmtinstall('T', toFmt);
}
