#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>
#include <json.h>
#include <bio.h>
#include <ndb.h>

#include "readfile.h"

typedef char* url_t;
typedef struct Person {
	url_t id;

	url_t inbox;
	url_t outbox;
	url_t following;
	url_t followers;

	char *name;
	char *preferredUsername;
	char *uuidstr;

} Person;

typedef struct {
	enum{
		outbox,
		inbox,
		following,
		followers,
		name,
		preferredUsername,
		id,
		PostFile
	} filetype;
	union {
		Person *p;
		struct {
			char *filename;
			char *cachefile;
			Person *p;
		} post;
	};
} AuxData;

#define READACTORFILETYPE(type) case type: \
	readstr(r, a->p->type); \
	respond(r, nil); \
	return;

static void postrespfromcache(Req *r, char *abspath, char *fieldname) {
	JSON *json, *field;
	char *s;

	// Parse the json and then free the intermediary stuff
	int fd = open(abspath, OREAD);
	s = readfile(fd);
	close(fd);
	json = jsonparse(s);
	free(s);
	if (json == nil) {
		respond(r, "no json");
		return;
	}

	// get the field
	field = jsonbyname(json, fieldname);
	if (field == nil) {
		free(json);
		respond(r, "json missing field");
		return;
	}

	int slen = strlen(field->s);
	// after eof, return 0
	if (r->ifcall.offset >= slen) {
		free(json);
		r->ofcall.data = 0;
		r->ofcall.count = 0;
		respond(r, nil);
		return;
	}
	// Return the value
	assert(field->t == JSONString);
	r->ofcall.data = strdup(field->s);
	r->ofcall.count = strlen(field->s);
	free(json);
	respond(r, nil);
	return;
}

void fsread(Req *r){
	AuxData *a = r->fid->file->aux;
	if (a == nil) {
		respond(r, "internal error");
		return;
	}
	switch (a->filetype) {
	READACTORFILETYPE(preferredUsername)
	READACTORFILETYPE(name)
	READACTORFILETYPE(id)
	READACTORFILETYPE(inbox)
	READACTORFILETYPE(outbox)
	READACTORFILETYPE(following)
	READACTORFILETYPE(followers)
	case PostFile: {
		char path[1024];
		// cachefile is relative to $home/lib/fedi9 so make it absolute
		sprint(path, "%s/lib/fedi9/%s", getenv("home"), a->post.cachefile);
		if (strcmp(a->post.filename, "raw") == 0) {
			// pass through to cachefile using pread
			int fd = open(path, OREAD);
			r->ofcall.data = malloc(r->ifcall.count);
		
			int n = pread(fd, r->ofcall.data, r->ifcall.count, r->ifcall.offset);
			close(fd);
			if (n < 0) {
				char errresp[1024];
				errstr(errresp, 1024);
				free(r->ofcall.data);
				respond(r, strdup(errresp));
				return;
			} else {
				r->ofcall.count = n;
			}
			respond(r, nil);
			return;
		} else if (strcmp(a->post.filename, "url") == 0){
			postrespfromcache(r, path, "url");
			return;
		} else if (strcmp(a->post.filename, "content") == 0) {
			postrespfromcache(r, path, "content");
			return;
		} else {
			fprint(2, "Unhandled post file type %s\n", a->post.filename);
			assert(0);
		}
	}
	}

	respond(r, "bad file type");
	return;
}

Srv fs = {
	.read = fsread,
};

void usage(void) {
	fprint(2, "usage: %s -f following.db\n", argv0);
	exits("usage");
}
void ndb2person(Ndbtuple *src, Person *dst) {
	memset(dst, 0, sizeof(Person));
	
	Ndbtuple *cur = src;
	while (cur != nil) {
		if(strcmp(cur->attr, "id")==0) {
			assert(dst->id == 0);
			dst->id = strdup(cur->val);
		} else if (strcmp(cur->attr, "inbox") == 0) {
			assert(dst->inbox == 0);
			dst->inbox = strdup(cur->val);
		} else if (strcmp(cur->attr, "outbox") == 0) {
			assert(dst->outbox == 0);
			dst->outbox = strdup(cur->val);
		} else if (strcmp(cur->attr, "following") == 0) {
			assert(dst->following == 0);
			dst->following = strdup(cur->val);
		} else if (strcmp(cur->attr, "followers") == 0) {
			assert(dst->followers == 0);
			dst->followers = strdup(cur->val);
		} else if (strcmp(cur->attr, "name") == 0) {
			assert(dst->name == 0);
			dst->name = strdup(cur->val);
		} else if (strcmp(cur->attr, "preferredUsername") == 0) {
			assert(dst->preferredUsername == 0);
			dst->preferredUsername = strdup(cur->val);
		} else if (strcmp(cur->attr, "uuid") == 0) {
			assert(dst->uuidstr == 0);
			dst->uuidstr = strdup(cur->val);
		}
		cur = cur->entry;
	}

}

void printperson(Person p) {
	print("%s %s %s %s",p.id, p.outbox, p.name, p.uuidstr);
	
}

static char* geturlhostname(char *url) {
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

char* friendlyName(Person *p) {
	char *hostname = geturlhostname(p->id);
	char *s;

	// +3 = 2 @s and a \0
	s = malloc(strlen(hostname) + strlen(p->preferredUsername) + 3);
	sprint(s, "@%s@%s\0", p->preferredUsername, hostname);
	return s;	
}

static char* getfirstattr(Ndbtuple *line, char *attr) {
	Ndbtuple *cur = line;
	while (cur != nil) {
		if(strcmp(cur->attr, attr)==0) {
			return cur->val;
		}
		cur = cur->entry;
	}
	return nil;
	
}

static JSON* getjsonfromcache(char *cachepath) {
	JSON *data;
	char abspath[1024];
	char *s;
	sprint(abspath, "%s/lib/fedi9/%s", getenv("home"), cachepath);
	int fd = open(abspath, OREAD);
	s = readfile(fd);
	close(fd);
	data = jsonparse(s);
	free(s);
	return data;

}
static void createpostsdir(Ndb *db, File *dir, Person *p) {
	Ndbs s;
	assert(p != nil && dir != nil && db != nil);

	Ndbtuple *cur = ndbsearch(db, &s, "attributedTo", p->id);
	
	char *postname;
	File *postdir;
	while(cur != nil){
		// only top level posts go in the posts directory
		if (strcmp(getfirstattr(cur, "inReplyTo"), "null") != 0) {
			cur = ndbsnext(&s, "attributedTo", p->id);
			continue;
		}
		postname = getfirstattr(cur, "publishedTime");
		assert(postname != nil);
		postdir = createfile(dir, postname, nil, DMDIR|0555, nil);

		char *cachefile = getfirstattr(cur, "cachepath");
		{
			AuxData *ax = malloc(sizeof(AuxData));
			ax->filetype = PostFile;
			ax->post.cachefile = cachefile;
			ax->post.filename = "raw";
			ax->post.p = p;
			
			createfile(postdir, "raw", nil, 0444, ax);
		}

		
		JSON *jsondata = getjsonfromcache(cachefile);
		if (jsondata == nil) {
			fprint(2, "could not parse json for %s (%s)\n", cachefile, postname);
			cur = ndbsnext(&s, "attributedTo", p->id);
			continue;
		}
	
		if (jsonbyname(jsondata, "url") != nil) {
			AuxData *ax = malloc(sizeof(AuxData));
			ax->filetype = PostFile;
			ax->post.cachefile = cachefile;
			ax->post.filename = "url";
			ax->post.p = p;
			
			createfile(postdir, "url", nil, 0444, ax);
		}
		if (jsonbyname(jsondata, "content") != nil) {
			AuxData *ax = malloc(sizeof(AuxData));
			ax->filetype = PostFile;
			ax->post.cachefile = cachefile;
			ax->post.filename = "content";
			ax->post.p = p;
			
			createfile(postdir, "content", nil, 0444, ax);
		}
		if (jsonbyname(jsondata, "summary") != nil) {
			AuxData *ax = malloc(sizeof(AuxData));
			ax->filetype = PostFile;
			ax->post.cachefile = cachefile;
			ax->post.filename = "summary";
			ax->post.p = p;
			
			createfile(postdir, "summary", nil, 0444, ax);
		}
		cur = ndbsnext(&s, "attributedTo", p->id);
	}
}

#define MKFILETYPE(type) { \
	AuxData *ax = malloc(sizeof(AuxData)); \
	ax->filetype = type; \
	ax->p = p; \
	createfile(f, "type", nil, 0444, ax); \
	}

void createpeopletree(Ndb *db, Ndb *objectdb, Tree *t) {
	Ndbs s;
	Person *p;
	Ndbtuple *cur = ndbsearch(db, &s, "type", "Person");
	while(cur != nil) {
		p = malloc(sizeof(Person));
		ndb2person(cur, p);
		
		File *f = createfile(t->root, friendlyName(p), nil, DMDIR|0555, nil);
		if (f != nil) {
			MKFILETYPE(preferredUsername)
			MKFILETYPE(name)
			MKFILETYPE(id)
			MKFILETYPE(outbox)
			MKFILETYPE(inbox)
			MKFILETYPE(following)
			MKFILETYPE(followers)

			File *pdir = createfile(f, "posts", nil, DMDIR|0555, nil);
			createpostsdir(objectdb, pdir, p);
			// FIXME: create files for each of their posts.
			
		}
		cur = ndbsnext(&s, "type", "Person");
	}
	assert(cur==nil);
}

static char* getdefaultdb(char *type) {
	char path[1024];
	sprint(path, "%s/lib/fedi9/%s.db", getenv("home"), type);
	return strdup(path);
}
void main(int argc, char *argv[]) {
	char *actordbfile = getdefaultdb("following");
	char *objectdbfile = getdefaultdb("objects");
	ARGBEGIN{
	case 'f':
		actordbfile = EARGF(usage());
		break;
	case 'o':
		objectdbfile = EARGF(usage());
	case 'D':
		chatty9p++;
		break;
	}ARGEND;

	if (actordbfile == nil) {
		usage();
	}
	Ndb *actordb = ndbopen(actordbfile);
	if (actordb == nil) {
		fprint(2, "Could not open %s", actordbfile);
		exits("no actor db");
	}
	Ndb *objectdb = ndbopen(objectdbfile);
	if (objectdb == nil) {
		fprint(2, "Could not open %s", objectdbfile);
		exits("no object db");
	}
	Tree *t;
	t = alloctree(nil, nil, DMDIR|0555, nil);
	fs.tree = t;
	createpeopletree(actordb, objectdb, t);
	postmountsrv(&fs, nil, "/mnt/fedi9", MREPL | MCREATE);	
}
