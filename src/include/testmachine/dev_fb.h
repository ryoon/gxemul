#ifndef	TESTMACHINE_FB_H
#define	TESTMACHINE_FB_H

/*
 *  Definitions used by the framebuffer device in GXemul.
 *
 *  $Id: dev_fb.h,v 1.4 2006-07-08 12:30:03 debug Exp $
 *  This file is in the public domain.
 */


/*  Physical base address for linear framebuffer memory:  */
#define	DEV_FB_ADDRESS			0x12000000

/*  Physical base address for the framebuffer controller:  */
#define	DEV_FBCTRL_ADDRESS		0x12f00000
#define	DEV_FBCTRL_LENGTH		      0x20

#define	DEV_FBCTRL_PORT			      0x00
#define	DEV_FBCTRL_DATA			      0x10

#define	DEV_FBCTRL_PORT_COMMAND_AND_STATUS		0
#define	DEV_FBCTRL_PORT_X1				1
#define	DEV_FBCTRL_PORT_Y1				2
#define	DEV_FBCTRL_PORT_X2				3
#define	DEV_FBCTRL_PORT_Y2				4
#define	DEV_FBCTRL_PORT_COLOR				5
#define	DEV_FBCTRL_NPORTS		6

#define	DEV_FBCTRL_COMMAND_NOP				0
#define	DEV_FBCTRL_COMMAND_CHANGE_RESOLUTION		1
#define	DEV_FBCTRL_COMMAND_FILL				2
#define	DEV_FBCTRL_COMMAND_COPY				3

#define	DEV_FBCTRL_MAXY(x)	(((DEV_FBCTRL_ADDRESS-DEV_FB_ADDRESS) / 3) / x)

#endif	/*  TESTMACHINE_FB_H  */
