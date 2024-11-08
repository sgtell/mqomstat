#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <glib.h>
#include <mosquitto.h>
#include <mqoms.h>

#define HOST "localhost"
#define ADDRESS     "tcp://localhost:1883"
#define TOPIC       "omnistat/#"
#define TIMEOUT     10000L

static struct mosquitto *mosq;
static GIOChannel *mosq_gio_chan;
static int mosq_fd;

static guint mosq_misc_timer_tag = 0;
static GSource *mosquitto_source;

void on_connect(struct mosquitto *mosq, void *obj, int rc)
{
    if (rc == 0) {
        printf("Connected to mqtt broker\n");
        mosquitto_subscribe(mosq, NULL, TOPIC, 1);
	// TODO hook to subscribe to more
    } else {
        fprintf(stderr, "Failed to connect, return code %d\n", rc);
    }
}

void on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg)
{
//	printf("Received message: %s on topic %s\n", (char *)msg->payload, msg->topic);
	mq_recv_message( msg->topic, (char *)msg->payload );
}

void on_disconnect(struct mosquitto *mosq, void *obj, int rc)
{
	printf("Disconnected from mqtt broker\n");
}



static gboolean
mosquitto_fd_dispatch(GIOChannel* source,
		      GIOCondition condition,
		      gpointer user_data)
{
	struct mosquitto *mosq = user_data;
	int new_mosq_fd;

	int ret = mosquitto_loop_read(mosq, 1);
	if (ret == MOSQ_ERR_CONN_LOST) {
		/* We've been disconnected from the server */
		printf("Reconnect...\n");
		mosquitto_reconnect(mosq);
	}
	if (mosquitto_want_write(mosq)) {
		mosquitto_loop_write(mosq, 8);
	}
	mosquitto_loop_misc(mosq);
	new_mosq_fd = mosquitto_socket(mosq);
	if(new_mosq_fd != mosq_fd) {
		printf("mosq_fd was %d now %d\n", mosq_fd, new_mosq_fd);
		// TODO what to do to tell GMainLoop
	}
}

static gboolean
mosquitto_misc_dispatch(gpointer user_data)
{
	struct mosquitto *mosq = user_data;
//	printf("mosq_misc_timeout\n");
	mosquitto_loop_misc(mosq);
	return TRUE;
}

// Initialize the MQTT client
int
mqtt_setup( GMainLoop *loop)
{
	int rc;

	// Initialize the Mosquitto library
	mosquitto_lib_init();

	// Create a new Mosquitto client instance
	mosq = mosquitto_new(NULL, true, NULL);
	if (!mosq) {
		fprintf(stderr, "Failed to create mosquitto client\n");
		return 1;
	}
	
	// Set callbacks
	mosquitto_connect_callback_set(mosq, on_connect);
	mosquitto_message_callback_set(mosq, on_message);
	mosquitto_disconnect_callback_set(mosq, on_disconnect);
	printf("got mosq client fd=%d; trying to connect:\n", mosquitto_socket(mosq));
	
	// Connect to the MQTT broker
	if (mosquitto_connect(mosq, HOST, 1883, 60) != MOSQ_ERR_SUCCESS) {
		fprintf(stderr, "Failed to connect to broker\n");
		return 1;
	}

	// Add Mosquitto to GMainLoop
	mosq_fd = mosquitto_socket(mosq);
	printf("connected; adding moscq_fd=%d to gio\n", mosq_fd);
	mosq_gio_chan = g_io_channel_unix_new(mosq_fd);
	g_io_add_watch(mosq_gio_chan, G_IO_IN | G_IO_HUP | G_IO_ERR,
		       mosquitto_fd_dispatch, 
		       (gpointer)mosq);

	// TODO set up periodic timer that will call mosquitto_loop_misc
	mosq_misc_timer_tag = g_timeout_add(500, mosquitto_misc_dispatch, mosq);
	return 1;
}

void
mqtt_shutdown()
{
	if(mosq_misc_timer_tag != 0)
		g_source_remove(mosq_misc_timer_tag);
	mosq_misc_timer_tag = 0;
	mosquitto_disconnect(mosq);
	mosquitto_destroy(mosq);
	mosquitto_lib_cleanup();
}

void
mqtt_publish(char *topic, char *msg)
{
	mosquitto_publish(mosq, NULL, topic, strlen(msg), msg, 0, 0);
}
