#ifndef PTYCHITE_APPLICATIONS_H
#define PTYCHITE_APPLICATIONS_H

struct ptychite_server;

struct ptychite_application {
	char *name;
	char *df;
	char *df_basename;
	char *wmclass;
	char *icon;
	char *resolved_icon;

	int refs;
};

void read_applications(struct ptychite_server *server);

#endif
