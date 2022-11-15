typedef struct { unsigned char val[16]; } uuid_t;

#pragma varargck type "U" uuid_t

int Ufmt(Fmt *f);
uuid_t* newuuid(void);
