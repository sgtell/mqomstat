/*
 * handle list of nodes on the omnistat shared serial port.
 * for now, a stub that just handles one of them.
 */


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <glib.h>
#include <glib-unix.h>
#include <MQTTClient.h>
#include <mqoms.h>
#include <oms_list.h>

extern OmsChan *g_omc;

static int oms_address;
static char *oms_name;

void
oms_list_init_1(int address, char *name)
{

	oms_address = address;
	oms_name = name;
}

// do per-minute status collection for the list of known devices
void
oms_list_per_minute()
{
	oms_chan_send_msg_getg(g_omc, oms_address)
}
