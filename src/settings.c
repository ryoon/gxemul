/*
 *  Copyright (C) 2006  Anders Gavare.  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright  
 *     notice, this list of conditions and the following disclaimer in the 
 *     documentation and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 *  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE   
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 *  OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 *  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 *  SUCH DAMAGE.
 *
 *
 *  $Id: settings.c,v 1.10 2006-09-05 07:30:34 debug Exp $
 *
 *  A generic settings object. (This module should be 100% indepedent of GXemul
 *  and hence easily reusable.)  It is basically a tree structure of nodes,
 *  where each node has a name and a few properties. The main property is
 *  a pointer, which can either point to other settings ("sub-settings"),
 *  or to a variable in memory.
 *
 *  Appart from the pointer, the other properties are a definition of the
 *  type being pointed to (int, int32_t, int64_t, char*, etc), how it should be
 *  presented (e.g. it may be an int value in memory, but it should be
 *  presented as a boolean "true/false" value), and a flag which tells us
 *  whether the setting is directly writable or not.
 *
 *  If UNSTABLE_DEVEL is defined, then warnings are printed when
 *  settings_destroy() is called if individual settings have not yet been
 *  deleted. (This is to help making sure that code which uses the settings
 *  subsystem correctly un-initializes stuff.)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*  Including misc.h should ONLY be necessary to work around the fact that
    many systems don't have PRIx64 etc defined.  */
#include "misc.h"

#include "settings.h"


struct settings {
	int			n_settings;

	/*
	 *  Each setting has a name, a writable flag, a storage type, a
	 *  presentation format, and a pointer.
	 *
	 *  For subsettings, the pointer points to the subsettings object;
	 *  for other settings, the pointer points to a variable.
	 *
	 *  These pointers point to simple linear arrays, containing n_settings
	 *  entries each.
	 */

	char			**name;
	int			*writable;
	int			*storage_type;
	int			*presentation_format;
	void			**ptr;
};


/*
 *  settings_new():
 *
 *  Create a new settings object. Return value is a pointer to the newly
 *  created object. The function does not return on failure.
 */
struct settings *settings_new(void)
{
	struct settings *settings = malloc(sizeof(struct settings));

	if (settings == NULL) {
		fprintf(stderr, "settings_new(): out of memory\n");
		exit(1);
	}

	/*  No settings.  */
	memset(settings, 0, sizeof(struct settings));

	return settings;
}


/*
 *  settings_destroy():
 *
 *  Frees all resources occupied by a settings object.
 */
void settings_destroy(struct settings *settings)
{
	int i;

	if (settings == NULL) {
		fprintf(stderr, "settings_destroy(): internal error, "
		    "settings = NULL!\n");
		exit(1);
	}

#ifdef UNSTABLE_DEVEL
	if (settings->n_settings > 0)
		printf("settings_destroy(): there are remaining settings!\n");
#endif

	if (settings->name != NULL) {
		for (i=0; i<settings->n_settings; i++) {
			if (settings->name[i] != NULL) {
#ifdef UNSTABLE_DEVEL
				printf("settings_destroy(): setting '%s'"
				    " was not properly deleted before "
				    "exiting!\n", settings->name[i]);
#endif
				free(settings->name[i]);
			}
		}

		free(settings->name);
	} else if (settings->n_settings != 0) {
		fprintf(stderr, "settings_destroy(): internal error, "
		    "settings->name = NULL, but there were settings?"
		    " (n_settings = %i)\n", settings->n_settings);
		exit(1);
	}

	if (settings->writable != NULL)
		free(settings->writable);

	if (settings->storage_type != NULL)
		free(settings->storage_type);

	if (settings->presentation_format != NULL)
		free(settings->presentation_format);

	if (settings->ptr != NULL)
		free(settings->ptr);

	free(settings);
}


/*
 *  settings_debugdump():
 *
 *  Dump settings in a settings object to stdout.
 *  If recurse is non-zero, all subsetting objects are also dumped.
 */
void settings_debugdump(struct settings *settings, const char *prefix,
	int recurse)
{
	size_t name_buflen = strlen(prefix) + 100;
	char *name = malloc(name_buflen);
	int i;
	uint64_t value = 0;

	for (i=0; i<settings->n_settings; i++) {
		snprintf(name, name_buflen, "%s.%s", prefix, settings->name[i]);

		if (settings->storage_type[i] == SETTINGS_TYPE_SUBSETTINGS) {
			/*  Subsettings:  */
			if (recurse)
				settings_debugdump(settings->ptr[i], name, 1);
		} else {
			/*  Normal value:  */
			printf("%s = ", name);

			switch (settings->storage_type[i]) {
			case SETTINGS_TYPE_INT:
				value = *((int *) settings->ptr[i]);
				break;
			case SETTINGS_TYPE_INT32:
				value = *((int32_t *) settings->ptr[i]);
				break;
			case SETTINGS_TYPE_INT64:
				value = *((int64_t *) settings->ptr[i]);
				break;
			case SETTINGS_TYPE_STRING:
				break;
			default:printf("FATAL ERROR! Unknown storage type"
				    ": %i\n", settings->storage_type[i]);
				exit(1);
			}

			switch (settings->presentation_format[i]) {
			case SETTINGS_FORMAT_DECIMAL:
				printf("%"PRIi64, value);
				break;
			case SETTINGS_FORMAT_HEX32:
				printf("0x08%"PRIx32, (int32_t) value);
				break;
			case SETTINGS_FORMAT_HEX64:
				printf("0x016%"PRIx64, (int64_t) value);
				break;
			case SETTINGS_FORMAT_BOOL:
				printf(value? "true" : "false");
				break;
			case SETTINGS_FORMAT_YESNO:
				printf(value? "yes" : "no");
				break;
			case SETTINGS_FORMAT_STRING:
				printf("\"%s\"", *((char **)settings->ptr[i]));
				break;
			default:printf("FATAL ERROR! Unknown presentation "
				    "format: %i\n",
				    settings->presentation_format[i]);
				exit(1);
			}

			if (!settings->writable[i])
				printf("  (R/O)");

			printf("\n");
		}
	}

	free(name);
}


/*
 *  settings_add():
 *
 *  Add a setting to a settings object.
 */
void settings_add(struct settings *settings, const char *name, int writable,
	int type, int format, void *ptr)
{
	int i;

	for (i=0; i<settings->n_settings; i++) {
		if (strcmp(settings->name[i], name) == 0)
			break;
	}

	if (i < settings->n_settings) {
		fprintf(stderr, "settings_add(): name '%s' is already"
		    " in use\n", name);
		exit(1);
	}

	settings->n_settings ++;

	if ((settings->name = realloc(settings->name, settings->n_settings
	    * sizeof(char *))) == NULL)
		goto out_of_mem;
	if ((settings->writable = realloc(settings->writable,
	    settings->n_settings * sizeof(int))) == NULL)
		goto out_of_mem;
	if ((settings->storage_type = realloc(settings->storage_type,
	    settings->n_settings * sizeof(int))) == NULL)
		goto out_of_mem;
	if ((settings->presentation_format = realloc(settings->
	    presentation_format, settings->n_settings * sizeof(int))) == NULL)
		goto out_of_mem;
	if ((settings->ptr = realloc(settings->ptr, settings->n_settings
	    * sizeof(void *))) == NULL)
		goto out_of_mem;

	settings->name[settings->n_settings - 1] = strdup(name);
	settings->writable[settings->n_settings - 1] = writable;
	settings->storage_type[settings->n_settings - 1] = type;
	settings->presentation_format[settings->n_settings - 1] = format;
	settings->ptr[settings->n_settings - 1] = ptr;

	return;

out_of_mem:
	fprintf(stderr, "settings_add(): fatal error: out of memory\n");
	exit(1);
}


/*
 *  settings_remove():
 *
 *  Remove a setting from a settings object.
 */
void settings_remove(struct settings *settings, const char *name)
{
	int i, m;

	for (i=0; i<settings->n_settings; i++) {
		if (strcmp(settings->name[i], name) == 0)
			break;
	}

	if (i >= settings->n_settings) {
#ifdef UNSTABLE_DEVEL
		fprintf(stderr, "settings_remove(): attempting to remove"
		    " non-existant setting '%s'\n", name);
#endif
		return;
	}

	/*  Check subsettings specifically:  */
	if (settings->storage_type[i] == SETTINGS_TYPE_SUBSETTINGS &&
	    settings->ptr[i] != NULL) {
		struct settings *subsettings = settings->ptr[i];
		if (subsettings->n_settings != 0) {
			fprintf(stderr, "settings_remove(): attempting to "
			    "remove non-emtpy setting '%s'\n", name);
			fprintf(stderr, "Remaining settings are:\n");
			for (i=0; i<subsettings->n_settings; i++)
				fprintf(stderr, "\t%s\n", subsettings->name[i]);
			exit(1);
		}
	}

	settings->n_settings --;
	free(settings->name[i]);

	m = settings->n_settings - i;

	memmove(&settings->name[i], &settings->name[i+1],
	    m * sizeof(settings->name[0]));
	memmove(&settings->writable[i], &settings->writable[i+1],
	    m * sizeof(settings->writable[0]));
	memmove(&settings->storage_type[i], &settings->storage_type[i+1],
	    m * sizeof(settings->storage_type[0]));
	memmove(&settings->presentation_format[i],
	    &settings->presentation_format[i+1],
	    m * sizeof(settings->presentation_format[0]));
	memmove(&settings->ptr[i], &settings->ptr[i+1],
	    m * sizeof(settings->ptr[0]));
}


/*
 *  settings_remove_all():
 *
 *  Remove all (level-1) settings from a settings object. By level-1, I mean
 *  all settings that do not contain subsettings.
 */
void settings_remove_all(struct settings *settings)
{
	while (settings->n_settings > 0)
		settings_remove(settings, settings->name[0]);
}


/*
 *  settings_access():
 *
 *  Read or write a setting. fullname may be something like
 *  "settings.x.y". When writing a value, valuebuf should point to a string
 *  containing the new value (note: always a string). When reading a value,
 *  valuebuf should point to a buffer where the value will be stored (up
 *  to bufsize-1 chars, plus the nul char).
 *
 *  The return value is one of the following:
 *
 *	SETTINGS_OK
 *		The value was read or written.
 *
 *	SETTINGS_NAME_NOT_FOUND
 *		The name was not found in the settings object.
 *
 *	SETTINGS_READONLY
 *		The name was found, but it was marked as read-only, and
 *		an attempt was made to write to it.
 */
int settings_access(struct settings *settings, const char *fullname,
        int writeflag, char *valuebuf, size_t bufsize)
{
	/*  TODO  */
	printf("settings_access(fullname='%s')\n", fullname);

	return SETTINGS_NAME_NOT_FOUND;
}

