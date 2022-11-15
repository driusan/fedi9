#include <u.h>
#include <libc.h>
#include <json.h>
#include <stdio.h>
#include <bio.h>
#include <ndb.h>

#include "readfile.h"
#include "uuid.h"

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
	char *data = readfile(fd);
	close(fd);
	return data;
	
}

JSON* getjson(int conn, char *url) {
	char webfspath[2048];
	int fd;
	sprintf(webfspath, "/mnt/web/%d/ctl", conn);
	print("getjson webfs: %s\n", webfspath);
	fd = open(webfspath, OWRITE);
	print("Opening %s\n", webfspath);
	fprint(fd, "url %s\n", url);
	fprint(fd, "headers Accept: application/ld+json;profile=\"https://www.w3.org/ns/activitystreams\"\n");
	
	char *body = readbody(conn);

	close(fd);
	JSON* json = jsonparse(body);
	free(body);
	return json;
}
static int idexists(Ndb *db, char *val) {
	Ndbs s;
	Ndbtuple *t = ndbsearch(db, &s, "id", val);
	return t != 0;
}
void cachecollectionpage(JSON *page) {
	char path[1024];
	char abspath[1024];
	char objdb[1024];
	JSON *orderedItems = jsonbyname(page, "orderedItems");
	JSON *id;
	assert(orderedItems->t == JSONArray);
	uuid_t *u;
	JSON *object;
	int ofd;
	sprint(objdb, "%s/lib/fedi9/objects.db", getenv("home"));
	if (access(objdb, AEXIST) == 0) {
		fprint(2, "Open %s", objdb);
		ofd = open(objdb, OWRITE);
		seek(ofd,0,2);
	} else {
		fprint(2, "Create %s\n", objdb);
		ofd = create(objdb, OWRITE, 0644);
	}
	print("I am about to open db\n");
	Ndb* db = ndbopen(objdb);
	
	print("Iterating through items\n");
	for(JSONEl *cur = orderedItems->first; cur != nil; cur = cur->next) {
		object = jsonbyname(cur->val, "object");


		id = jsonbyname(object, "id");
		if (id == nil) {
			fprint(2, "No id for object");
			// no id in the object, was probably an Announce
			continue;
		}
		if (idexists(db, id->s) == 0) {
			u = newuuid();
			sprint(path, "objects/%U", *u);
			sprint(abspath, "%s/lib/fedi9/%s", getenv("home"), path);
			// id=xx uuid=xx path=xxxx >> $home/lib/fedi9/objects.db
			fprint(ofd, "id=%s uuid=%U path=%s\n", id->s, *u, path);

			// create cache
			int fd = create(abspath, OWRITE, 0664);
			fprint(fd, "%J", object);
			close(fd);
			ndbreopen(db);
			free(u);
		} else {
			fprint(2, "id %s already cached\n", id->s);
		}
	}
	ndbclose(db);
	close(ofd);
}
void cachecollection(int conn, JSON *root) {
	print("getcollectionpage %d %J\n", conn, root);
	JSON *cur = jsonbyname(root, "first");
	JSON *items;
	
	while(cur != nil) {
		items = getjson(conn, cur->s);
		cachecollectionpage(items);
		cur = jsonbyname(items, "next");
	//	free(items);
	}
}
void main(int argc, char *argv[]) {
	int webfd;
	if (argc != 2) {
		fprint(2, "usage: %s outboxURL\n", argv[0]);
		exits("usage");
	}
	fmtinstall('U', Ufmt);
	JSONfmtinstall();

	int conN = openwebfs(&webfd);
	print("ConN: %d\n", conN);
	JSON *js = getjson(conN, argv[1]);

	

	JSON *field = jsonbyname(js, "type");
	// verify type=OrderedCollection
	if (field->t != JSONString || strcmp(field->s, "OrderedCollection") != 0) {
		exits("bad outbox");
	}
	// verify id=argv[1]
	field = jsonbyname(js, "id");
	if (field->t != JSONString || strcmp(field->s, argv[1]) != 0) {
		exits("id mismatch");
	}
	print("conN 2: %d\n", conN);
	cachecollection(conN, js);
	// iterate through pages until we've seen something
	
	print("%J\n", js);
	free(js);
	close(webfd);
	exits("");
	/*
	char *buf = readfile(0);
	JSON* root = jsonparse(buf);
	if (root == nil) {
		exits("badjson");
	}
	JSON* field = jsonbyname(root, argv[1]);
	if (field == nil) {
		exits("no field");
	}

	JSONfmtinstall();
	if (field->t == JSONString) {
		// don't use %J to avoid quotation marks
		print("%s", field->s);
	} else if (field->t == JSONArray) {
		for(JSONEl *cur = field->first; cur != nil; cur = cur->next) {
			if (cur->val->t == JSONString) {
				print("%s\n", cur->val->s);
			} else {
				print("%J\n", cur->val);
			}
		
		}
	} else {
	
		print("%J\n", field);
	}
	free(field);
	free(root);
	exits("");
*/
}
