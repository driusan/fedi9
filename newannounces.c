/* #!/bin/rc

. /bin/fedi9/rclib
echo 'Hashing'
echo 'Getting'
objects=`{ndb/query -f $home/lib/fedi9/announces.db -a type Announce object}
echo 'Looping' $#objects
ndb/mkhash $deaddb id
ndb/mkhash $objectsdb id
for(object in $objects){
	if (! ~ `{ndb/query -f $deaddb id $"object} ''){
		# echo Skipping $"object
	}
	if not {
		if (~ `{ndb/query -f $objectsdb id $"object} ''){
			echo Getting $"object
			fedi9/addObjectToDb -g $"object
		}
		if not {
			# echo Already processed $"object
		}
	}
}
rm $objectsdb.id
rm $deaddb.id
*/
#include <u.h>
#include <libc.h>

#include <bio.h>
#include <ndb.h>

Ndb* getdb(char *name){
	char *fullname = smprint("%s/lib/fedi9/%s.db", getenv("home"), name);
	Ndb* db = ndbopen(fullname);
	free(fullname);
	return db;
}
int checkcontains(Ndb *db, char *name){
	Ndbtuple *entry;
	Ndbs s;
	if (db == nil){
		return 0;
	}
	// fprint(2, "Checking for %s\n", name);
	entry = ndbsearch(db, &s, "id", name);
	if (entry == nil){
		return 0;
	}
	ndbfree(entry);
	return 1;
}
void loopannounces(Ndb* announcesdb, Ndb* objects, Ndb* deaddb) {
	Ndbtuple *entry, *object;
	Ndbs s;
	entry = ndbsearch(announcesdb, &s, "type", "Announce");
	while(entry != nil){
		object = ndbfindattr(entry, nil, "object");
		if (checkcontains(deaddb, object->val) == 1){
			// fprint(2, "Skipping id %s, dead\n", object->val);
			ndbfree(entry);
			entry = ndbsnext(&s, "type", "Announce");
			continue;
		}
		if (checkcontains(objects, object->val) == 1){
			// fprint(2, "Skipping id %s, already processed\n", object->val);
			ndbfree(entry);
			entry = ndbsnext(&s, "type", "Announce");
			continue;
		} 
		print("%s\n", object->val);
		ndbfree(entry);
		entry = ndbsnext(&s, "type", "Announce");
	}
	
}
void main(void){
	Ndb *announces = getdb("announces"),
	    *objects = getdb("objects"),
	    *deaddb = getdb("dead.objects");
	if (announces == nil){
		sysfatal("No announces");
	}
	loopannounces(announces, objects, deaddb);
	ndbclose(announces);
	ndbclose(deaddb);
	ndbclose(objects);
	exits("");
}
