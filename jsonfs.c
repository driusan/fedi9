#include <u.h>
#include <libc.h>

#include <fcall.h>
#include <thread.h>
#include <9p.h>

#include <json.h>

#include "removedir.h"

#include <bio.h>
#include <ndb.h>

Ndb* actordb;

typedef struct{
	enum{JsonType, JsonValue, JsonRaw, CtlFile} filetype;
	union {
		JSON *js;
		struct {
			int nbuf;
			char *buf;
		};
	};
} AuxData;

void recreateroot(JSON *js);
void createjsonfiles(JSON *js, File *root);

void fsread(Req *r){
	AuxData *a = r->fid->file->aux;
	if (a == nil){
		respond(r, "no aux data");
		return;	
	}
	switch(a->filetype){
	case JsonRaw:
		{
		char *str = smprint("%J", a->js);
		readstr(r, str);
		free(str);
		respond(r, nil);
		return;
		}
	case JsonType:
		switch(a->js->t){
		case JSONNull:
			readstr(r, "null");
			break;
		case JSONBool:
			readstr(r, "bool");
			break;
		case JSONNumber:
			readstr(r, "number");
			break;
		case JSONString:
			readstr(r, "string");
			break;
		case JSONArray:
			readstr(r, "array");
			break;
		case JSONObject:
			readstr(r, "object");
			break;
		default:
			respond(r, "invalid json type");
			return;
		}
		respond(r, nil);
		return;
	case JsonValue:
		switch(a->js->t){
		case JSONNull:
		case JSONArray:
		case JSONObject:
			// null shouldn't have a value file, array
			// and object should have values directory
			respond(r, "internal error");
			return;
		case JSONBool:
			if (a->js->n){
				readstr(r, "true");
			} else{
				readstr(r, "false");
			}
			respond(r, nil);
			return;
		case JSONNumber:
			{
			char *tmp = smprint("%f", a->js->n);
			readstr(r, tmp);
			free(tmp);
			respond(r, nil);
			return;
			}
		case JSONString:
			readstr(r, a->js->s);
			respond(r, nil);
			return;
		}
	}
	respond(r, "read not implemented");
	return;
}

void fswrite(Req *r){
	AuxData *a = r->fid->file->aux;
	if (a == nil){
		r->ofcall.count = 0;
		respond(r, "bad write");
		return;	
	}
	if(a->filetype != CtlFile ){
		r->ofcall.count = 0;
		respond(r, "not ctl file");
		return;	
	}
	JSON *js;
	if (r->ifcall.offset == 0){
		js = jsonparse(r->ifcall.data);
	} else {
		a->buf = realloc(a->buf, a->nbuf+r->ifcall.count);
		memmove(&a->buf[a->nbuf], r->ifcall.data, r->ifcall.count);
		js = jsonparse(a->buf);
	}

	if (js == nil) {
		// We want large files to work. cat uses 8192 byte chunks, but
		// fedi9/get uses arbitrary sizes depending on what came in over
		// the wire depending on what webfs sends it. We just claim the
		// write succeeded and add it to a buffer in aux, assuming another
		// write will come in that makes it valid json.
		a->buf = realloc(a->buf, a->nbuf+r->ifcall.count);
		memmove(&a->buf[a->nbuf], r->ifcall.data, r->ifcall.count);
		a->nbuf += r->ifcall.count;
		r->ofcall.count = r->ifcall.count;
		respond(r, nil);
		return;
	}
	recreateroot(js);
	r->ofcall.count = r->ifcall.count;
	respond(r, nil);
}

Srv fs = {
	.read = fsread,
	.write = fswrite,
};

void destroyfile(File *f) {
	AuxData *a = f->aux;
	if (a == nil){
		return;
	}
	if (a->filetype == CtlFile){
		free(a->buf);
	}
	free(f->aux);
}
void recreateroot(JSON *js){
	if (fs.tree == nil) {
		fs.tree = alloctree(nil, nil, DMDIR|0555, destroyfile);
	} else{
		removedir(fs.tree->root, 0);
	}
	AuxData *a = malloc(sizeof(AuxData));
	a->filetype = CtlFile;
	a->buf = nil;
	a->nbuf = 0;
	createfile(fs.tree->root, "ctl", nil, 0660, a);
	createjsonfiles(js, fs.tree->root);
}

void createjsonfiles(JSON *js, File *root) {
	File *values;
	incref(root);
	AuxData *a = malloc(sizeof(AuxData));
	a->filetype = JsonType;
	a->js = js;
	incref(root);
	createfile(root, "jsontype", nil, 0444, a);

	
	AuxData *b = malloc(sizeof(AuxData));
	b->filetype = JsonRaw;
	b->js = js;
	incref(root);

	incref(root);
	createfile(root, "raw", nil, 0444, b);

	switch(js->t) {
	case JSONNull: break;
	case JSONBool:
		a =  malloc(sizeof(AuxData));
		a->filetype = JsonValue;
		a->js = js;
		createfile(root, "value", nil, 0444, a);
		break;
	case JSONNumber:
		a =  malloc(sizeof(AuxData));
		a->filetype = JsonValue;
		a->js = js;
		createfile(root, "value", nil, 0444, a);
		break;
	case JSONString:
		a =  malloc(sizeof(AuxData));
		a->filetype = JsonValue;
		a->js = js;
		createfile(root, "value", nil, 0444, a);
		break;
	case JSONArray:
		values = createfile(root, "values", nil, DMDIR|0555, nil);
		int i=0;
		for(JSONEl *cur = js->first; cur != nil; cur = cur->next){
			File *f = createfile(values, smprint("%d", i), nil, DMDIR|0555, nil);
			createjsonfiles(cur->val, f);
			i++;
		}
		break;
	case JSONObject:	
		values = createfile(root, "values", nil, DMDIR|0555, nil);
		for(JSONEl *cur = js->first; cur != nil; cur = cur->next){
			File *f = createfile(values, cur->name, nil, DMDIR|0555, nil);
			createjsonfiles(cur->val, f);
		}
	}	
}

void main(void){
	char *tmp = "{}";
	JSONfmtinstall();
	JSON *js = jsonparse(tmp);
	recreateroot(js);

	postmountsrv(&fs, nil, "/mnt/json", MREPL | MCREATE);
}
