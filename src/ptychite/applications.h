#ifndef PTYCHITE_APPLICATIONS_H
#define PTYCHITE_APPLICATIONS_H

struct ptychite_server;

struct ptychite_application {
	char *name;
	char *wmclass;
  char *icon;
};

void read_applications(struct ptychite_server *server);

#endif
