#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "applications.h"
#include "icon.h"
#include "server.h"
#include "util.h"

static void destroy_application(struct ptychite_application *app) {
	free(app->name);
	free(app->df);
	free(app->df_basename);
	free(app->wmclass);
	free(app->icon);
	free(app->resolved_icon);
	free(app);
}

void ptychite_application_unref(struct ptychite_application *app) {
	app->refs--;
	if (app->refs <= 0) {
		destroy_application(app);
	}
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
		app->refs = 1;

		bool valid = false;
		bool in_entry = false;

		char buf[BUFSIZ];
		while (fgets(buf, sizeof(buf), desktop_file)) {
			if (buf[0] == '[') {
				if (in_entry) {
					break;
				}
				if (!strcmp(buf, "[Desktop Entry]\n")) {
					in_entry = true;
				}
				continue;
			}

			if (!in_entry) {
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

		if (!valid) {
			destroy_application(app);
			continue;
		}

		app->df = strdup(path);
		int df_basename_c = d_name_l + 1 - strlen(".desktop");
		if ((app->df_basename = malloc(df_basename_c))) {
			snprintf(app->df_basename, df_basename_c, "%s", entry->d_name);
		}

		if (app->icon) {
			ptychite_icon_create(server, app->icon, &app->resolved_icon);
		}
		
		if (app->wmclass) {
			if (!ptychite_hash_map_insert(&server->applications, app->wmclass, app)) {
				destroy_application(app);
				continue;
			}
			app->refs++;
		}

		if (!ptychite_hash_map_insert(&server->applications, app->df_basename, app)) {
			ptychite_application_unref(app);
		}
	}

	closedir(dir);
}
