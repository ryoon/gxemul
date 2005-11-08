/*
 *  Copyright (C) 2005  Anders Gavare.  All rights reserved.
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
 *  $Id: device.c,v 1.17 2005-11-08 11:01:45 debug Exp $
 *
 *  Device registry framework.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "device.h"
#include "misc.h"


static struct device_entry *device_entries = NULL;
static int device_entries_sorted = 0;
static int n_device_entries = 0;
static int device_exit_on_error = 1;

static struct pci_entry *pci_entries = NULL;
static int n_pci_entries = 0;


/*
 *  device_entry_compar():
 *
 *  Internal function, used by sort_entries().
 */
static int device_entry_compar(const void *a, const void *b)
{
	struct device_entry *pa = (struct device_entry *) a;
	struct device_entry *pb = (struct device_entry *) b;

	return strcmp(pa->name, pb->name);
}


/*
 *  sort_entries():
 *
 *  Internal function. Sorts the device_entries array in alphabetic order.
 */
static void sort_entries(void)
{
	qsort(device_entries, n_device_entries, sizeof(struct device_entry),
	    device_entry_compar);

	device_entries_sorted = 1;
}


/*
 *  device_register():
 *
 *  Registers a device. The device is added to the end of the device_entries
 *  array, and the sorted flag is set to zero.
 *
 *  NOTE: It would be a bad thing if two devices had the same name. However,
 *        that isn't checked here, it is up to the caller!
 *
 *  Return value is 1 if the device was registered, 0 otherwise.
 */
int device_register(char *name, int (*initf)(struct devinit *))
{
	device_entries = realloc(device_entries, sizeof(struct device_entry)
	    * (n_device_entries + 1));
	if (device_entries == NULL) {
		fprintf(stderr, "device_register(): out of memory\n");
		exit(1);
	}

	memset(&device_entries[n_device_entries], 0,
	    sizeof(struct device_entry));

	device_entries[n_device_entries].name = strdup(name);
	device_entries[n_device_entries].initf = initf;

	device_entries_sorted = 0;
	n_device_entries ++;
	return 1;
}


/*
 *  pci_register():
 *
 *  Registers a pci device. The pci device is added to the pci_entries array.
 *
 *  Return value is 1 if the pci device was registered. If it was not
 *  added, this function does not return.
 */
int pci_register(char *name, void (*initf)(struct machine *, struct memory *,
	struct pci_device *))
{
	pci_entries = realloc(pci_entries, sizeof(struct pci_entry)
	    * (n_pci_entries + 1));
	if (pci_entries == NULL) {
		fprintf(stderr, "pci_register(): out of memory\n");
		exit(1);
	}

	memset(&pci_entries[n_pci_entries], 0, sizeof(struct pci_entry));

	pci_entries[n_pci_entries].name = strdup(name);
	pci_entries[n_pci_entries].initf = initf;
	n_pci_entries ++;
	return 1;
}


/*
 *  pci_lookup_initf():
 *
 *  Find a pci device init function by scanning the pci_entries array.
 *
 *  Return value is a function pointer, or NULL if the name was not found.
 */
void (*pci_lookup_initf(char *name))(struct machine *machine,
	struct memory *mem, struct pci_device *pd)
{
	int i;
	for (i=0; i<n_pci_entries; i++)
		if (strcmp(name, pci_entries[i].name) == 0)
			return pci_entries[i].initf;
	return NULL;
}


/*
 *  device_lookup():
 *
 *  Lookup a device name by scanning the device_entries array (as a binary
 *  search tree).
 *
 *  Return value is a pointer to the device_entry on success, or a NULL pointer
 *  if there was no such device.
 */
struct device_entry *device_lookup(char *name)
{
	int i, step, r, do_return = 0;

	if (name == NULL) {
		fprintf(stderr, "device_lookup(): NULL ptr\n");
		exit(1);
	}

	if (!device_entries_sorted)
		sort_entries();

	if (n_device_entries == 0)
		return NULL;

	i = n_device_entries / 2;
	step = i/2 + 1;

	for (;;) {
		if (i < 0)
			i = 0;
		if (i >= n_device_entries)
			i = n_device_entries - 1;

		/*  printf("device_lookup(): i=%i step=%i\n", i, step);
		    printf("  name='%s', '%s'\n", name,
		    device_entries[i].name);  */

		r = strcmp(name, device_entries[i].name);

		if (r < 0) {
			/*  Go left:  */
			i -= step;
			if (step == 0)
				i --;
		} else if (r > 0) {
			/*  Go right:  */
			i += step;
			if (step == 0)
				i ++;
		} else {
			/*  Found it!  */
			return &device_entries[i];
		}

		if (do_return)
			return NULL;

		if (step == 0)
			do_return = 1;

		if (step & 1)
			step = (step/2) + 1;
		else
			step /= 2;
	}
}


/*
 *  device_unregister():
 *
 *  Unregisters a device.
 *
 *  Return value is 1 if a device was unregistered, 0 otherwise.
 */
int device_unregister(char *name)
{
	ssize_t i;
	struct device_entry *p = device_lookup(name);

	if (p == NULL) {
		fatal("device_unregister(): no such device (\"%s\")\n", name);
		return 0;
	}

	i = (size_t)p - (size_t)device_entries;
	i /= sizeof(struct device_entry);

	free(device_entries[i].name);
	device_entries[i].name = NULL;

	if (i == n_device_entries-1) {
		/*  Do nothing if we're removing the last array element.  */
	} else {
		/*  Remove array element i by copying the last element
		    to i's position:  */
		device_entries[i] = device_entries[n_device_entries-1];

		/*  The array is not sorted anymore:  */
		device_entries_sorted = 0;
	}

	n_device_entries --;

	/*  TODO: realloc?  */
	return 1;
}


/*
 *  device_add():
 *
 *  Add a device to a machine. For example: "kn210 addr=0x12340000" adds a
 *  device called "kn210" at a specific address.
 *
 *  TODO: This function is quite ugly, and should be cleaned up.
 */
void *device_add(struct machine *machine, char *name_and_params)
{
	struct device_entry *p;
	struct devinit devinit;
	char *s2, *s3;
	size_t len;

	memset(&devinit, 0, sizeof(struct devinit));
	devinit.machine = machine;

	/*  Default values:  */
	devinit.addr_mult = 1;
	devinit.in_use = 1;

	/*  Get the device name first:  */
	s2 = name_and_params;
	while (s2[0] != ',' && s2[0] != ' ' && s2[0] != '\0')
		s2 ++;

	len = (size_t)s2 - (size_t)name_and_params;
	devinit.name = malloc(len + 1);
	if (devinit.name == NULL) {
		fprintf(stderr, "device_add(): out of memory\n");
		exit(1);
	}
	memcpy(devinit.name, name_and_params, len);
	devinit.name[len] = '\0';

	p = device_lookup(devinit.name);
	if (p == NULL) {
		fatal("no such device (\"%s\")\n", devinit.name);
		if (device_exit_on_error)
			exit(1);
		else
			goto return_fail;
	}

	/*  Get params from name_and_params:  */
	while (*s2 != '\0') {
		/*  Skip spaces, commas, and semicolons:  */
		while (*s2 == ' ' || *s2 == ',' || *s2 == ';')
			s2 ++;

		if (*s2 == '\0')
			break;

		/*  s2 now points to the next param. eg "addr=1234"  */

		/*  Get a word (until there is a '=' sign):  */
		s3 = s2;
		while (*s3 != '=' && *s3 != '\0')
			s3 ++;
		if (s3 == s2) {
			fatal("weird param: %s\n", s2);
			if (device_exit_on_error)
				exit(1);
			else
				goto return_fail;
		}
		s3 ++;
		/*  s3 now points to the parameter value ("1234")  */

		if (strncmp(s2, "addr=", 5) == 0) {
			devinit.addr = mystrtoull(s3, NULL, 0);
		} else if (strncmp(s2, "len=", 4) == 0) {
			devinit.len = mystrtoull(s3, NULL, 0);
		} else if (strncmp(s2, "addr_mult=", 10) == 0) {
			devinit.addr_mult = mystrtoull(s3, NULL, 0);
		} else if (strncmp(s2, "irq=", 4) == 0) {
			devinit.irq_nr = mystrtoull(s3, NULL, 0);
		} else if (strncmp(s2, "in_use=", 7) == 0) {
			devinit.in_use = mystrtoull(s3, NULL, 0);
		} else if (strncmp(s2, "name2=", 6) == 0) {
			char *h = s2 + 6;
			size_t len = 0;
			while (*h && *h != ' ')
				h++, len++;
			devinit.name2 = malloc(len + 1);
			if (devinit.name2 == NULL) {
				fprintf(stderr, "out of memory\n");
				exit(1);
			}
			snprintf(devinit.name2, len + 1, s2 + 6);
		} else {
			fatal("unknown param: %s\n", s2);
			if (device_exit_on_error)
				exit(1);
			else
				goto return_fail;
		}

		/*  skip to the next param:  */
		s2 = s3;
		while (*s2 != '\0' && *s2 != ' ' && *s2 != ',' && *s2 != ';')
			s2 ++;
	}


	/*
	 *  Call the init function for this device:
	 */

	devinit.return_ptr = NULL;

	if (!p->initf(&devinit)) {
		fatal("error adding device (\"%s\")\n", name_and_params);
		if (device_exit_on_error)
			exit(1);
		else
			goto return_fail;
	}

	free(devinit.name);
	return devinit.return_ptr;

return_fail:
	free(devinit.name);
	return NULL;
}


/*
 *  device_dumplist():
 *
 *  Dump a list of all registered devices.  (If the list is not sorted when
 *  this function is called, it is implicitly sorted.)
 */
void device_dumplist(void)
{
	int i;

	if (!device_entries_sorted)
		sort_entries();

	for (i=0; i<n_device_entries; i++) {
		debug("  %s", device_entries[i].name);

		/*  TODO: flags?  */

		debug("\n");
	}
}


/*
 *  device_set_exit_on_error():
 *
 *  This function selects the behaviour of the emulator when a device is not
 *  found. During startup, it is nicest to abort the whole emulator session,
 *  but if a device addition is attempted from within the debugger, then it is
 *  nicer to just print a warning and continue.
 */
void device_set_exit_on_error(int exit_on_error)
{
	device_exit_on_error = exit_on_error;
}


/*
 *  device_init():
 *
 *  Initialize the device registry, and call autodev_init() to automatically
 *  add all normal devices (from the src/devices/ directory).
 *
 *  This function should be called before any other device_*() function is used.
 */
void device_init(void)
{
	device_entries = NULL;
	device_entries_sorted = 0;
	n_device_entries = 0;

	autodev_init();
}

