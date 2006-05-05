#ifndef	SETTINGS_H
#define	SETTINGS_H

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
 *  $Id: settings.h,v 1.3 2006-05-05 21:52:21 debug Exp $
 */

#include <inttypes.h>

#define	GLOBAL_SETTINGS_NAME	"settings"

struct settings;

/*  Storage types:  */
#define	SETTINGS_TYPE_SUBSETTINGS	1
#define	SETTINGS_TYPE_INT		2
#define	SETTINGS_TYPE_INT32		3
#define	SETTINGS_TYPE_INT64		4

/*  Presentation formats:  */
#define	SETTINGS_FORMAT_DECIMAL		1	/*  -123  */
#define	SETTINGS_FORMAT_HEX		2	/*  0xffffffff80000000  */
#define	SETTINGS_FORMAT_BOOL		3	/*  true, false  */
#define	SETTINGS_FORMAT_YESNO		4	/*  yes, no  */

/*  settings.c:  */
struct settings *settings_new(void);
void settings_destroy(struct settings *settings);
void settings_debugdump(struct settings *settings, const char *prefix,
	int recurse);
void settings_add(struct settings *settings, const char *name, int writable,
	int type, int format, void *ptr);

#endif	/*  SETTINGS_H  */
