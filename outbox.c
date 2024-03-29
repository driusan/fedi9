#include <u.h>
#include <libc.h>
#include <json.h>
#include <stdio.h>
#include <bio.h>
#include <ndb.h>

#include "readfile.h"
#include "uuid.h"
#include "outbox.h"
#include "utils.h"


int openwebfs(int *fd) {
	char buf[1000];
	*fd = open("/mnt/web/clone", ORDWR);
	int n = read(*fd, buf, 1000);
	assert(n > 0);
	int con = atoi(buf);
	return con;
}

char* readbody(int conn) {
	char bodypath[2048];
	sprintf(bodypath, "/mnt/web/%d/body", conn);
	int fd = open(bodypath, OREAD);
	if (fd <= 0) {
		fprint(2, "could not open body for %d\n", conn);
	}
	char *data = readfile(fd);
	close(fd);
	return data;
	
}

JSON* getjson(int conn, char *url) {
	char webfspath[2048];
	int fd;
	sprintf(webfspath, "/mnt/web/%d/ctl", conn);
	fd = open(webfspath, OWRITE);
	fprint(fd, "url %s\n", url);
	fprint(fd, "headers Accept: application/ld+json;profile=\"https://www.w3.org/ns/activitystreams\"\n");
	char *body = readbody(conn);
	close(fd);
	
	JSON* data = jsonparse(body);
	return data;
}
static int idexists(Ndb *db, char *val) {
	Ndbs s;
	Ndbtuple *t = ndbsearch(db, &s, "id", val);
	return t != 0;
}


static void freenoteobject(NoteObject *n) {
	free(n->internaluuid);
	free(n->cachepath);
	free(n->id);
	free(n->attributedTo);
	free(n->inReplyTo);
	free(n->url);
	free(n->publishedTime);

	free(n);
}
static NoteObject* json2newnoteobject(JSON *data) {
	char path[1024];
	JSON *field;
	NoteObject *dst = malloc(sizeof(NoteObject));
	memset(dst, 0, sizeof(NoteObject));

	dst->internaluuid = newuuid();

	field = jsonbyname(data, "id");
	if (field == nil) {
		fprint(2, "No id for object:\n%J\n", data);
		freenoteobject(dst);
		// invalid note, so free it and return
		return nil;
	}
	dst->id = getfieldstr(data, "id");
	sprint(path, "objects/%U", *dst->internaluuid);
	dst->cachepath = strdup(path);

	dst->attributedTo = getfieldstr(data, "attributedTo");

	field = jsonbyname(data, "inReplyTo");
	if (field) {
		if (field->t == JSONString) {
			dst->nInReplyTo = 1;
			dst->inReplyTo = malloc(sizeof(char*));
			dst->inReplyTo[0] = strdup(field->s);
		} else if(field->t == JSONArray) {
			dst->nInReplyTo = 0;

			dst->nInReplyTo = field->n;
			for(JSONEl *cur = field->first; cur != nil; cur = cur->next) {
				assert(cur->val->t == JSONString);
				dst->nInReplyTo++;
				dst->inReplyTo = realloc(dst->inReplyTo, sizeof(char*)*dst->nInReplyTo);
				dst->inReplyTo[dst->nInReplyTo-1] = strdup(cur->val->s);
			}
			
		} else if(field->t == JSONNull) {
		} else {
			fprint(2, "Unhandled inReplyTo type %J\n", field);
			assert(0);
		}
	}
	dst->url = getfieldstr(data, "url");
	dst->publishedTime = getfieldstr(data, "published");
	return dst;
}

int Nfmt(Fmt *f) {
	int n;
	NoteObject* nt = va_arg(f->args, NoteObject*);
	n = fmtprint(f, "id=%s internaluuid=%U cachepath=%s\n", nt->id, *nt->internaluuid, nt->cachepath);
	if (n < 0) {
		return n;
	}
	if (nt->attributedTo != nil) {
		n += fmtprint(f, "\tattributedTo=%s\n", nt->attributedTo);
	}
	for(int i = 0; i < nt->nInReplyTo; i++){
		if (nt->inReplyTo[i] != nil) {
			n += fmtprint(f, "\tinReplyTo=%s\n", nt->inReplyTo[i]);
		}
	}
	if (nt->nInReplyTo == 0) {
		n += fmtprint(f, "\tinReplyTo=null\n");
	}
	if (nt->url != nil) {
		n += fmtprint(f, "\turl=%s\n", nt->url);
	}
	if (nt->publishedTime != nil) {
		n += fmtprint(f, "\tpublishedTime=%s\n", nt->publishedTime);
	}
	return n;
}

outboxRetrievalStats cachecollectionpage(JSON *page) {
	char abspath[1024];
	char objdb[1024];
	outboxRetrievalStats rv;

	JSON *orderedItems = jsonbyname(page, "orderedItems");
	JSON *id, *objtype;
	assert(orderedItems->t == JSONArray);
	JSON *object;
	int ofd;
	sprint(objdb, "%s/lib/fedi9/objects.db", getenv("home"));

	if (access(objdb, AEXIST) == 0) {
		// fprint(2, "Open %s", objdb);
		ofd = open(objdb, OWRITE);
		seek(ofd,0,2);
	} else {
	//	fprint(2, "Create %s\n", objdb);
		ofd = create(objdb, OWRITE, 0644);
	}
	memset(&rv, 0, sizeof(rv));
	Ndb* db = ndbopen(objdb);

	NoteObject* noteobject;
	for(JSONEl *cur = orderedItems->first; cur != nil; cur = cur->next) {
		object = jsonbyname(cur->val, "object");

		id = jsonbyname(object, "id");
		if (id == nil) {
			rv.nshares++;
			// fprint(2, "No id for object");
			// no id in the object, was probably an Announce
			continue;
		}
		if (idexists(db, id->s) == 0) {
			objtype = jsonbyname(object, "type");
			assert(objtype->t == JSONString);
			if (strcmp(objtype->s, "Note") == 0) {
				noteobject = json2newnoteobject(object);
				if (noteobject->nInReplyTo == 0) {
					rv.nposts++;
				} else {
					rv.nreplies++;
				}
				sprint(abspath, "%s/lib/fedi9/%s", getenv("home"), noteobject->cachepath);
				// create cache
				int fd = create(abspath, OWRITE, 0664);
				fprint(fd, "%J", object);
				close(fd);

				fprint(ofd, "%N\n", noteobject);

				ndbreopen(db);
				freenoteobject(noteobject);	
			} else {
				fprint(2, "Unhandled object type\n");
				continue;
			}

		} else {
			// fprint(2, "id %s already cached\n", id->s);
			ndbclose(db);
			close(ofd);
			return rv;
		}
	}
	ndbclose(db);
	close(ofd);
	return rv;
}
static outboxRetrievalStats cachecollection(int conn, JSON *root) {
	outboxRetrievalStats rv, pagestats;
//	print("getcollectionpage %d %J\n", conn, root);
	JSON *cur = jsonbyname(root, "first");
	JSON *items;

	memset(&rv, 0, sizeof(rv));
	while(cur != nil) {
		items = getjson(conn, cur->s);
		pagestats = cachecollectionpage(items);
		rv.nreplies += pagestats.nreplies;
		rv.nposts += pagestats.nposts;
		rv.nshares += pagestats.nshares;
		cur = jsonbyname(items, "next");
		free(items);

		if (pagestats.nreplies == 0 && pagestats.nposts == 0 && pagestats.nshares == 0) {
			return rv;
		}
	}
	return rv;
}


outboxRetrievalStats getoutbox(char *url) {
	int webfd;
	outboxRetrievalStats stats;
	memset(&stats, 0, sizeof(stats));

	int conN = openwebfs(&webfd);
	JSON *js = getjson(conN, url);

	JSON *field = jsonbyname(js, "type");
	if (field->t != JSONString || strcmp(field->s, "OrderedCollection") != 0) {
		return stats;
	}
	field = jsonbyname(js, "id");
	if (field->t != JSONString || strcmp(field->s, url) != 0) {
		return stats;
	}
	stats = cachecollection(conN, js);
	free(js);
	close(webfd);
	return stats;
}
