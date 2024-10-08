
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

extern 	OmsChan *g_omc;

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
	// could log more about the message that went nowhere, before we drop it
	omc->state =  KCH_STATE_IDLE;
	if(omc->totimer) {
		g_source_remove(omc->totimer);
		omc->totimer = 0;
	}
	if(omc->outstanding) {
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
	if(nd->omc->flags & KCH_FLAG_VERBOSE) {
		printf("reg[0x%02x]: newval 0x%02x\n", regaddr, val);
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
	printf("id=%d slen=%d scmd=%d ", msg->id, msg->slength, msg->sdata[0]);
	if(msg->slength > 1) {
		printf("(");
		for(i = 1; i < msg->slength; i++)
			printf(" %02x", msg->sdata[i]);
		printf(")");
	}
	printf("   rlength=%d rstatus=%d:", msg->rlength, msg->rstatus);
	if(msg->rlength > 0) {
		printf("(");
		for(i = 0; i < msg->rlength; i++)
			printf(" %02x", msg->rbuf[i]);
		printf(")");
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


// do per-minute status collection for the list of known devices
void
oms_list_per_minute()
{
	OmsChan *omc = g_omc;
	OmsNode *nd;
	int i;
	for(i = 1; i < 128; i++) {
		if(omc->nodes[i]) {
			nd = omc->nodes[i];
			oms_node_send_msg_getg(nd);
			oms_node_send_msg_readregs(nd, 0x3b, 14);
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

	if(tm->tm_hour != last_hour) {
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


// compute x - y for two timevals.
int
timeval_subtract (struct timeval *result, struct timeval *x, struct timeval *y)
{
       /* Perform the carry for the later subtraction by updating y. */
       if (x->tv_usec < y->tv_usec) {
         int nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;
         y->tv_usec -= 1000000 * nsec;
         y->tv_sec += nsec;
       }
       if (x->tv_usec - y->tv_usec > 1000000) {
         int nsec = (x->tv_usec - y->tv_usec) / 1000000;
         y->tv_usec += 1000000 * nsec;
         y->tv_sec -= nsec;
       }
     
       /* Compute the time remaining to wait.
          tv_usec is certainly positive. */
       result->tv_sec = x->tv_sec - y->tv_sec;
       result->tv_usec = x->tv_usec - y->tv_usec;
     
       /* Return 1 if result is negative. */
       return x->tv_sec < y->tv_sec;
}
