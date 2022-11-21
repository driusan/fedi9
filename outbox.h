typedef struct{
	uint nreplies;
	uint nposts;
	uint nshares;
} outboxRetrievalStats;

typedef struct {
	uuid_t* internaluuid;

	char* id;

	char *cachepath;

	char *attributedTo;

	int nInReplyTo;
	char **inReplyTo;

	char *url;

	char *publishedTime; // called "published" in the JSON
} NoteObject;

outboxRetrievalStats getoutbox(char *url);
#pragma varargck type "N" NoteObject*
int Nfmt(Fmt *f);