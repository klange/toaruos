/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 *
 * Configuration File Reader
 *
 * Reads an implementation of the INI "standard". Note that INI
 * isn't actually a standard. We support the following:
 * - ; comments
 * - foo=bar keyword assignment
 * - [sections]
 */
#include <stdio.h>
#include <toaru/hashmap.h>
#include <toaru/confreader.h>

#define TRACE_APP_NAME "confreader"
#define TRACE(...)
//#include <toaru/trace.h>

static void free_hashmap(void * h) {
	hashmap_free(h);
	free(h);
}

confreader_t * confreader_load(const char * file) {

	confreader_t * out = malloc(sizeof(confreader_t));

	out->sections = hashmap_create(10);

	FILE * f = fopen(file, "r");

	hashmap_t * current_section = hashmap_create(10);
	current_section->hash_val_free = free_hashmap;

	hashmap_set(out->sections, "", current_section);

	if (!f) {
		/* File does not exist, no configuration values, but continue normally. */
		return out;
	}

	char tmp[1024];
	char tmp2[1024];

	while (!feof(f)) {
		char c = fgetc(f);
		tmp[0] = '\0';
		tmp2[0] = '\0';
		if (c == ';') {
			TRACE("Comment");
			while (!feof(f) && fgetc(f) != '\n');
			TRACE("Done");
		} else if (c == '\n' || c == EOF) {
			TRACE("blank line or EOF: %d", c);
			continue;
		} else if (c == '[') {
			TRACE("section");
			char * foo = tmp;
			int i;
			while ((i = fgetc(f)) >= 0) {
				if (i == ']') break;
				*foo = i;
				foo++;
				*foo = '\0';
			}
			while (!feof(f) && fgetc(f) != '\n');
			current_section = hashmap_create(10);
			TRACE("adding section %s", tmp);
			hashmap_set(out->sections, tmp, current_section);
			TRACE("section is over");
		} else {
			TRACE("value");
			char * foo = tmp;
			*foo = c;
			foo++;
			int i;
			while ((i = fgetc(f)) >= 0) {
				if (i == '=') break;
				*foo = i;
				foo++;
				*foo = '\0';
			}
			if (i != '=') {
				TRACE("no equals sign");
				while (!feof(f) && fgetc(f) != '\n');
				continue;
			}
			TRACE("=");
			foo = tmp2;
			while ((i = fgetc(f)) >= 0) {
				if (i == '\n') break;
				*foo = i;
				foo++;
				*foo = '\0';
			}
			TRACE("setting value %s to %s", tmp, tmp2);
			hashmap_set(current_section, tmp, strdup(tmp2));
		}
	}

	fclose(f);

	TRACE("done reading");

	return out;
}

void confreader_free(confreader_t * conf) {
	free_hashmap(conf->sections);
	free(conf);
}

char * confreader_get(confreader_t * ctx, char * section, char * value) {
	if (!ctx) return NULL;

	hashmap_t * s = hashmap_get(ctx->sections, section);

	if (!s) {
		TRACE("section doesn't exist: %s", section);
		return NULL;
	}

	return hashmap_get(s, value);
}

char * confreader_getd(confreader_t * ctx, char * section, char * value, char * def) {
	char * result = confreader_get(ctx, section, value);
	return result ? result : def;
}

int confreader_int(confreader_t * ctx, char * section, char * value) {
	char * result = confreader_get(ctx, section, value);
	if (!result) return 0;
	return atoi(result);
}

int confreader_intd(confreader_t * ctx, char * section, char * value, int def) {
	char * result = confreader_get(ctx, section, value);
	if (!result) return def;
	return atoi(result);
}



