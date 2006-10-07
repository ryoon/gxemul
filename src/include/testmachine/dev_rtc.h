#ifndef	TESTMACHINE_RTC_H
#define	TESTMACHINE_RTC_H

/*
 *  Definitions used by the "rtc" device in GXemul.
 *
 *  $Id: dev_rtc.h,v 1.1 2006-10-07 00:36:29 debug Exp $
 *  This file is in the public domain.
 */


#define	DEV_RTC_ADDRESS			0x0000000015000000
#define	DEV_RTC_LENGTH			0x0000000000000200

#define	    DEV_RTC_YEAR		    0x0000
#define	    DEV_RTC_MONTH		    0x0010
#define	    DEV_RTC_DAY			    0x0020
#define	    DEV_RTC_HOUR		    0x0030
#define	    DEV_RTC_MINUTE		    0x0040
#define	    DEV_RTC_SECOND		    0x0050
#define	    DEV_RTC_USEC		    0x0060

#define	    DEV_RTC_HZ			    0x0100
#define	    DEV_RTC_INTERRUPT_ACK	    0x0110

#endif	/*  TESTMACHINE_CONS_H  */
