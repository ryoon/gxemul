/*
 *  GXemul floating point tests.
 *
 *  This file is in the Public Domain.
 */

// Regenerate fpconst.h like this:
// grep void\) fpconst.c | while read a; do echo $a";"; done > fpconst.h

#include "fpconst.h"


float f_0_0(void)
{
	return 0.0;
}

double d_0_0(void)
{
	return 0.0;
}


float f_m0_0(void)
{
	return -0.0;
}

double d_m0_0(void)
{
	return -0.0;
}


float f_0_17(void)
{
	return 0.17;
}

double d_0_17(void)
{
	return 0.17;
}


float f_m0_17(void)
{
	return -0.17;
}

double d_m0_17(void)
{
	return -0.17;
}


float f_1_0(void)
{
	return 1.0;
}

double d_1_0(void)
{
	return 1.0;
}


float f_m1_0(void)
{
	return -1.0;
}

double d_m1_0(void)
{
	return -1.0;
}


float f_1_7(void)
{
	return 1.7;
}

double d_1_7(void)
{
	return 1.7;
}


float f_m1_7(void)
{
	return -1.7;
}

double d_m1_7(void)
{
	return -1.7;
}


float f_42(void)
{
	return 42;
}

double d_42(void)
{
	return 42;
}


float f_m42(void)
{
	return -42;
}

double d_m42(void)
{
	return -42;
}


float f_nan(void)
{
	unsigned int v = 0x7fffffff;
	void *p = &v;
	float *pf = (float*) p;
	return *pf;
}

double d_nan(void)
{
	unsigned long long v = 0x7fffffffffffffffULL;
	void *p = &v;
	double *pd = (double*) p;
	return *pd;
}


float f_nan_x(void)
{
	unsigned int v = 0x7fc01234;
	void *p = &v;
	float *pf = (float*) p;
	return *pf;
}

double d_nan_x(void)
{
	unsigned long long v = 0x7ff8000000001234ULL;
	void *p = &v;
	double *pd = (double*) p;
	return *pd;
}


float f_inf(void)
{
	unsigned int v = 0x7f800000;
	void *p = &v;
	float *pf = (float*) p;
	return *pf;
}

double d_inf(void)
{
	unsigned long long v = 0x7ff0000000000000ULL;
	void *p = &v;
	double *pd = (double*) p;
	return *pd;
}


float f_m_inf(void)
{
	unsigned int v = 0xff800000;
	void *p = &v;
	float *pf = (float*) p;
	return *pf;
}

double d_m_inf(void)
{
	unsigned long long v = 0xfff0000000000000ULL;
	void *p = &v;
	double *pd = (double*) p;
	return *pd;
}

