</$objtype/mkfile

BIN=/$objtype/bin/fedi9
TARG=\
	extractkey\
	newuuid\
	getoutbox\
	fs

RC=\
	personrecord\
	get

OFILES=readfile.$O uuid.$O

HFILES=readfile.h uuid.h

</sys/src/cmd/mkmany

# Override install target to install rc.
install:V:
	mkdir -p $BIN
	for (i in $TARG)
		mk $MKFLAGS $i.install
	for (i in $RC)
		mk $MKFLAGS $i.rcinstall

%.rcinstall:V:
	cp $stem $BIN/$stem
	chmod +x $BIN/$stem
