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
 *  $Id: settings.c,v 1.1 2006-05-05 05:32:46 debug Exp $
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "misc.h"
#include "settings.h"


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
	if (settings == NULL) {
		fprintf(stderr, "settings_destroy(): internal error, "
		    "settings = NULL!\n");
		exit(1);
	}

	/*  TODO  */
}


/*
 *  TODO:
 *	Debug dump of all settings.
 *	Adding a setting.
 *	Removing a setting.
 *	Reading from a setting, given a name.
 *	Writing to a setting, given a name.
 */

