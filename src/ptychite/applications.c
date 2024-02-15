#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "applications.h"
#include "icon.h"
#include "server.h"
#include "util.h"

void destroy_application(struct ptychite_application *app) {
	free(app->name);
	free(app->wmclass);
	free(app->icon);
	free(app);
}

void read_applications(struct ptychite_server *server) {
	DIR *dir;
	if (!(dir = opendir("/usr/share/applications"))) {
		return;
	}

	struct dirent *entry;
	while ((entry = readdir(dir))) {
		if (entry->d_name[0] == '.') {
			continue;
		}
		unsigned int d_name_l = strlen(entry->d_name);
		if (d_name_l < strlen(".desktop")) {
			continue;
		}
		if (strcmp(entry->d_name + d_name_l - strlen(".desktop"), ".desktop")) {
			continue;
		}

		char path[512];
		snprintf(path, sizeof(path), "%s/%s", "/usr/share/applications", entry->d_name);

		FILE *desktop_file = fopen(path, "r");
		if (!desktop_file) {
			continue;
		}

		struct ptychite_application *app = calloc(1, sizeof(struct ptychite_application));
		if (!app) {
			fclose(desktop_file);
			continue;
		}

		bool valid = false;
		char buf[BUFSIZ];
		while (fgets(buf, sizeof(buf), desktop_file)) {
			if (buf[0] == '[') {
				continue;
			}

			char *equals = strchr(buf, '=');
			if (!equals) {
				continue;
			}
			*equals = '\0';

			char *key = buf;
			char *val = equals + 1;

			char *newline = strchr(val, '\n');
			if (newline) {
				*newline = '\0';
			}

			if (!strcmp(key, "Type")) {
				if (!strcmp(val, "Application")) {
					valid = true;
				} else {
					valid = false;
					break;
				}
			} else if (!strcmp(key, "Name")) {
				app->name = strdup(val);
			} else if (!strcmp(key, "StartupWMClass")) {
				app->wmclass = strdup(val);
			} else if (!strcmp(key, "Icon")) {
				app->icon = strdup(val);
			} else if (!strcmp(key, "NoDisplay") || !strcmp(key, "Hidden")) {
				if (!strcmp(val, "true")) {
					valid = false;
					break;
				}
			}
		}

		fclose(desktop_file);

		if (valid) {
			if (ptychite_hash_map_insert(&server->applications, app->wmclass ? app->wmclass : app->name, app)) {
				if (app->icon && !ptychite_hash_map_get(&server->icons, app->icon)) {
					struct ptychite_icon *icon = ptychite_icon_create(server, app->icon);
					if (icon) {
						ptychite_hash_map_insert(&server->icons, app->icon, icon);
					}
				}
			} else {
				destroy_application(app);
			}
		} else {
			destroy_application(app);
		}
	}

	closedir(dir);
}
