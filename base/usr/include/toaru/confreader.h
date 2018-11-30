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

#pragma once

#include <_cheader.h>
#include <toaru/hashmap.h>

_Begin_C_Header

/**
 * A configuration file is represented as a hashmap of sections,
 * which are themselves hashmaps. You may modify these hashmaps
 * to change key values, or add new sections.
 */
typedef struct {
	hashmap_t * sections;
} confreader_t;

/**
 * confreader_load
 *
 * Open a configuration file and read its contents.
 * Returns NULL if the requested file failed to open.
 */
extern confreader_t * confreader_load(const char * file);

/**
 * confreader_get
 *
 * Retrieve a string value from the config file.
 * An empty string for `section` represents the default section.
 * If the value is not found, NULL is returned.
 */
extern char * confreader_get(confreader_t * ctx, char * section, char * value);

/**
 * confreader_getd
 *
 * Retrieve a string value from the config file, falling back
 * to a default value if the requested key is not found.
 */
extern char * confreader_getd(confreader_t * ctx, char * section, char * value, char * def);

/**
 * confreader_int
 *
 * Retrieve an integer value from the config file.
 *
 * This is a convenience wrapper that calls atoi().
 * If the value is not found, 0 is returned.
 */
extern int confreader_int(confreader_t * ctx, char * section, char * value);

/**
 * confreader_intd
 *
 * Retrieve an integer value from the config file, falling back
 * to a default if the requested key is not found.
 */
extern int confreader_intd(confreader_t * ctx, char * section, char * value, int def);

/**
 * confreader_free
 *
 * Free the memory associated with a config file.
 */
extern void confreader_free(confreader_t * conf);

/**
 * confreader_write
 *
 * Write a config file back out to a file.
 */
extern int confreader_write(confreader_t * config, const char * file);

/**
 * confreader_create_empty
 *
 * Create an empty configuration file to be modified directly
 * through hashmap values.
 */
extern confreader_t * confreader_create_empty(void);

_End_C_Header
