#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <wlr/util/log.h>

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

static void ptychite_application_unref(struct ptychite_application *app) {
	app->refs--;
	if (app->refs <= 0) {
		wlr_log(WLR_INFO, "Removing application '%s'", app->name);
		destroy_application(app);
	}
}

static int validate_df_name(const char *name) {
	unsigned int name_l = strlen(name);
	if (name_l < strlen(".desktop")) {
		return -1;
	}
	if (strcmp(name + name_l - strlen(".desktop"), ".desktop")) {
		return -1;
	}
	return 0;
}

static char *get_df_basename(const char *name) {
	int basename_c = strlen(name) + 1 - strlen(".desktop");
	char *basename = malloc(basename_c);
	if (!basename) {
		return NULL;
	}
	snprintf(basename, basename_c, "%s", name);
	return basename;
}

static void remove_application_from_df(struct ptychite_server *server, const char *name) {
	char *basename = get_df_basename(name);

	struct ptychite_application *app = ptychite_hash_map_get(&server->applications, basename);
	free(basename);
	if (!app) {
		return;
	}

	ptychite_hash_map_remove(&server->applications, app->df_basename);
	ptychite_application_unref(app);
	if (app->wmclass) {
		ptychite_hash_map_remove(&server->applications, app->wmclass);
		ptychite_application_unref(app);
	}
}

static void read_df(struct ptychite_server *server, const char *path, const char *name) {
	FILE *desktop_file = fopen(path, "r");
	if (!desktop_file) {
		return;
	}

	struct ptychite_application *app = calloc(1, sizeof(struct ptychite_application));
	if (!app) {
		fclose(desktop_file);
		return;
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
		return;
	}

	app->df = strdup(path);

	if (!(app->df_basename = get_df_basename(name))) {
		destroy_application(app);
		return;
	}

	wlr_log(WLR_INFO, "Adding application '%s'", app->name);

	if (app->icon) {
		ptychite_icon_create(server, app->icon, &app->resolved_icon);
	}

	if (app->wmclass && ptychite_hash_map_insert(&server->applications, app->wmclass, app)) {
		app->refs++;
	}

	if (!ptychite_hash_map_insert(&server->applications, app->df_basename, app)) {
		ptychite_application_unref(app);
	}
}

static void read_applications(struct ptychite_server *server) {
	DIR *dir = opendir("/usr/share/applications");
	if (!dir) {
		return;
	}

	struct dirent *entry;
	while ((entry = readdir(dir))) {
		if (entry->d_name[0] == '.') {
			continue;
		}

		if (validate_df_name(entry->d_name)) {
			continue;
		}

		char path[512];
		snprintf(path, sizeof(path), "%s/%s", "/usr/share/applications", entry->d_name);

		read_df(server, path, entry->d_name);
	}

	closedir(dir);
}

static int handle_inotify(int fd, uint32_t mask, void *data) {
	struct ptychite_server *server = data;

	static const size_t buflen = 1024 * (sizeof(struct inotify_event) + 16);
	char buf[buflen];

	int len = read(fd, buf, buflen);
	if (len < 0) {
		return 0;
	}

	int i = 0;
	while (i < len) {
		struct inotify_event *event = (struct inotify_event *)&buf[i];
		if (event->len) {
			char path[512];
			snprintf(path, sizeof(path), "%s/%s", "/usr/share/applications", event->name);

			if (event->mask & IN_CREATE) {
				if (event->mask & IN_ISDIR) {
					/* printf("The directory %s was created.\n", event->name); */
				} else {
					/* printf("The file %s was created.\n", event->name); */

					if (!validate_df_name(event->name)) {
						read_df(server, path, event->name);
					}
				}

			} else if (event->mask & IN_DELETE) {
				if (event->mask & IN_ISDIR) {
					/* printf("The directory %s was deleted.\n", event->name); */
				} else {
					/* printf("The file %s was deleted.\n", event->name); */

					if (!validate_df_name(event->name)) {
						remove_application_from_df(server, event->name);
					}
				}

			} else if (event->mask & IN_MODIFY) {
				if (event->mask & IN_ISDIR) {
					/* printf("The directory %s was modified.\n", event->name); */
				} else {
					/* printf("The file %s was modified.\n", event->name); */

					if (!validate_df_name(event->name)) {
						remove_application_from_df(server, event->name);
						read_df(server, path, event->name);
					}
				}

			} else if (event->mask & IN_MOVED_TO) {
				if (event->mask & IN_ISDIR) {
					/* printf("The directory %s was moved to.\n", event->name); */
				} else {
					/* printf("The file %s was moved to.\n", event->name); */

					if (!validate_df_name(event->name)) {
						read_df(server, path, event->name);
					}
				}

			} else if (event->mask & IN_MOVED_FROM) {
				if (event->mask & IN_ISDIR) {
					/* printf("The directory %s was moved from.\n", event->name); */
				} else {
					/* printf("The file %s was moved from.\n", event->name); */

					if (!validate_df_name(event->name)) {
						remove_application_from_df(server, event->name);
					}
				}
			}
		}

		i += sizeof(struct inotify_event) + event->len;
	}

	return 0;
}

int ptychite_server_init_applications(struct ptychite_server *server) {
	int fd = inotify_init();
	if (fd < 0) {
		return -1;
	}

	int watch = inotify_add_watch(
			fd, "/usr/share/applications", IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO);
	if (watch < 0) {
		return -1;
	}

	wl_event_loop_add_fd(wl_display_get_event_loop(server->display), fd, WL_EVENT_READABLE, handle_inotify, server);

	read_applications(server);

	return 0;
}
