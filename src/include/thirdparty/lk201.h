/*
 *  This file contains definitions from two files in NetBSD, lk201.h and lk201.c.
 *
 *  Technical information can be found here:
 *
 *	https://www.netbsd.org/docs/Hardware/Machines/DEC/lk201.html
 *
 *  The header file (lk201.h) in NetBSD did not have a separate copyright
 *  notice on it. Possible sources for lk201.h and lk201.c are the old files
 *  http://cvsweb.netbsd.org/bsdweb.cgi/src/sys/arch/pmax/dev/Attic/fb.c?rev=1.1&content-type=text/x-cvsweb-markup
 *  for the scan codes, and
 *  http://cvsweb.netbsd.org/bsdweb.cgi/src/sys/arch/pmax/dev/Attic/fbreg.h?rev=1.1&content-type=text/x-cvsweb-markup
 *  for other control codes, in which case the copyright is/was:
 *
 *
 * Copyright (c) 1992 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ralph Campbell and Rick Macklem.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *
 *
 *	@(#)fb.c	7.2 (Berkeley) 12/20/92
 * and
 *	@(#)fbreg.h	7.1 (Berkeley) 11/15/92
 */

#ifndef LK201_H
#define LK201_H

/*	$NetBSD: lk201.h,v 1.7 1999/03/19 18:34:01 ad Exp $	*/


/*
 * Ascii values of command keys.
 */
#define KBD_TAB		'\t'
#define KBD_DEL		127
#define KBD_RET		'\r'

/*
 *  Define "hardware-independent" codes for the control, shift, meta and
 *  function keys.  Codes start after the last 7-bit ASCII code (127)
 *  and are assigned in an arbitrary order.
 */
#define KBD_NOKEY	128

#define KBD_F1		201
#define KBD_F2		202
#define KBD_F3		203
#define KBD_F4		204
#define KBD_F5		205
#define KBD_F6		206
#define KBD_F7		207
#define KBD_F8		208
#define KBD_F9		209
#define KBD_F10		210
#define KBD_F11		211
#define KBD_F12		212
#define KBD_F13		213
#define KBD_F14		214
#define KBD_HELP	215
#define KBD_DO		216
#define KBD_F17		217
#define KBD_F18		218
#define KBD_F19		219
#define KBD_F20		220

#define KBD_FIND	221
#define KBD_INSERT	222
#define KBD_REMOVE	223
#define KBD_SELECT	224
#define KBD_PREVIOUS	225
#define KBD_NEXT	226

#define KBD_KP_ENTER	227
#define KBD_KP_F1	228
#define KBD_KP_F2	229
#define KBD_KP_F3	230
#define KBD_KP_F4	231
#define KBD_LEFT	232
#define KBD_RIGHT	233
#define KBD_DOWN	234
#define KBD_UP		235

#define KBD_CONTROL	236
#define KBD_SHIFT	237
#define KBD_CAPSLOCK	238
#define KBD_ALTERNATE	239



/*
 * Definitions for the Keyboard and mouse.
 */
/*
 * Special key values.
 */
#define	KEY_R_SHIFT	0xab
#define KEY_SHIFT	0xae
#define KEY_CONTROL	0xaf
#define KEY_CAPSLOCK	0xb0
#define	KEY_R_ALT	0xb2
#define KEY_UP		0xb3
#define KEY_REPEAT	0xb4
#define KEY_F1		0x56
#define KEY_COMMAND	KEY_F1

/*
 * Lk201/301 keyboard
 */
#define LK_UPDOWN	0x86		/* bits for setting lk201 modes */
#define LK_AUTODOWN	0x82
#define LK_DOWN		0x80
#define LK_DEFAULTS	0xd3		/* reset mode settings          */
#define LK_AR_ENABLE	0xe3		/* global auto repeat enable	*/
#define LK_CL_ENABLE	0x1b		/* keyclick enable		*/
#define LK_KBD_ENABLE	0x8b		/* keyboard enable		*/
#define LK_BELL_ENABLE	0x23		/* the bell			*/
#define LK_LED_ENABLE	0x13		/* light led			*/
#define LK_LED_DISABLE	0x11		/* turn off led			*/
#define LK_RING_BELL	0xa7		/* ring keyboard bell		*/
#define LED_1		0x81		/* led bits			*/
#define LED_2		0x82
#define LED_3		0x84
#define LED_4		0x88
#define LED_ALL		0x8f
#define LK_HELP		0x7c		/* help key			*/
#define LK_DO		0x7d		/* do key			*/
#define LK_KDOWN_ERROR	0x3d		/* key down on powerup error	*/
#define LK_POWER_ERROR	0x3e		/* keyboard failure on pwrup tst*/
#define LK_OUTPUT_ERROR 0xb5		/* keystrokes lost during inhbt */
#define LK_INPUT_ERROR	0xb6		/* garbage command to keyboard	*/
#define LK_LOWEST	0x56		/* lowest significant keycode	*/

/* max volume is 0, lowest is 0x7 */
#define	LK_PARAM_VOLUME(v)		(0x80|((v)&0x7))

/* mode command details */
#define	LK_CMD_MODE(m,div)		((m)|((div)<<3))


/*
 * Command characters for the mouse.
 */
#define MOUSE_SELF_TEST		'T'
#define MOUSE_INCREMENTAL	'R'

/*
 * Mouse output bits.
 *
 *     	MOUSE_START_FRAME	Start of report frame bit.
 *	MOUSE_X_SIGN		Sign bit for X.
 *	MOUSE_Y_SIGN		Sign bit for Y.
 *	MOUSE_X_OFFSET		X offset to start cursor at.
 *	MOUSE_Y_OFFSET		Y offset to start cursor at.
 */
#define MOUSE_START_FRAME	0x80
#define MOUSE_X_SIGN		0x10
#define MOUSE_Y_SIGN		0x08

/*
 * Definitions for mouse buttons
 */
#define EVENT_LEFT_BUTTON	0x01
#define EVENT_MIDDLE_BUTTON	0x02
#define EVENT_RIGHT_BUTTON	0x03
#define RIGHT_BUTTON		0x01
#define MIDDLE_BUTTON		0x02
#define LEFT_BUTTON		0x04

#ifdef _KERNEL
extern int LKgetc __P((dev_t dev));
extern void lkdivert __P ((int (*getc_fn)(dev_t dev), dev_t dev));
#endif


/*  This is also from netbsd, pmax/dev/lk201.c:  */

/*
 * Keyboard to ASCII, unshifted.
 */
static u_char unshiftedAscii[] = {
/*  0 */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/*  4 */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/*  8 */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/*  c */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 10 */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 14 */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 18 */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 1c */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 20 */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 24 */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 28 */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 2c */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 30 */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 34 */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 38 */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 3c */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 40 */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 44 */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 48 */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 4c */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 50 */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 54 */ KBD_NOKEY,	KBD_NOKEY,	KBD_F1,		KBD_F2,
/* 58 */ KBD_F3,	KBD_F4,		KBD_F5,		KBD_NOKEY,
/* 5c */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 60 */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 64 */ KBD_F6,	KBD_F7,		KBD_F8,		KBD_F9,
/* 68 */ KBD_F10,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 6c */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 70 */ KBD_NOKEY,	'\033',		KBD_F12,	KBD_F13,
/* 74 */ KBD_F14,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 78 */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 7c */ KBD_HELP,	KBD_DO,		KBD_NOKEY,	KBD_NOKEY,
/* 80 */ KBD_F17,	KBD_F18,	KBD_F19,	KBD_F20,
/* 84 */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 88 */ KBD_NOKEY,	KBD_NOKEY,	KBD_FIND,	KBD_INSERT,
/* 8c */ KBD_REMOVE,	KBD_SELECT,	KBD_PREVIOUS,	KBD_NEXT,
/* 90 */ KBD_NOKEY,	KBD_NOKEY,	'0',		KBD_NOKEY,
/* 94 */ '.',		KBD_KP_ENTER,	'1',		'2',
/* 98 */ '3',		'4',		'5',		'6',
/* 9c */ ',',		'7',		'8',		'9',
/* a0 */ '-',		KBD_KP_F1,	KBD_KP_F2,	KBD_KP_F3,
/* a4 */ KBD_KP_F4,	KBD_NOKEY,	KBD_NOKEY,	KBD_LEFT,
/* a8 */ KBD_RIGHT,	KBD_DOWN, 	KBD_UP,		KBD_NOKEY,
/* ac */ KBD_NOKEY,	KBD_NOKEY,	KBD_SHIFT,	KBD_CONTROL,
/* b0 */ KBD_CAPSLOCK,	KBD_ALTERNATE,	KBD_NOKEY,	KBD_NOKEY,
/* b4 */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* b8 */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* bc */ KBD_DEL,	KBD_RET,	KBD_TAB,	'`',
/* c0 */ '1',		'q',		'a',		'z',
/* c4 */ KBD_NOKEY,	'2',		'w',		's',
/* c8 */ 'x',		'<',		KBD_NOKEY,	'3',
/* cc */ 'e',		'd',		'c',		KBD_NOKEY,
/* d0 */ '4',		'r',		'f',		'v',
/* d4 */ ' ',		KBD_NOKEY,	'5',		't',
/* d8 */ 'g',		'b',		KBD_NOKEY,	'6',
/* dc */ 'y',		'h',		'n',		KBD_NOKEY,
/* e0 */ '7',		'u',		'j',		'm',
/* e4 */ KBD_NOKEY,	'8',		'i',		'k',
/* e8 */ ',',		KBD_NOKEY,	'9',		'o',
/* ec */ 'l',		'.',		KBD_NOKEY,	'0',
/* f0 */ 'p',		KBD_NOKEY,	';',		'/',
/* f4 */ KBD_NOKEY,	'=',		']',		'\\',
/* f8 */ KBD_NOKEY,	'-',		'[',		'\'',
/* fc */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
};

/*
 * Keyboard to Ascii, shifted.
 */
static u_char shiftedAscii[] = {
/*  0 */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/*  4 */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/*  8 */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/*  c */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 10 */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 14 */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 18 */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 1c */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 20 */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 24 */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 28 */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 2c */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 30 */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 34 */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 38 */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 3c */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 40 */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 44 */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 48 */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 4c */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 50 */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 54 */ KBD_NOKEY,	KBD_NOKEY,	KBD_F1,		KBD_F2,
/* 58 */ KBD_F3,	KBD_F4,		KBD_F5,		KBD_NOKEY,
/* 5c */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 60 */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 64 */ KBD_F6,	KBD_F7,		KBD_F8,		KBD_F9,
/* 68 */ KBD_F10,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 6c */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 70 */ KBD_NOKEY,	KBD_F11,	KBD_F12,	KBD_F13,
/* 74 */ KBD_F14,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 78 */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 7c */ KBD_HELP,	KBD_DO,		KBD_NOKEY,	KBD_NOKEY,
/* 80 */ KBD_F17,	KBD_F18,	KBD_F19,	KBD_F20,
/* 84 */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* 88 */ KBD_NOKEY,	KBD_NOKEY,	KBD_FIND,	KBD_INSERT,
/* 8c */ KBD_REMOVE,	KBD_SELECT,	KBD_PREVIOUS,	KBD_NEXT,
/* 90 */ KBD_NOKEY,	KBD_NOKEY,	'0',		KBD_NOKEY,
/* 94 */ '.',		KBD_KP_ENTER,	'1',		'2',
/* 98 */ '3',		'4',		'5',		'6',
/* 9c */ ',',		'7',		'8',		'9',
/* a0 */ '-',		KBD_KP_F1,	KBD_KP_F2,	KBD_KP_F3,
/* a4 */ KBD_KP_F4,	KBD_NOKEY,	KBD_NOKEY,	KBD_LEFT,
/* a8 */ KBD_RIGHT,	KBD_DOWN, 	KBD_UP,		KBD_NOKEY,
/* ac */ KBD_NOKEY,	KBD_NOKEY,	KBD_SHIFT,	KBD_CONTROL,
/* b0 */ KBD_CAPSLOCK,	KBD_ALTERNATE,	KBD_NOKEY,	KBD_NOKEY,
/* b4 */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* b8 */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
/* bc */ KBD_DEL,	KBD_RET,	KBD_TAB,	'~',
/* c0 */ '!',		'q',		'a',		'z',
/* c4 */ KBD_NOKEY,	'@',		'w',		's',
/* c8 */ 'x',		'>',		KBD_NOKEY,	'#',
/* cc */ 'e',		'd',		'c',		KBD_NOKEY,
/* d0 */ '$',		'r',		'f',		'v',
/* d4 */ ' ',		KBD_NOKEY,	'%',		't',
/* d8 */ 'g',		'b',		KBD_NOKEY,	'^',
/* dc */ 'y',		'h',		'n',		KBD_NOKEY,
/* e0 */ '&',		'u',		'j',		'm',
/* e4 */ KBD_NOKEY,	'*',		'i',		'k',
/* e8 */ '<',		KBD_NOKEY,	'(',		'o',
/* ec */ 'l',		'>',		KBD_NOKEY,	')',
/* f0 */ 'p',		KBD_NOKEY,	':',		'?',
/* f4 */ KBD_NOKEY,	'+',		'}',		'|',
/* f8 */ KBD_NOKEY,	'_',		'{',		'"',
/* fc */ KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,	KBD_NOKEY,
};

#endif	/*  LK201_H  */
