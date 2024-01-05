#include <u.h>
#include <libc.h>

#include <json.h>

#include <bio.h>
#include <ndb.h>

#include "readfile.h"
#include "fmt.h"
#include "utils.h"


Ndb* actordb;

void main(int argc, char *argv[]) {
	if(argc < 2){
		sysfatal("missing arg");
	}
	fmtinstalls();
	actordb=ndbopen("/usr/driusan/lib/fedi9/actors.db");
	char *file = argv[1];
	int fd = open(file, OREAD);
	if (fd <= 0) {
		sysfatal("No file");
	}
	char *data = readfile(fd);
	JSON *j = jsonparse(data);
	free(data);
	if (j == nil) {
		sysfatal("No json");
	}
	
	char *published = getfieldstr(j, "published");
	if(published == nil) {
		sysfatal("No published");
	}
	Tm t;
	tmfmtinstall();

	tmparse(&t, "YYYY-MM-DDThh:mm:ss", published, nil, nil);
	if (argc > 2 && strcmp(argv[2], "mboxname") == 0) {
		print("%ulld.00\n",tmnorm(&t));
		exits("");
	}
	JSON *attr = jsonbyname(j, "attributedTo");
	if (attr->t != JSONString){
		fprint(2, "Non-string attributed\n");
	}
	print("From %M %τ\n", getfieldstr(j, "attributedTo"), tmfmt(&t, nil));

	print("To: <%T>\n", jsonbyname(j, "to"));
	print("Date: %τ\n", tmfmt(&t, "WW, DD MMM YYYY hh:mm:ss Z"));
	print("From: <%M>\n", getfieldstr(j, "attributedTo"));
	print("Message-ID: <%s>\n", getfieldstr(j, "id"));
	char *subject = getfieldstr(j, "summary");
	if(subject != nil) {
		print("Subject: %s\n", subject);
	}
	print("Content-Type: text/html; charset=UTF8\n");

	print("\n%s\n", jsonbyname(j, "content")->s);
	jsonfree(j);
	close(fd);
	ndbclose(actordb);
	exits("");
}
