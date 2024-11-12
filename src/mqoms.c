
/*
 * omnistat low level communication stuff, object-style
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <glib.h>
#include <glib_extra.h>
#include <mqoms.h>
#include <tty.h>
#include <asciiutils.h>
#include <omnistat.h>
#include <utils.h>

extern 	OmsChan *g_omc;
extern int g_verbose;

void oms_chan_reply_getg(OmsNode *nd, OmsMessage *msg);
int timeval_subtract (struct timeval *result,
		      struct timeval *a,
		      struct timeval *b);
	
OmsChan *oms_chan_open(char *devname)
{
	int fd = tty_open(devname);
	if(fd < 0) {
		fprintf(stderr, "open(%s): %s\n", devname, strerror(errno));
		return NULL;
	}

        if(tty_set(fd, 300, 8, 1, NO_PARITY) < 0) {
                tty_close(fd);
		fprintf(stderr, "ioctl(%s): %s", devname, strerror(errno));
                return NULL;
        }

        if(tty_raw(fd) < 0) {
                tty_close(fd);
		fprintf(stderr, "ioctl(%s): %s", devname, strerror(errno));
                return NULL;
        }
/*
 * The Omnistat's power-stealing isolated serial port seems
 * to generate a bogus start bit when DTR is asserted.
 * we make sure DTR is on, then wait a while for the character to get 
 * read (long 16C550 timeout) then flush the input buffer.
 *
 * tcflush(3) appears broken in glibc-2.0.7 when used with kernel 2.0.34,
 * so we do the ioctl instead.
 */
        int xbit = TIOCM_DTR;
        if(ioctl(fd, TIOCMBIS, &xbit) < 0)
		fprintf(stderr, "ioctl(%s,TIOCMBIS): %s", devname, strerror(errno));
        usleep(200000);
        if(ioctl(fd, TCFLSH, TCIFLUSH) < 0)
		fprintf(stderr, "ioctl(%s,TCIFLUSH): %s", devname, strerror(errno));

	OmsChan *omc = g_new0(OmsChan, 1);
	omc->fname = g_strdup(devname);
	omc->fd = fd;
	omc->timeout = 1250; // milliseconds
}

void oms_chan_close(OmsChan *omc)
{
	tty_close(omc->fd);
	if(omc->totimer) {
		g_source_remove(omc->totimer);
		omc->totimer = 0;
	}
}

OmsNode *
oms_chan_add_node(OmsChan *omc, guint addr, char *name)
{
	if(addr <= 0 || addr > 127) {
		fprintf(stderr, "oms_chan_add_node: invalid address %d for node named %s\n", addr, name);
		return NULL;
	}
	addr &= 0x7f;
	if(omc->nodes[addr]) {
		fprintf(stderr, "oms_chan_add_node: warning overwriting existing node addresss %d for node named %s\n", addr, name);
		g_free(omc->nodes[addr]);
	}
	OmsNode *nd = g_new0(OmsNode, 1);
	omc->nodes[addr] = nd;
	nd->omc = omc;
	nd->addr = addr;
	nd->name = g_strdup(name);
	return nd;
}

// print list of configured nodes
void
oms_chan_dump_nodes(OmsChan *omc)
{
	int a;
	printf("Channel %s\n", omc->fname);
	for(a = 0; a < 128; a++) {
		if(omc->nodes[a]) {
			OmsNode *nd = omc->nodes[a];
			printf("  [%d] \"%s\" state=%d\n", a, nd->name, nd->state);
		}
	}
}


/*
 * Clear a channel that is waiting for a reply so that it can be used
 * to send another message.  The next message in the queue is sent.
 *
 * This is used only  as a callback for the timer that is started 
 * when a message is dispatched.
 */
static gboolean
oms_chan_clear(void *p)
{
	OmsChan *omc = (OmsChan *)p;
	
	fprintf(stderr, "Omnistat: timeout on %s\n", omc->fname);
	omc->state =  KCH_STATE_IDLE;
	if(omc->totimer) {
		g_source_remove(omc->totimer);
		omc->totimer = 0;
	}
	if(omc->outstanding) {
		oms_chan_timeout_handler(omc, omc->outstanding, KE_TIMEOUT);
		g_free(omc->outstanding);
		omc->outstanding = NULL;
	}
	oms_chan_dispatch(omc);
	return FALSE;
}

/*
 * put a packet onto the wire
 */
void
oms_chan_dispatch(OmsChan *omc)
{
        int i, st;
        int len;
        int plen;
        unsigned char pbuf[256];

        if(omc->flags & KCH_FLAG_VERBOSE)
		oms_chan_print(omc);

        if(omc->state != KCH_STATE_IDLE) {
		fprintf(stderr, "omnistat(%s): dispatch called when not idle\n", omc->fname);
                return;
        }

        OmsMessage *msg;
        msg = g_list_shift(&omc->sendq);
        if(!msg) {  /* nothing to send: not an error */
                return;
        }
        if(msg->sdata == NULL || msg->slength < 1 || msg->slength > 16) {
		fprintf(stderr, "omnistat(%s) bad msg to dispatch: slen=%d sdata=0x%x\n",
			omc->fname, msg->slength, msg->sdata);
                return;
        }
        if(omc->flags & KCH_FLAG_VERBOSE) {
		oms_msg_print(msg, "in omnistat_dispatch for");
	}

        len = msg->slength - 1; /* length on the wire does not include command */
        pbuf[0] = msg->nodeno;
        pbuf[1] = (len<<4) | (msg->sdata[0] & 0x0f);
        if(len)
                memcpy(&pbuf[2], &msg->sdata[1], len);

        pbuf[len+2] = 0;
        for(i = 0; i < len+2; i++) {
                pbuf[len+2] += pbuf[i];
        }
        plen = len+3;

        if(omc->flags & KCH_FLAG_VERBOSE) {
                printf("omnistat_dispatch(%s)(len=%d; ", omc->fname, plen);
                xprint(stdout, pbuf, plen);
                putchar(')');
                putchar('\n');
        }

        write(omc->fd, pbuf, plen);

        omc->outstanding = msg;
        omc->state = KCH_STATE_RECV;

	omc->totimer = g_timeout_add(omc->timeout, oms_chan_clear, omc);
}

/* 
 * Routine to be called by event loop when there is data to read
 * on our channel's file descriptor.
 */
void
oms_chan_recv(OmsChan *omc)
{
        int n, i;
        int err = 0;
        unsigned char rbuf[32], c;
	OmsMessage *msg;

        n = read(omc->fd, rbuf, 32);
        if(n < 0) {
		fprintf(stderr, "omnistat %s read: %s", omc->fname, strerror(errno));
                return;
        }
        if(omc->flags & KCH_FLAG_VERBOSE) {
                printf("read(%d): ", n);
                xprint(stdout, rbuf, n);
                putchar('\n');
                fflush(stdout);
        }

        msg = omc->outstanding;
        for(i = 0; i < n; i++) {
                c=rbuf[i];
                switch(omc->state) {
                case KCH_STATE_IDLE:
                        if(c == FLAG_BYTE) {
                                fprintf(stderr, "start of message while idle");
                                omc->state = KCH_STATE_TYPE;
                                omc->rcrc = 0xff;   /* force CRC error */
                                omc->raddr = 0;
                        }
                        break;
                case KCH_STATE_RECV:
                        omc->rcrc = c;
                        omc->raddr = c;
                        omc->state = KCH_STATE_TYPE;
                        break;
                case KCH_STATE_TYPE:
                        omc->rcrc += c;
                        omc->rlen = (c >> 4) & 0x0f;
                        omc->rstatus = c & 0x0f;
			msg->rstatus = omc->rstatus;
			msg->rlength = 0;  // incremented below per data byte
			
                        if(omc->rlen == 0)
                                omc->state = KCH_STATE_CKSUM;
                        else
                                omc->state = KCH_STATE_DATA;
                        break;
                case KCH_STATE_DATA:
                        omc->rcrc += c;
                        if(msg && msg->rbuf && msg->rlength < OMNS_PKT_MAX){
                                msg->rbuf[msg->rlength++] = c;
                        }
                        omc->rlen--;
                        if(omc->rlen <= 0)
                                omc->state = KCH_STATE_CKSUM;
                        break;

		case KCH_STATE_CKSUM:
                        omc->state = KCH_STATE_IDLE;
                        
                        if(!msg)
                                break;
                        err = KE_NOERROR;
                        if(c != omc->rcrc) {
                                fprintf(stderr, "bad checksum; got %02x expected %02x", c, omc->rcrc);
                                err = KE_BADCRC;
                        } else if(omc->rstatus == OMMS_NACK) {
                                fprintf(stderr, "got NAK");
                                err = KE_NACK;
                        } else if(omc->raddr != (0x80|msg->nodeno) ) {
                                fprintf(stderr,
					"reply address=%02x expected %02x",
					omc->raddr, 0x80|msg->nodeno);
                                err = KE_BADADDR;
                        } else {
                                err = KE_NOERROR;
                        }

			if(omc->totimer) {
				g_source_remove(omc->totimer);
				omc->totimer = 0;
			}
			oms_chan_dispatch(omc);
			oms_chan_reply_handler(omc, msg, err);
                        break;
                }
        }
}

// called when there was a timeout waiting for reply
void
oms_chan_timeout_handler(OmsChan *omc, OmsMessage *msg, int err)
{
	OmsNode *nd;
	guint mt = msg->rstatus & 0x0f;
	guint nodeno = msg->nodeno;

	if(!omc->nodes[nodeno]) {
			fprintf(stderr, "timeout for nodeno %d but there's no node with that address\n");
			return;
	}
	nd = omc->nodes[nodeno];
	oms_nd_update_state(nd);   // might declare the node dead
}

// called with a complete message
void
oms_chan_reply_handler(OmsChan *omc, OmsMessage *msg, int err)
{
	OmsNode *nd;
	if(err == KE_NOERROR) {
		guint mt = msg->rstatus & 0x0f;
		guint nodeno = msg->nodeno;
		if(omc->flags & KCH_FLAG_VERBOSE)
			oms_msg_print(msg, "oms_reply_handler");
		if(!omc->nodes[nodeno]) {
			fprintf(stderr, "recieved reply for nodeno %d but there's no node with that address\n");
			return;
		}
		nd = omc->nodes[nodeno];
		nd->last_resp = time(NULL);
		switch(mt) {
		case OMMS_ACK:	// ACK  (likely successful reg write)
			break;
		case OMMS_NACK:	// NAK  (likely invalid register address)
			break;
		case OMMS_DATA:  // register returndata
			oms_chan_reply_regdata(nd, msg);
			break;
		case OMMS_GRP1: // group 1 data
			oms_chan_reply_getg(nd, msg);
			break;
		case OMMS_GRP2: // group 2 data
			break;
		default:
			
		}
		if(nd->state != NODE_ALIVE) {
			oms_nd_update_state(nd);
		}
	} else {
		if(omc->flags & KCH_FLAG_VERBOSE)
			fprintf(stderr, "oms_reply_handler err=%d\n", err);
			oms_msg_print(msg, "oms_reply_handler:error");
	}
}

// unpack message reply contining group1 data.
void
oms_chan_reply_getg(OmsNode *nd, OmsMessage *msg)
{
	if(msg->rlength != 6) {
		fprintf(stderr, "oms_chan_reply_getg: addr=%d rlength=%d expected 6\n", nd->addr, msg->rlength);
		return;
	}
	nd->setpoint_cool = omcf_temp(msg->rbuf[0], 1);
	nd->setpoint_heat = omcf_temp(msg->rbuf[1], 1);
	nd->mode = msg->rbuf[2];
	nd->fanmode = msg->rbuf[3];
	nd->hold = msg->rbuf[4];

	char dbuf[64];
	char topic[128];

	nd->cur_temp = omcf_temp(msg->rbuf[5], 1);
	sprintf(dbuf, "%.1f", nd->cur_temp);
	sprintf(topic, "omnistat/%s/current", nd->name);
	mqtt_publish(topic, dbuf);
	
	if(nd->omc->flags & KCH_FLAG_VERBOSE) {
		omcs_temp(dbuf, msg->rbuf[0]);
                printf(" cool setpoint: %s\n", dbuf);
                omcs_temp(dbuf, msg->rbuf[1]);
                printf(" heat setpoint: %s\n", dbuf);
                omcs_mode(dbuf, msg->rbuf[2]);
                printf("          mode: %s\n", dbuf);
                omcs_fanm(dbuf, msg->rbuf[3]);
                printf("           fan: %s\n", dbuf);
                omcs_hold(dbuf, msg->rbuf[4]);
                printf("          hold: %s\n", dbuf);
                omcs_temp(dbuf, msg->rbuf[5]);
                printf("  current temp: %s\n", dbuf);
	}
}

// unpack message reply contining register read data.
void
oms_chan_reply_regdata(OmsNode *nd, OmsMessage *msg)
{
	if(msg->rlength < 2 ) {
		fprintf(stderr, "oms_chan_reply_regdata: addr=%d rlength=%d expected >= 2\n", nd->addr, msg->rlength);
		return;
	}
	guint startreg = msg->rbuf[0];
	for(int i = 0; i < msg->rlength-1; i++) {
		oms_nd_regdata(nd, startreg+i, msg->rbuf[i+1]);
	}
}

// store and optionally publish register value
void
oms_nd_regdata(OmsNode *nd, guint regaddr, guchar val)
{
	int model;
	if(nd->omc->flags & KCH_FLAG_VERBOSE) {
		printf("reg[0x%02x]: newval 0x%02x\n", regaddr, val);
	}
	if(regaddr == OM_REGADDR_MODEL)
		nd->model = val;
	model = nd->model;   // special case because we need the model code to do the others!

	char dbuf[MQSTRSIZE];
	char topic[MQSTRSIZE];
	
	struct omst_reg *regtab = om_model_table(model);
	int max_regs = om_model_table_size(model);
	if(regtab && regaddr < max_regs) {
		if(regtab[regaddr].flags & PUBA
		   || (nd->reg_cache[regaddr].flags & PUB_NEXT)
		   || ( (regtab[regaddr].flags & PUBC)
			&& (val != nd->reg_cache[regaddr].val))
		   || ( (regtab[regaddr].flags & PUBC)
			&& (nd->reg_cache[regaddr].vtime < 10)) ) {
			snprintf(topic, MQSTRSIZE, "omnistat/%s/%s", nd->name, regtab[regaddr].topic);
			omcs_regval(dbuf, regaddr, val, model);
			mqtt_publish(topic, dbuf);
			nd->reg_cache[regaddr].flags &= ~PUB_NEXT;
		}
	}

	nd->reg_cache[regaddr].val = val;
	nd->reg_cache[regaddr].vtime = time(NULL);

	if(regaddr == OM_REGADDR_CURRENT_TEMP) {
		nd->cur_temp = omcf_temp(val, 1);
	}
}

void
oms_chan_enqueue_msg(OmsChan *omc, OmsMessage *msg)
{
	msg->omc = omc;
	msg->qtime = time(NULL);
	if(omc->flags & KCH_FLAG_VERBOSE) {
		printf("enqueue id=%d cmd=%d\n", msg->id, msg->sdata[0]);
	}
		
	omc->sendq = g_list_append(omc->sendq, msg);
	if(omc->state == KCH_STATE_IDLE)
                oms_chan_dispatch(omc);
}

void
oms_msg_print(OmsMessage *msg, char *str)
{
	int i;
	if(str)
		printf("om_msg(\"%s\") ", str);
	printf("id=%d scmd=%d slen=%d", msg->id, msg->sdata[0], msg->slength);
	if(msg->slength > 1) {
		printf(":(");
		for(i = 1; i < msg->slength; i++)
			printf(" %02x", msg->sdata[i]);
		printf(" )");
	}
	printf("   rlength=%d rstatus=%d", msg->rlength, msg->rstatus);
	if(msg->rlength > 0) {
		printf(":(");
		for(i = 0; i < msg->rlength; i++)
			printf(" %02x", msg->rbuf[i]);
		printf(" )");
	}
	printf("\n");
}

/* helper */
void
oms_msg_print_wrap(gpointer p, gpointer d)
{
	OmsMessage *msg = (OmsMessage *)p;
	oms_msg_print(msg, NULL);
}
/* print the whole queue of pending messages to be sent */
void
oms_chan_print(OmsChan *omc)
{
	printf("--\nOmsChan(%s): state=%d\n", omc->fname, omc->state);
	g_list_foreach(omc->sendq, oms_msg_print_wrap, NULL);
	printf("--\n");
}

/*
 * create message structure, fill in the message body, and enqueue it.
 */
void
oms_chan_send_msg(OmsChan *omc, int addr, int scmd, unsigned char *sbuf, int sblen)
{
	static guint msgid;
	OmsMessage *msg = g_new0(OmsMessage, 1);
	int slength = MIN(sblen+1, 16);
	msg->id = ++msgid;
	msg->nodeno = addr & 0x7f;
	msg->slength = slength;
	msg->sdata[0] = scmd & 0x0f;
	if(slength > 1)
		memcpy(&msg->sdata[1], sbuf, slength);

	oms_chan_enqueue_msg(omc, msg);
}

/* send a "get group 1" message */
void
oms_chan_send_msg_getg(OmsChan *omc, int addr)
{
        unsigned char sbuf[4];
	int groupno = 1;
	unsigned char msgtype = OMMT_GETG;

	oms_chan_send_msg(omc, addr, msgtype, sbuf, 0);
}

/* like above, but using OmsNode*
 * which API do we like using more?
 */
void
oms_node_send_msg_getg(OmsNode *nd)
{
	OmsChan *omc = nd->omc;
	int addr = nd->addr;

	oms_chan_send_msg_getg(omc, addr);
}

void
oms_node_send_msg_readregs(OmsNode *nd, int startreg, unsigned int count)
{
	OmsChan *omc = nd->omc;
        unsigned char sbuf[4];
	unsigned char msgtype = OMMT_GETREG;
	sbuf[0] = startreg & 0xff;
	if(count > 14)
		count = 14;
	sbuf[1] = count;

	oms_chan_send_msg(omc, nd->addr, msgtype, sbuf, 2);
}

void
oms_node_send_msg_setregs(OmsNode *nd, unsigned char *sbuf, unsigned int count)
/* first byte is starting register address, rest are data.
   count includes starting register address.
   ugly but avoids a copy.
 */
{
	OmsChan *omc = nd->omc;
	unsigned char msgtype = OMMT_SETREG;
	if(count > 15)
		count = 15;
	oms_chan_send_msg(omc, nd->addr, msgtype, sbuf, count);
}

/* set a thermostat's time from system clock */
void oms_node_set_clock(OmsNode *nd)
{
        unsigned char sbuf[16];
        time_t nowt;
        struct tm *nowtm;
        int rc, rlen;

        time(&nowt);
        nowtm = localtime(&nowt);

        /* I don't want to think about whether the thermostat
         * understands leap seconds */
        if(nowtm->tm_sec > 59)
                nowtm->tm_sec = 59;

        sbuf[0] = 0x3a;
        /* thermostat has 0=monday, struct tm has 0=sunday */
        if(nowtm->tm_wday == 0)
                sbuf[1] = 6;
        else
                sbuf[1] = nowtm->tm_wday - 1;
                
        oms_node_send_msg_setregs(nd, sbuf, 2);

        sbuf[0] = 0x41;
        sbuf[1] = nowtm->tm_sec;
        sbuf[2] = nowtm->tm_min;
        sbuf[3] = nowtm->tm_hour;
        oms_node_send_msg_setregs(nd, sbuf, 4);
}

// update the alive/intermediate/dead state of a node
// safe to call this at any time, although usually called when a reply is recieved
// or failed to be recieved due to a timeout.
void
oms_nd_update_state(OmsNode *nd)
{
	time_t now = time(NULL);
	int oldstate = nd->state;
	const int node_timeout = 130; // TODO configurable parameter
	
	if( (now - nd->last_resp) > node_timeout) {  
		nd->state = NODE_DEAD;
		if(nd->state != oldstate) {
			if(g_verbose) { // TODO verbose per node? inherit from channel?
				fprintf(stderr, "omnistat(%s) dead: %d seconds since last reponse (oldstate=%d)\n",
					nd->name,  (now - nd->last_resp), oldstate );
			}
			char topic[128];
			sprintf(topic, "omnistat/%s/state", nd->name);
			mqtt_publish(topic, "dead");
			// require recent model to call it alive again
			nd->reg_cache[OM_REGADDR_MODEL].vtime = 0;
		}
	}
	if( (now - nd->last_resp) < 10) {  // some recent reply
		int recent_model  = (now - nd->reg_cache[OM_REGADDR_MODEL].vtime  < 3700);
		int recent_temp  = (now - nd->reg_cache[OM_REGADDR_CURRENT_TEMP].vtime  < 130);
		
		// if recent device model and recent temp status, its alive
		if( recent_model && recent_temp) {
			nd->state = NODE_ALIVE;
		} else {
			nd->state = NODE_WAKEUP;
			if(!recent_model) { // if not recent device model message, ask for it
				oms_node_send_msg_readregs(nd, OM_REGADDR_MODEL, 1);
			}
			if(!recent_temp) { // if not recent temp status, query for that
				oms_node_send_msg_readregs(nd, OM_REGADDR_STATUS, OM_REGADDR_STATUS_LEN);
			}
		}
		if(nd->state != oldstate) {
			fprintf(stderr, "omnistat(%s) state=%d oldstate=%d\n",
				nd->name,  nd->state, oldstate );
			if(nd->state == NODE_ALIVE) {
				if(g_verbose) { // TODO verbose per node? inherit from channel?
					fprintf(stderr, "omnistat(%s) alive\n", nd->name);
				}
				char topic[128];
				sprintf(topic, "omnistat/%s/state", nd->name);
				mqtt_publish(topic, "alive");
				oms_node_set_clock(nd);
			}
		}
	}
}


// do per-minute status collection for the list of known devices
// TODO if node is dead, could send different probe message, perhaps less frequently.
void
oms_list_per_minute()
{
	OmsChan *omc = g_omc;
	OmsNode *nd;
	int i;
	for(i = 1; i < 128; i++) {
		if(omc->nodes[i]) {
			nd = omc->nodes[i];
//			oms_node_send_msg_getg(nd);
			oms_node_send_msg_readregs(nd, OM_REGADDR_STATUS, OM_REGADDR_STATUS_LEN);
		}
	}
}

void
oms_list_per_hour()
{
	OmsChan *omc = g_omc;
	OmsNode *nd;
	int i;
	for(i = 1; i < 128; i++) {
		if(omc->nodes[i]) {
			nd = omc->nodes[i];
			oms_node_set_clock(nd);
			oms_node_send_msg_readregs(nd, OM_REGADDR_MODEL, 1);
		}
	}
}


static guint per_minute_timer;

gboolean
per_minute_callback(gpointer data)
{
	static int last_hour = -1;
	if(data) {
		oms_list_per_minute();
	}
	
	struct timeval now;
	struct timeval next_minute;
	gettimeofday(&now, NULL);
	struct tm *tm = localtime(&now.tv_sec);
	char buf[256];
	strftime(buf, 256, "%F %T", tm);
	printf("per_minute_callback at %s.%06u\n", buf, now.tv_usec);

	if(tm->tm_hour != last_hour) {    // causes this to be called at startup too
		last_hour = tm->tm_hour;
		printf("per_hour_callback at %s.%06u\n", buf, now.tv_usec);
		oms_list_per_hour();
	}

	int seconds_into_minute = (now.tv_sec) % 60;
//	printf("    %d seconds into minute \n", seconds_into_minute);
		
	next_minute = now;
	next_minute.tv_sec += (60 - seconds_into_minute);
	next_minute.tv_usec = 0;
	tm = localtime(&next_minute.tv_sec);
	strftime(buf, 256, "%F %T", tm);
//	printf(" next minute will be %s.%06u:  (:%02d)\n", buf, next_minute.tv_usec, (next_minute.tv_sec % 60) );
	
	struct timeval delta;
	timeval_subtract(&delta, &next_minute, &now);
//	printf("  delta timeval %d.%06d\n", delta.tv_sec, delta.tv_usec);

	int delta_ms = (delta.tv_sec * 1000) + delta.tv_usec / 1000 + 1;

//	printf("   %d ms until next minute\n", delta_ms );

/*	struct tm *tm = localtime(&now.tv_sec);
	char buf[256];
	strftime(buf, 256, "%F %T", tm);
	printf("per_minute_callback at %s.%06u:  %d ms until next minute\n", buf, now.tv_usec, delta_ms );
*/	
	per_minute_timer = g_timeout_add(delta_ms, per_minute_callback, g_omc);

	return FALSE; // old one will get dropped
}

void
per_minute_init()
{
//	per_minute_callback(NULL);
	per_minute_callback((gpointer)0xffffffff);  // go ahead and send the get-group data
}

OmsNode *
mqoms_find_node(OmsChan *omc, char *name)
{
	for(int i = 0; i < 128; i++) {
		if(omc->nodes[i]) 
			if(strcmp(omc->nodes[i]->name, name) == 0)
				return omc->nodes[i];
	}
	return NULL;
}

void
mq_recv_message(char *topic, char *payload)
{
	char *me = strtok(topic, "/");
	if(strcmp(me, "omnistat") != 0)
		return;
	char *tstatname = strtok(NULL, "/");
	char *cmd = strtok(NULL, "/");

	if(strcmp(cmd, "getreg") != 0
		&& strcmp(cmd, "set") != 0)  // early reject of things we don't respond to, including things we send!
		return;
	
	char *regname = strtok(NULL, "/");

	printf("recieved mqtt message: %s %s", me, tstatname);
	if(cmd)
		printf(" cmd=%s", cmd);
	if(regname)
		printf(" reg=%s", regname);
	printf("\n");
	
	OmsNode *nd = mqoms_find_node(g_omc, tstatname);

	// if tstatname == "server"  // maybe maintenance or diagnostic commands for us here
	if(nd) {
		printf("  target node=%s addr=%d payload=%s\n", nd->name, nd->addr, payload);
		if(strcmp(cmd, "getreg") == 0) {
			oms_nd_get_reg_str(nd, regname);

		} else if(strcmp(cmd, "set") == 0) {
			oms_nd_set_reg_str(nd, regname, payload);
		}
	}
}


// set a thermostat register to a new value
// register name and new value are both strings to be looked up and converted
// to register number and binary-byte value
void
oms_nd_set_reg_str(OmsNode *nd, char *regname, char *valstr)
{
	uint8_t sbuf[16];
	struct omst_reg *regtab = om_model_table(nd->model);   // TODO avoid doing this twice, once here
	int regno = oms_nd_lookup_reg_by_topic(nd, regname); 	// and again in here
	if(g_verbose)
		printf("nd_set_reg_str regno=0x%02x\n", regno);
	if(regno >= 0) {
		uint8_t valbyte = regtab[regno].cvt_byte(valstr);

		if(g_verbose) {
			printf("nd_set_reg_str(%s, regname=%s valstr=%s valbyte=0x%02x\n",
			       nd->name, regname, valstr, valbyte);

		}
		sbuf[0] = regno;
		sbuf[1] = valbyte;
		oms_node_send_msg_setregs(nd, sbuf, 2);
		oms_node_send_msg_readregs(nd, regno, 1);  // read it back, and maybe publish
	}
}

// retrieve a value from a register.
// maybe first check the cache?
// but likely have to send a read to the thermostat, and arrange for the reply value to get published
// later when its recieved.
void
oms_nd_get_reg_str(OmsNode *nd, char *regname)
{
	int regno = oms_nd_lookup_reg_by_topic(nd, regname); 	// and again in here
	if(regno >= 0) {
		nd->reg_cache[regno].flags |= PUB_NEXT;	// publish on next read-reply
		oms_node_send_msg_readregs(nd, regno, 1);  // que msg to do the read
	}
}


// find register whose mqtt-topic matches regname, and return the register number.
// topics can depend on thermostat model, so look in the right table.
// return -1 if not found.
int
oms_nd_lookup_reg_by_topic(OmsNode *nd, char *regname)
{
	struct omst_reg *regtab = om_model_table(nd->model);
	int max_regs = om_model_table_size(nd->model);

	if(regtab) {
		for(int i = 0; i < max_regs; i++) {
			if(regtab[i].topic && (strcmp(regtab[i].topic, regname) == 0))
				return i;
		}
		return -1;
	} else
		return -1;
}
