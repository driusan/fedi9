#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>
#include <json.h>
#include <bio.h>
#include <ndb.h>

#include "readfile.h"
#include "uuid.h"
#include "outbox.h"
#include "removedir.h"
#include "utils.h"

Ndb *actordb;
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

static void createpostsdir(Ndb *db, File *dir, Person *p);


typedef struct {
	enum{
		ActorFile,
		PostFile
	} filetype;
	char *filename;
	union {
		struct {
			Person *p;
			Reqqueue *ctlqueue;
			File *fstreeroot;
			Ndb* objectsdb;		
		} actor;
		struct {
			char *cachefile;
			Person *p;
			int ncontentfmt;
			char *contentfmt;
		} post;

	};
} AuxData;

static void htmlfmt(AuxData *dst, char *val) {
	// Waitmsg *w;
	int c2p[2];
	int p2c[2];
	if (pipe(c2p) < 0) {
		exits("pipe failed");
	}
	if (pipe(p2c) < 0) {
		exits("pipe failed");
	}
	switch(fork()){
	case -1: exits("fork failed");
	case 0:
		dup(p2c[0], 0);
		dup(c2p[0], 1);

		close(p2c[0]);
		close(p2c[1]);
		close(c2p[0]);
		close(c2p[1]);

		execl("/bin/htmlfmt", "htmlfmt", nil);
	default:
	 	fprint(p2c[1], "%s", val);
		close(p2c[0]);
		close(p2c[1]);
		close(c2p[0]);		
		dst->post.contentfmt = readfile(c2p[1]);

		close(c2p[1]);
		wait();
	}
}
#define BEGINREADACTORFILE if(0) {

#define READACTORFILETYPE(type) } else if (strcmp(a->filename, "type") == 0) { \
	readstr(r, a->actor.p->type); \
	respond(r, nil); \
	return;

#define ENDREADACTORFILE } else { \
	fprint(2, "Unhandled actor file type %s\n", a->filename); \
	assert(0); \
	}

static void htmlfmtcontentresp(Req *r, char *abspath, AuxData *dst){
	if (dst->post.contentfmt == nil) {
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
		field = jsonbyname(json, "content");
		if (field == nil) {
			free(json);
			respond(r, "json missing content");
			return;
		}
		if(field->t != JSONString){
			free(json);
			respond(r, "content not string");
		}
		htmlfmt(dst, field->s);
	}
	// htmlfmt should have populated it above, otherwise it should already
	// be cached.
	assert(dst->post.contentfmt != nil);
	readstr(r, dst->post.contentfmt);
	respond(r, nil);
}
static void postrespfromcache(Req *r, char *abspath, char *fieldname) {
	JSON *json, *field;
	char *s;

	// Parse the json and then free the intermediary stuff
	int fd = open(abspath, OREAD);
	s = readfile(fd);
	close(fd);
	json = jsonparse(s);
	if (strcmp(fieldname, "content") == 0)	fprint(2, "s: %s\n", s);
	free(s);
	if (json == nil) {
		respond(r, "no json");
		return;
	}
	if (strcmp(fieldname, "content") == 0)	fprint(2, "JSON: %J\n", json);
	// get the field
	field = jsonbyname(json, fieldname);
	if (field == nil) {
		jsonfree(json);
		respond(r, "json missing field");
		return;
	}
	if (strcmp(fieldname, "content") == 0)	fprint(2, "Field: %J\n\n", field);
	int slen = strlen(field->s);
	// fprint(2, "Offset %lld slen: %d count %d  %J\n", r->ifcall.offset, slen, r->ifcall.count, field);
	// after eof, return 0
	if (r->ifcall.offset >= slen) {
		jsonfree(json);
		r->ofcall.data = 0;
		r->ofcall.count = 0;
		respond(r, nil);
		return;
	}
	// Return the value
	assert(field->t == JSONString);
	r->ofcall.data = strdup(field->s);
	r->ofcall.count = strlen(field->s);
	jsonfree(json);
	respond(r, nil);
	return;
}
extern int debug;
void fsread(Req *r){
	AuxData *a = r->fid->file->aux;
	if (a == nil) {
		respond(r, "internal error");
		return;
	}
	switch (a->filetype) {
	case ActorFile: {
		if (strcmp(a->filename, "ctl") == 0) {
			// special file, not in the struct.
			// reading from it currently does nothing
			// because we can't write to it yet.
			r->ofcall.data = 0;
			r->ofcall.count = 0;
			respond(r, nil);
			return;
		}

		// files that just pass through to the person struct
		BEGINREADACTORFILE
		READACTORFILETYPE(preferredUsername)
		READACTORFILETYPE(name)
		READACTORFILETYPE(id)
		READACTORFILETYPE(inbox)
		READACTORFILETYPE(outbox)
		READACTORFILETYPE(following)
		READACTORFILETYPE(followers)
		ENDREADACTORFILE
	}
	case PostFile: {
		char path[1024];
		// cachefile is relative to $home/lib/fedi9 so make it absolute
		sprint(path, "%s/lib/fedi9/%s", getenv("home"), a->post.cachefile);
		// fprint(2, "path: %s\n", path);
		if (strcmp(a->filename, "raw") == 0) {
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
		} else if (strcmp(a->filename, "id") == 0){
			postrespfromcache(r, path, "id");
			return;
		} else if (strcmp(a->filename, "url") == 0){
			postrespfromcache(r, path, "url");
			return;
		} else if (strcmp(a->filename, "content.raw") == 0) {
			postrespfromcache(r, path, "content");
			return;
		} else if (strcmp(a->filename, "content") == 0) {
			htmlfmtcontentresp(r, path, a);
			return;
		} else if (strcmp(a->filename, "inReplyTo") == 0) {
			postrespfromcache(r, path, "inReplyTo");
			return;
		} else if (strcmp(a->filename, "summary") == 0) {
			postrespfromcache(r, path, "summary");
			return;
		} else {
			fprint(2, "Unhandled post file type %s\n", a->filename);
			assert(0);
		}
	}
	}

	respond(r, "bad file type");
	return;
}

void actorctlwrite(Req *r) {
	AuxData *a = r->fid->file->aux;
	if (a == nil) {
		respond(r, "internal error");
		return;
	}
	incref(a->actor.fstreeroot);
	if (strncmp(r->ifcall.data, "refresh", 7) == 0) {
		// get the outbox
		outboxRetrievalStats stats = getoutbox(a->actor.p->outbox);
		if (stats.nposts > 0) {
			// Remove the old posts tree
			File *pdir = walkfile(a->actor.fstreeroot, "posts");
	
			if (pdir == nil) {
				respond(r, "no posts");
				return;
			}
			incref(pdir);
			removedir(pdir, 0);

			// create a new posts tree
			createpostsdir(a->actor.objectsdb, pdir, a->actor.p);
			respond(r, nil);
			return;
		}
		respond(r, nil);
	} else if(strncmp(r->ifcall.data, "destroy", 7) == 0) {
		File *pdir = walkfile(a->actor.fstreeroot, "posts");
	
		if (pdir == nil) {
			respond(r, "no posts");
			return;
		}
		incref(pdir);
		fprint(2, "removedir posts\n");
		removedir(pdir, 1);
		respond(r, "done destroy");
	} else {
		respond(r, "bad ctl command");
	}
}
void fswrite(Req *r){
	AuxData *a = r->fid->file->aux;
	if (a == nil) {
		respond(r, "internal error");
		return;
	}
	if (a->filetype != ActorFile) {
		respond(r, "write prohibited");
		return;
	}
	if (strcmp(a->filename, "ctl") != 0) {
		respond(r, "write prohibited");
		return;
	}
	if (a->actor.ctlqueue == nil){
		a->actor.ctlqueue = reqqueuecreate();
	}	
	reqqueuepush(a->actor.ctlqueue, r, actorctlwrite);

}
Srv fs = {
	.read = fsread,
	.write = fswrite,
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

static void createpostdir(File *parentdir, char* postname, Ndbtuple *cur, Person *p) {
	AuxData *ax;
	incref(parentdir);
	File *postdir = createfile(parentdir, postname, nil, DMDIR|0555, nil);
	if (postdir == nil) {
		fprint(2, "Could not create %s\n", postname);
		return;
	}
	char *cachefile = strdup(getfirstattr(cur, "cachepath"));
	assert(cachefile != nil);

	ax = malloc(sizeof(AuxData));
	ax->filetype = PostFile;
	ax->post.cachefile = cachefile;
	ax->filename = "raw";
	ax->post.p = p;
	createfile(postdir, "raw", nil, 0444, ax);

	JSON *jsondata = getjsonfromcache(cachefile);
	if (jsondata == nil) {
		fprint(2, "could not parse json for %s (%s)\n", cachefile, postname);
		return;
	}
	if (jsonbyname(jsondata, "id") != nil) {
		ax = malloc(sizeof(AuxData));
		ax->filetype = PostFile;
		ax->post.cachefile = cachefile;
		ax->filename = "id";
		ax->post.p = p;

		createfile(postdir, "id", nil, 0444, ax);
	}
	if (jsonbyname(jsondata, "url") != nil) {
		ax = malloc(sizeof(AuxData));
		ax->filetype = PostFile;
		ax->post.cachefile = cachefile;
		ax->filename = "url";
		ax->post.p = p;

		createfile(postdir, "url", nil, 0444, ax);
	}
	
	if (jsonbyname(jsondata, "content") != nil) {
		ax = malloc(sizeof(AuxData));
		ax->filetype = PostFile;
		ax->post.cachefile = cachefile;
		ax->filename = "content";
		ax->post.p = p;
		ax->post.contentfmt = nil;

		createfile(postdir, "content", nil, 0444, ax);

		ax = malloc(sizeof(AuxData));
		ax->filetype = PostFile;
		ax->post.cachefile = cachefile;
		ax->filename = "content.raw";
		ax->post.p = p;
		ax->post.contentfmt = nil;
		createfile(postdir, "content.raw", nil, 0444, ax);
	}

	if (jsonbyname(jsondata, "summary") != nil) {
		ax = malloc(sizeof(AuxData));
		ax->filetype = PostFile;
		ax->post.cachefile = cachefile;
		ax->filename = "summary";
		ax->post.p = p;

		createfile(postdir, "summary", nil, 0444, ax);
	}
	if (jsonbyname(jsondata, "inReplyTo") != nil) {
		ax = malloc(sizeof(AuxData));
		ax->filetype = PostFile;
		ax->post.cachefile = cachefile;
		ax->filename = "inReplyTo";
		ax->post.p = p;

		createfile(postdir, "inReplyTo", nil, 0444, ax);
	}
}

static void createpostsdir(Ndb *db, File *dir, Person *p) {
	Ndbs s;
	incref(dir);
	assert(p != nil && dir != nil && db != nil);

	// fprint(2, "Create posts dir for %s\n", p->id);
	if (ndbchanged(db)) {
		ndbreopen(db);
	}
	Ndbtuple *cur = ndbsearch(db, &s, "attributedTo", p->id);
	
	char *postname;
	char datestr[11];
	File *dateDir = nil;
	for(;cur != nil;ndbfree(cur), cur = ndbsnext(&s, "attributedTo", p->id)){
		postname = getfirstattr(cur, "publishedTime");

		assert(postname != nil);
		
		if (strncmp(datestr, postname, 10) != 0) {
			memset(datestr, 0, 11);
			strncpy(datestr, postname, 10);
			dateDir = createfile(dir, datestr, nil, DMDIR|0555, nil);
			if (dateDir == nil){
				// file might have already existed if dates were
				// out of order in db, try to walk..
				dateDir = walkfile(dir, datestr);
			}
		}
		assert(dateDir != nil);
		
		createpostdir(dateDir, postname, cur, p);
	}
}

#define MKFILETYPE(type, perms, filetree, _objectsdb) { \
	AuxData *ax = malloc(sizeof(AuxData)); \
	ax->filetype = ActorFile; \
	ax->filename = "type"; \
	ax->actor.p = p; \
	ax->actor.fstreeroot = filetree; \
	incref(filetree); \
	ax->actor.ctlqueue = nil; \
	ax->actor.objectsdb = _objectsdb; \
	createfile(f, "type", nil, perms, ax); \
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
			MKFILETYPE(preferredUsername, 0444, f, objectdb)
			MKFILETYPE(name, 0444, f, objectdb)
			MKFILETYPE(id, 0444, f, objectdb)
			MKFILETYPE(outbox, 0444, f, objectdb)
			MKFILETYPE(inbox, 0444, f, objectdb)
			MKFILETYPE(following, 0444, f, objectdb)
			MKFILETYPE(followers, 0444, f, objectdb)
			MKFILETYPE(ctl, 0660, f, objectdb)
			File *pdir = createfile(f, "posts", nil, DMDIR|0555, nil);
			createpostsdir(objectdb, pdir, p);
			
		}
		ndbfree(cur);
		cur = ndbsnext(&s, "type", "Person");
	}
	assert(cur==nil);
}

static char* getdefaultdb(char *type) {
	char path[1024];
	sprint(path, "%s/lib/fedi9/%s.db", getenv("home"), type);
	return strdup(path);
}
void destroyfile(File *f) {
	AuxData *a = f->aux;
	free(a);
	fprint(2, "In destroy %s\n", f->Dir.name);
}
extern int chatty9p;
void threadmain(int argc, char *argv[]) {
	char *actordbfile = getdefaultdb("actors");
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
	fmtinstall('U', Ufmt);
	fmtinstall('N', Nfmt);
	JSONfmtinstall();
	actordb = ndbopen(actordbfile);
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
	t = alloctree(nil, nil, DMDIR|0555, destroyfile);
	fs.tree = t;
	createpeopletree(actordb, objectdb, t);
	threadpostmountsrv(&fs, nil, "/mnt/fedi9", MREPL | MCREATE);	
}
