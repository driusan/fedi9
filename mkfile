</$objtype/mkfile

BIN=/$objtype/bin/fedi9
TARG=\
	extractkey\
	newuuid\
	getoutbox\
	fs\
	jsonfmt\
	jsonfs

RC=\
	personrecord\
	get\
	validateincoming\
	spamcheck\
	processvalidated\
	mkprofile\
	mkactivity\
	mkobject\
	post\
	follower\
	rclib\
	getannounces\
	addObjectToDb\
	publish

OFILES=readfile.$O uuid.$O outbox.$O removedir.$O

HFILES=readfile.h uuid.h removedir.h

</sys/src/cmd/mkmany

validatehttpsig: validatehttpsig-go
	cd validatehttpsig-go
	go build -o ../validatehttpsig
	cd ..
httpsign: httpsign-go
	cd httpsign-go
	go build -o ../httpsign
	cd ..

# Override install target to install rc.
install:V:
	mkdir -p $BIN
	for (i in $TARG)
		mk $MKFLAGS $i.install
	for (i in $RC)
		mk $MKFLAGS $i.rcinstall
	cp validatehttpsig $BIN
	cp httpsign $BIN/

%.rcinstall:V:
	cp $stem $BIN/$stem
	chmod +x $BIN/$stem
