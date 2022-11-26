#include <u.h>
#include <libc.h>
#include <json.h>

#include "readfile.h"

void main(int argc, char *argv[]) {
	if (argc != 2) {
		fprint(2, "usage: %s keyname\n", argv[0]);
		exits("usage");
	}
	char *buf = readfile(0);
	JSON* root = jsonparse(buf);
	if (root == nil) {
		exits("badjson");
	}
	JSON* field = jsonbyname(root, argv[1]);
	if (field == nil) {
		exits("no field");
	}
	char *type = getenv("jsontype");
	if (type != nil && strcmp(type, "string") == 0) {
		if (field->t != JSONString) {
			exits("unmatched type");
		}
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
}
