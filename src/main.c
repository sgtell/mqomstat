/*
 * mqtt server for HAI omnistat
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

int g_verbose;
int g_debug;
char *g_progname;

OmsChan *g_omc = NULL;   // just one for now.  todo: more serial channels

/*
 * for g_main_loop and file-descriptor events, in addition to glib docs,
 * see also:
 * http://mail.gnome.org/archives/gtk-devel-list/2001-May/msg00192.html
 */
gboolean mqoms_gio_dispatch(
        GIOChannel *source, 
	GIOCondition condition,
	gpointer user_data)
{
        OmsChan *omc = (OmsChan *)user_data;
	oms_chan_recv(omc);
}

static gboolean signal_handler(gpointer user_data)
{
	GMainLoop *loop = (GMainLoop *)user_data;
	g_main_loop_quit(loop);
	return G_SOURCE_CONTINUE;
}

// set up and start the main event loop,
// connectting the serial port and mqtt socket to get polled.
// still not sure the best way to modularlize all this.
void
poll_loop(OmsChan *omc)
{
        GMainLoop *mainloop;
        mainloop = g_main_loop_new(NULL, TRUE);

	mqtt_setup(mainloop);
	
	// Add signal handlers for graceful shutdown
	guint signal_handler_id = g_unix_signal_add(SIGTERM, signal_handler, mainloop);
	g_unix_signal_add(SIGINT, signal_handler, mainloop);
	
	if(omc) {
		GIOChannel *chan = g_io_channel_unix_new(omc->fd);
		g_io_add_watch(chan, G_IO_IN | G_IO_HUP | G_IO_ERR,
                       mqoms_gio_dispatch,
                       (gpointer)omc);
	}

	if(g_verbose)
		printf("starting main loop");
        g_main_loop_run(mainloop);
}

void usage()
{
        fprintf(stderr, "Usage: %s [options] <file> ... \n", g_progname);
        fprintf(stderr, "Options:\n");
        fprintf(stderr, "\t-a thermostat-address\n");
        fprintf(stderr, "\t-n thermostat-name\n");
        fprintf(stderr, "\t-v verbose\n");
}

int
main(int argc, char **argv) 
{
        char *dev;
        GError *error;
        extern int optind;
        extern char *optarg;
        int errflg = 0;
        int c;

	int opt_a = 0;
	char *opt_n = NULL;
	g_progname = argv[0];
        while ((c = getopt (argc, argv, "a:n:vx")) != EOF) {
                switch(c) {
                case 'a':
                        opt_a = strtoul(optarg, NULL, 0);
                        break;
                case 'n':
                        opt_n = g_strdup(optarg);
                        break;
                case 'v':
                        g_verbose = 1;
                        break;
                case 'x':
                        g_debug = 1;
                        break;
                default:
                        errflg = 1;
		}
	}
        if(errflg) {
                usage();
                exit(1);
        }
        if(optind >= argc)  {
                fprintf(stderr, "usage: %s serial-device\n", g_progname);
                exit(1);
        } else {
		dev = argv[optind];
        }

	if(!opt_a)
		opt_a = 1;
	if(!opt_n)
		opt_n = g_strdup("hvac");

	g_omc = oms_chan_open(dev);
	if(!g_omc)
		exit(1);
//	oms_list_init_1(opt_a, opt_n);
	if(g_verbose)
		g_omc->flags |= KCH_FLAG_VERBOSE;
	oms_chan_add_node(g_omc, opt_a, opt_n);
	
	per_minute_init();
	
        setlinebuf(stdout);
        setlinebuf(stderr);
        printf("mqomstatd[%d] starting on %s\n", getpid(), dev);

        poll_loop(g_omc);

        exit(0);
}
