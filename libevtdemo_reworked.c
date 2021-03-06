#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

//#include <event.h>
#include <event2/event.h>
#include <json.h>

#include "pubnub.h"
#include "pubnub-libevent.h"

#define DEBUG 1

/* We must ensure that only one method call is in progress at once within a
 * single context, this is the libpubnub requirement. There are many tricky
 * issues that might also commonly show up in a variety of multi-threading
 * scenarios.
 *
 * For example, what to do if we want to regularly publish messages but are hit
 * with a stuck message - shall we maintain a queue of messages to publish,
 * create a new context for publishing the new message in parallel, or just
 * swallow the PNR_OCCUPIED error and drop the message? All three answers are
 * right, it just depends on your scenario (is ordering or latency more
 * important? is history important?). */

/* We will concern ourselves with these strategies in other examples. Here, we
 * will demonstrate just a simple sequential usage, our demo will just first
 * publish a single message, then retrieve history of last N messages, then
 * enter a subscription "loop". The calls will be stringed in sequential order
 * by callbacks.
 *
 * To showcase that this is all asynchronous, independent of the above a clock
 * will be shown at the last line of output, updated every second. */



const char *channels[] = { "my_channel", "demo_channel" };
const int nbr_channels = 2;

static void
clock_update(int fd, short kind, void *userp)
{
	
	/* Print current time. */
	time_t t = time(NULL);
	int now_s = t % 60;
	int now_m = (t / 60) % 60;
	int now_h = (t / 3600) % 24;
	/* The trailing \r will make cursor return to the beginning
	 * of the current line. */
	printf("%02d:%02d:%02d\r", now_h, now_m, now_s);
	fflush(stdout);

	/* Next clock update in one second. */
	/* (this is now handled by EV_PERSIST) */
}

/* Event based publish - added by Otisman.
 * This demonstrates using another event to invoke publishing at intervals to the channel.
 * Not part of the original example code. */

static void publish_event_function(int fd, short event, void *arg);
static void unsubscribe_done(struct pubnub *p, enum pubnub_res result, struct json_object *response, void *ctx_data, void *call_data);
static void publish_event_done(struct pubnub *p, enum pubnub_res result, struct json_object *response, void *ctx_data, void *call_data);


/* The callback chain.
 *
 * Below, we have many separate functions, but the control flow
 * is mostly linear, so just continue reading in next function
 * when you finish the previous one. The code is split to functions
 * (i) when issuing a call that must be handled asynchronously, and
 * (ii) for clarity. */




static void publish(struct pubnub *p);
static void publish_done(struct pubnub *p, enum pubnub_res result, struct json_object *response, void *ctx_data, void *call_data);

static void history(struct pubnub *p);
static void history_received(struct pubnub *p, enum pubnub_res result, struct json_object *msg, void *ctx_data, void *call_data);

static void subscribe(struct pubnub *p);
static void subscribe_received(struct pubnub *p, enum pubnub_res result, char **channels, struct json_object *msg, void *ctx_data, void *call_data);

/* Added by Otisman  -  the following three functions are not part of the original example concept*/
static void publish_event_function(int fd, short event, void *arg)
{
	struct pubnub **p = arg;
	printf("Event Based Publish\n");
	/* This needs to cancel the current method (as the subscribe loop is running) */
	/* Need to ensure subscribe loop does not re-subscribe before the publish has finished, and
	 * this is achieved by the subscribe callback being issued PNR_CANCELLED when we unsubscribe from all channels.
	 * The subscribe loop is terminated - and then re-commenced after the publish is complete*/
	
	pubnub_unsubscribe(*p, channels, nbr_channels, -1, unsubscribe_done, NULL);
}

static void unsubscribe_done(struct pubnub *p, enum pubnub_res result, struct json_object *response, void *ctx_data, void *call_data)	
{	

	if (result != PNR_OK) {
		/* An unrecoverable error, we just terminate with an
		 * error code. Since pubnub_error_policy()'s print is
		 * true by default, an explanation has already been
		 * written to stderr and we tried to retry as well. */
		exit(EXIT_FAILURE);
	}
	printf("Unsubscribe ok\n");
	json_object *msg = json_object_new_object();
	json_object_object_add(msg, "num", json_object_new_int(37));
	json_object_object_add(msg, "str", json_object_new_string("\"What a world, what a world!\" she said."));

	pubnub_publish(p, "my_channel", msg, -1, publish_event_done, NULL);
	json_object_put(msg);
	
	
}

static void
publish_event_done(struct pubnub *p, enum pubnub_res result, struct json_object *response, void *ctx_data, void *call_data)
{
	/* ctx_data is (struct pubnub_libevent *) */
	/* call_data is NULL as that's what we passed to pubnub_publish() */
	if (result != PNR_OK)
		/* An unrecoverable error, we just terminate with an
		 * error code. Since pubnub_error_policy()'s print is
		 * true by default, an explanation has already been
		 * written to stderr and we tried to retry as well. */
		exit(EXIT_FAILURE);

	printf("Event Based Publish ok\n");

	/* Next step in the sequence is to re-subscribe. */

	subscribe(p); //re-subscribe
}
/*end of code added by Otisman */


static void
publish(struct pubnub *p)
{
	json_object *msg = json_object_new_object();
	json_object_object_add(msg, "num", json_object_new_int(42));
	json_object_object_add(msg, "str", json_object_new_string("Hello, world!"));

	pubnub_publish(p, "my_channel", msg, -1, publish_done, NULL);
	printf("\nPublished Message...\n");
	json_object_put(msg);

	/* ...continues later in publish_done(). */
}

static void
publish_done(struct pubnub *p, enum pubnub_res result, struct json_object *response, void *ctx_data, void *call_data)
{
	/* ctx_data is (struct pubnub_libevent *) */
	/* call_data is NULL as that's what we passed to pubnub_publish() */
	printf("Publish Callback\n");
	if (result != PNR_OK)
		/* An unrecoverable error, we just terminate with an
		 * error code. Since pubnub_error_policy()'s print is
		 * true by default, an explanation has already been
		 * written to stderr and we tried to retry as well. */
		exit(EXIT_FAILURE);

	printf("pubnub publish ok\n");

	/* Next step in the sequence is retrieving history. */

	history(p);
}


static void
history(struct pubnub *p)
{
	pubnub_history(p, "my_channel", 10, -1, history_received, NULL);

	/* ...continues later in history_received(). */
}

static void
history_received(struct pubnub *p, enum pubnub_res result, struct json_object *msg, void *ctx_data, void *call_data)
{
	/* ctx_data is (struct pubnub_libevent *) */
	/* call_data is NULL as that's what we passed to pubnub_history() */

	if (result != PNR_OK)
		exit(EXIT_FAILURE);

	printf("pubnub history ok: %s\n", json_object_get_string(msg));


	/* Next step in the sequence is entering the subscribe "loop". */

	subscribe(p);
}


/* How does channel subscription work? The subscribe() call will issue
 * a PubNub subscribe request and call subscribe_received() when some
 * messages arrived. subscribe_received() will process the messages,
 * then "loop" by calling subscribe() again to issue a new request. */

static void
subscribe(struct pubnub *p)
{
	//const char *channels[] = { "my_channel", "demo_channel" };  //moved to global for use in other functions
	pubnub_subscribe_multi(p, channels, nbr_channels, -1, subscribe_received, NULL); //added nbr_channels global const

	/* ...continues later in subscribe_received(). */
}

static void
subscribe_received(struct pubnub *p, enum pubnub_res result, char **channels, struct json_object *msg, void *ctx_data, void *call_data)
{
	/* ctx_data is (struct pubnub_libevent *) */
	/* call_data is NULL as that's what we passed to pubnub_subscribe_multi() */

	if (result != PNR_OK) {
		if (result == PNR_CANCELLED) return;  //this occurs when channels are unsubscribed - not a fatal error, just exit subscribe loop.
		/* This must be something fatal, we retry on recoverable
		 * errors. */
		exit(EXIT_FAILURE);
	}
	if (json_object_array_length(msg) == 0) {
		printf("pubnub subscribe ok, no news\n");
	} else {
		for (int i = 0; i < json_object_array_length(msg); i++) {
			json_object *msg1 = json_object_array_get_idx(msg, i);
			printf("pubnub subscribe [%s]: %s\n", channels[i], json_object_get_string(msg1));
			free(channels[i]);
		}
	}
	free(channels);

	/* Loop. */
	subscribe(p);
}


int
main(void)
{
	/* Set up the libevent library. */
	
	struct event_base *evbase = event_base_new();
	
	/* Set up the PubNub library, with a single shared context,
	 * using the libevent backend for event handling. */
	struct pubnub_libevent *libevent = pubnub_libevent_init(evbase);
	struct pubnub *p = pubnub_init("demo", "demo", &pubnub_libevent_callbacks, libevent);

	/* Set up the events, and callbacks etc. */
	struct event *clock_update_timer = event_new(evbase, 0, EV_PERSIST, clock_update, NULL);
    struct event *publishevent = event_new(evbase, 0, EV_PERSIST, publish_event_function, &p);  //added by otisman
	
	/* Set up the timers for the events */
	
	struct timeval publishtimer = { .tv_sec = 5, .tv_usec = 0 }; //added by otisman
	struct timeval timeout = { .tv_sec = 1, .tv_usec = 0 };

	/* Add the events */
	event_add(clock_update_timer, &timeout);
	event_add(publishevent, &publishtimer); //added by otisman

	/* First step in the PubNub call sequence is publishing a message. */
	/* This sets off the chain of functions above that ultimately leads to a subscribe loop */
	publish(p);

	/* Here, we could start any other asynchronous operations as needed,
	 * launch a GUI or whatever. */
	 
	event_base_dispatch(evbase);
	/* We should never reach here. */
	printf("Oops.. we got here!\n");
	pubnub_done(p);
	event_base_free(evbase);
	return EXIT_SUCCESS;
}


