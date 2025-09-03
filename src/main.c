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
char *g_mqtt_host = NULL;
int g_mqtt_port = -1;

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

	mqtt_setup(mainloop, g_mqtt_host, g_mqtt_port);
	
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

GKeyFile *g_cfg_file;
char *g_devname;

int
read_config_file(char *fname)
{
	g_autoptr(GError) error = NULL;
	g_cfg_file = g_key_file_new ();
	int flags = 0;

	if (!g_key_file_load_from_file (g_cfg_file, fname, flags, &error)) {
		if (!g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
			g_warning ("Error loading config file: %s", error->message);
		return 1;
	}
	g_devname = g_key_file_get_string (g_cfg_file, "server", "device", &error);
	if (g_devname == NULL &&
	    !g_error_matches (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND)) {
		g_warning ("Error finding key in key file: %s", error->message);
		return 1;
	} 

	g_mqtt_host = g_key_file_get_string (g_cfg_file, "server", "mqtt_host", &error);
	char *mport_str = g_key_file_get_string (g_cfg_file, "server", "mqtt_port", &error);
	if(mport_str)
		g_mqtt_port = atoi(mport_str);
	else
		g_mqtt_port = -1;
}

int
nodes_from_config_file()
{
	gsize ngroups;
	gchar** groups = g_key_file_get_groups (g_cfg_file, &ngroups);
	g_autoptr(GError) error = NULL;
	int i;
	int addr;
	char *name;
	int enabled;
	for(i = 0; i < ngroups; i++) {
//		printf("group: %s:\n", groups[i]);
		if(strcmp(groups[i], "server")) {
			addr = -1;
			name = NULL;
			enabled = 0;

			name = g_key_file_get_string (g_cfg_file, groups[i], "name", &error);

//		if(name == NULL || !g_error_matches (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND)) {
//			printf("  name: %s\n", error->message);
//		} else
//			printf("  name=%s\n", name);
	
			error = NULL;
			addr = g_key_file_get_integer (g_cfg_file, groups[i], "address", &error);
			if(!g_error_matches (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND)) {
//			printf("  addr: %s\n", error->message);
			} 
			else
//				printf("  addr=%d\n", addr);
		
			error = NULL;
			enabled = g_key_file_get_boolean (g_cfg_file, groups[i], "enabled", &error);
			if(!g_error_matches (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND)) {
//			printf("  enabled: %s\n", error->message);
			} 

//			printf("  name=%s addr=%d enab=%d\n", name, addr, enabled);
			if(enabled && name && addr > 0) {
				oms_chan_add_node(g_omc, addr, name);
				g_free(name);
			}
		}
	}
	oms_chan_dump_nodes(g_omc);
}


void usage()
{
        fprintf(stderr, "Usage: %s [options] <device> ... \n", g_progname);
        fprintf(stderr, "Options:\n");
        fprintf(stderr, "\t-c config file name\n");
        fprintf(stderr, "\t-d serial device name\n");
        fprintf(stderr, "\t-a thermostat-address\n");
        fprintf(stderr, "\t-n thermostat-name\n");
        fprintf(stderr, "\t-v verbose\n");
}

int
main(int argc, char **argv) 
{
        char *dev = NULL;
        GError *error;
        extern int optind;
        extern char *optarg;
        int errflg = 0;
        int c;

	int opt_a = 0;
	char *opt_n = NULL;
	char *opt_c = NULL;
	char *opt_d = NULL;
		
	g_progname = argv[0];
        while ((c = getopt (argc, argv, "a:c:d:n:vx")) != EOF) {
                switch(c) {
                case 'a':
                        opt_a = strtoul(optarg, NULL, 0);
                        break;
                case 'c':
                        opt_c = optarg;
                        break;
                case 'd':
                        opt_d = optarg;
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
        if(optind != argc)  {
		printf("optind=%d argc=%d\n", optind, argc);
                usage();
                exit(1);
        }

	if(opt_c) {
		read_config_file(opt_c);
	}

	if(opt_d) {
		if(g_devname)
			g_free(g_devname);
		g_devname = g_strdup(opt_d);
	}	
	if(!g_devname) {
		printf("serial device must be specified in config file or with the -d option\n");
                usage();
                exit(1);
	}
	
	g_omc = oms_chan_open(g_devname);
	if(!g_omc)
		exit(1);

	if(g_verbose)
		g_omc->flags |= KCH_FLAG_VERBOSE;

	if(opt_c)
		nodes_from_config_file();
	else {
		if(!opt_a || !opt_n) {
			fprintf(stderr, "either specify -c config-file or -n nodename -a address\n");
			exit(1);
		}
		oms_chan_add_node(g_omc, opt_a, opt_n);
	}
	
	per_minute_init();
	
        setlinebuf(stdout);
        setlinebuf(stderr);
        printf("mqomstatd[%d] starting on %s\n", getpid(), dev);

        poll_loop(g_omc);

        exit(0);
}
