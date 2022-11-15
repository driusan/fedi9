#include <u.h>
#include <libc.h>
#include "uuid.h"

int Ufmt (Fmt *f){
	uuid_t uuid = va_arg(f->args, uuid_t);

	return fmtprint(f, "%0.8ulx-%0.4uhx-%0.4uhx-%0.4uhx-%0.4uhx%0.4uhx%0.4uhx", 
		*((ulong *)&uuid.val[0]),
		*((ushort *)&uuid.val[4]),
		*((ushort *)&uuid.val[6]),
		*((ushort *)&uuid.val[8]),
		*((ushort *)&uuid.val[10]),
		*((ushort *)&uuid.val[12]),
		*((ushort *)&uuid.val[14])
	);
}

uuid_t* newuuid(void) {
	uuid_t* u = malloc(sizeof(uuid_t));
	for(int i = 0; i < 16; i++) {
		u->val[i] = truerand();
	}

	// Specify the type and variant of UUID
	u->val[6] = 0x40 | (u->val[6] & 0xf);
	u->val[8] = 0x80 | (u->val[8] & 0x3f);
	return u;
}
