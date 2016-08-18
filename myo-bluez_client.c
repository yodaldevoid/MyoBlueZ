#include <errno.h>

#include <glib.h>

#include "myo-bluez.h"

static GMainLoop *loop;

void client_stop(int sig) {
	myobluez_deinit();

	if(loop != NULL) {
		if(g_main_loop_is_running(loop)) {
			debug("Quiting Main Loop");
			g_main_loop_quit(loop);
		}

		g_main_loop_unref(loop);
		loop = NULL;
	}
}

int main(void) {
	signal(SIGINT, client_stop);

	myobluez_init();

	loop = g_main_loop_new(NULL, false);

	debug("Running Main Loop");
	g_main_loop_run(loop);

	return 0;
}