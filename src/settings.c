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
 *  $Id: settings.c,v 1.4 2006-05-06 18:04:48 debug Exp $
 *
 *  A generic settings object. (This module should be 100% indepedent of GXemul
 *  and hence easily reusable.)  It is basically a tree structure of nodes,
 *  where each node has a name and a few properties. The main property is
 *  a pointer, which can either point to other settings ("sub-settings"),
 *  or to a variable in memory.
 *
 *  Appart from the pointer, the other properties are a definition of the
 *  type being pointed to (int, int32_t, int64_t, etc), how it should be
 *  presented (e.g. it may be an int value in memory, but it should be
 *  presented as a boolean "true/false" value), and a flag which tells us
 *  whether the setting is directly writable or not.
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

	if (settings->name != NULL) {
		for (i=0; i<settings->n_settings; i++) {
			if (settings->name[i] != NULL)
				free(settings->name[i]);
		}

		free(settings->name);
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
	uint64_t value;

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
			default:printf("FATAL ERROR! Unknown storage type"
				    ": %i\n", settings->storage_type[i]);
				exit(1);
			}

			switch (settings->presentation_format[i]) {
			case SETTINGS_FORMAT_DECIMAL:
				printf("%"PRIi64, value);
				break;
			case SETTINGS_FORMAT_HEX:
				printf("%"PRIx64, value);
				break;
			case SETTINGS_FORMAT_BOOL:
				printf(value? "true" : "false");
				break;
			case SETTINGS_FORMAT_YESNO:
				printf(value? "yes" : "no");
				break;
			default:printf("FATAL ERROR! Unknown presentation "
				    "format: %i\n",
				    settings->presentation_format[i]);
				exit(1);
			}

			if (!settings->writable[i])
				printf("  (Read-only)");

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


