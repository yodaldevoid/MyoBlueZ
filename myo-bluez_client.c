#include <errno.h>

#include <glib.h>

#include "myo-bluez.h"

static GMainLoop *loop;

int myo_initialize(myobluez_myo_t myo) {
	myohw_fw_version_t version;
	myohw_fw_info_t info;
	char name[25];

	printf("Initializing...\n");

	//read firmware version
	myo_get_version(myo, &version);
	printf("firmware version: %d.%d.%d.%d\n",
			version.major, version.minor, version.patch, version.hardware_rev);

	myo_get_info(myo, &info);

	myo_get_name(myo, name);
	printf("device name: %s\n", name);

	//enable IMU data
	myo_IMU_notify_enable(myo, true);
	//enable on/off arm notifications
	myo_arm_indicate_enable(myo, true);

	myo_update_enable(
			myo,
			myohw_emg_mode_none,
			myohw_imu_mode_send_events,
			myohw_classifier_mode_enabled
	);

	printf("Initialized!\n");

	return MYOBLUEZ_OK;
}

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

	loop = g_main_loop_new(NULL, false);

	myobluez_init(myo_initialize);

	debug("Running Main Loop");
	g_main_loop_run(loop);

	return 0;
}