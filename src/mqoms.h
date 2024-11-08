/*
 * mqoms.h
 *      mqtt-omnistat definitions
 *
 * $Id$
 * Copyright 2006 Steve Tell.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this software; see the file COPYING.GPL.  If not, write to
 * the Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307 USA
 *
 */

#ifndef MQOMS_H
#define MQOMS_H

#include <stdint.h>
#include <omnistat.h>

// going back & forth about whether these defs belong in omnistat.h or here

// errors detectable in message-handling code
#define KE_NOERROR      0
#define KE_TIMEOUT      1
#define KE_BADCRC       2
#define KE_NOTACK       3
#define KE_NACK         4
#define KE_WRONGDEST    5
#define KE_BADADDR      6
#define KE_WRONGCMD     7
#define KE_EOF          8
/* other non-error events used for tracing */
#define KF_DISPATCH     16

#define KCH_STATE_ZOMBIE        -1
#define KCH_STATE_IDLE          0
#define KCH_STATE_RECV          1
#define KCH_STATE_TYPE          2
#define KCH_STATE_DATA          3
#define KCH_STATE_CKSUM         4

#define KCH_FLAG_VERBOSE        1
#define KCH_FLAG_TRACE          2

typedef struct _OmsMessage OmsMessage;
typedef struct _OmsNode OmsNode;

// structure for omnistat communication channel - aka one serial port
struct _OmsChan {
	char *fname;
	int fd;
	int debug;
	int state;  // state of packet recieve state machine
	int flags;

        int rlen;  /* number of bytes remaining to be read in packet */
        unsigned char rcrc; /* running CRC while reading data */
        unsigned char raddr;  /* message address */
        unsigned char rstatus;  /* command node is replying to */

	guint timeout;  // timeout value, milliseconds
	guint totimer; 	// glib timer-source id for reply timeout, 0 if none
	
	// todo queue of messages to send
	GList *sendq;
	
	// packet out on the wire awaiting response
	OmsMessage *outstanding;

	// per-thermostat structures.  max 127 on a wire, so just an array.
	OmsNode *nodes[128];
};
typedef struct _OmsChan OmsChan;

// packet queued to be sent, or awaiting response.
struct _OmsMessage {
	OmsChan *omc;
        guint flags;
	guint id;

        time_t qtime;   // todo higher resolution
        time_t sendtime;

	guchar nodeno;  	// thermostat address
        guint slength;             // packet length to send, minimum 1
        guchar sdata[OMNS_PKT_MAX];  // first is always the cmd byte

	guint rlength;
	guint rstatus;
        guchar rbuf[OMNS_PKT_MAX];   // space for the reply

	int serial;  /* message serial number */
};
typedef struct _OmsMessage OmsMessage;

enum omsRegValFlags {
	PUB_NEXT = 1
};

// data about each register in the thermostat
struct _OmsRegVal {
	time_t vtime;		// time last value recieved
	uint8_t  val; 		// raw value
	enum omsRegValFlags  flags;  // PUB_NEXT, etc.
};
typedef struct _OmsRegVal OmsRegVal;

#define NODE_DEAD	0
#define NODE_WAKEUP	1   // seen some replies but not enough to call it alive.
#define NODE_ALIVE	2   // seen requisite replies, and one recently enough.

// data about a single thermostat on the multidrop serial port
struct _OmsNode {
	OmsChan *omc;
	int addr;
	char *name;
	int model;
	int state;

	// todo many more fields to come about the current state of this thermostat
	float setpoint_cool;
	float setpoint_heat;
	float cur_temp;
	int mode;
	int fanmode;
	int hold;
	time_t last_resp;

	OmsRegVal reg_cache[256];
};


extern int mqtt_setup();
extern void mqtt_shutdown();
extern void mqtt_publish(char *topic, char *msg);

OmsChan *oms_chan_open(char *fname);
void oms_chan_close(OmsChan *omc);
void oms_chan_recv(OmsChan *omc); // called when select() says there's somthing to read on the fd.
void oms_chan_dispatch(OmsChan *omc);
extern void oms_chan_print(OmsChan *omc);
OmsNode *oms_chan_add_node(OmsChan *omc,  guint node, char *name);
void oms_chan_timeout_handler(OmsChan *omc, OmsMessage *msg, int err);
void oms_chan_reply_handler(OmsChan *omc, OmsMessage *msg, int error);

extern void oms_chan_reply_regdata(OmsNode *nd, OmsMessage *msg);
extern void oms_nd_regdata(OmsNode *nd, guint regaddr, guchar val);
extern void oms_nd_update_state(OmsNode *nd);
void oms_msg_print(OmsMessage *msg, char *str);
void per_minute_init();
extern void mq_recv_message(char *topic, char *payload	);

extern int oms_nd_lookup_reg_by_topic(OmsNode *nd, char *regname);
extern void oms_nd_set_reg_str(OmsNode *nd, char *regname, char *valstr);
extern void oms_nd_get_reg_str(OmsNode *nd, char *regname);

#define MQSTRSIZE 128

#endif
