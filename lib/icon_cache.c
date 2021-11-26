/**
 * @brief icon_cache - caches icons
 *
 * Used be a few different applications.
 * Probably needs scaling?
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 */
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

#include <toaru/graphics.h>
#include <toaru/hashmap.h>

static hashmap_t * icon_cache_16;
static hashmap_t * icon_cache_48;

static char * icon_directories_16[] = {
	"/usr/share/icons/16",
	"/usr/share/icons/24",
	"/usr/share/icons/48",
	"/usr/share/icons",
	"/usr/share/icons/external",
	NULL
};

static char * icon_directories_48[] = {
	"/usr/share/icons/48",
	"/usr/share/icons/24",
	"/usr/share/icons/16",
	"/usr/share/icons",
	"/usr/share/icons/external",
	NULL
};

static char * prefixes[] = {
	"png",
	"bmp",
	NULL
};

__attribute__((constructor))
static void _init_caches(void) {
	icon_cache_16 = hashmap_create(10);
	{ /* Generic fallback icon */
		sprite_t * app_icon = malloc(sizeof(sprite_t));
		load_sprite(app_icon, "/usr/share/icons/16/applications-generic.png");
		hashmap_set(icon_cache_16, "generic", app_icon);
	}

	icon_cache_48 = hashmap_create(10);
	{ /* Generic fallback icon */
		sprite_t * app_icon = malloc(sizeof(sprite_t));
		load_sprite(app_icon, "/usr/share/icons/48/applications-generic.png");
		hashmap_set(icon_cache_48, "generic", app_icon);
	}
}


static sprite_t * icon_get_int(const char * name, hashmap_t * icon_cache, char ** icon_directories) {

	if (!strcmp(name,"")) {
		/* If a window doesn't have an icon set, return the generic icon */
		return hashmap_get(icon_cache, "generic");
	}

	/* Check the icon cache */
	sprite_t * icon = hashmap_get(icon_cache, (void*)name);

	if (!icon) {
		/* We don't have an icon cached for this identifier, try search */
		int i = 0;
		char path[100];
		while (icon_directories[i]) {
			/* Check each path... */
			char ** prefix = prefixes;
			while (*prefix) {
				sprintf(path, "%s/%s.%s", icon_directories[i], name, *prefix);
				if (access(path, R_OK) == 0) {
					/* And if we find one, cache it */
					icon = malloc(sizeof(sprite_t));
					load_sprite(icon, path);
					hashmap_set(icon_cache, (void*)name, icon);
					return icon;
				}
				prefix++;
			}
			i++;
		}

		/* If we've exhausted our search paths, just return the generic icon */
		icon = hashmap_get(icon_cache, "generic");
		hashmap_set(icon_cache, (void*)name, icon);
	}

	/* We have an icon, return it */
	return icon;
}

sprite_t * icon_get_16(const char * name) {
	return icon_get_int(name, icon_cache_16, icon_directories_16);
}

sprite_t * icon_get_48(const char * name) {
	return icon_get_int(name, icon_cache_48, icon_directories_48);
}
