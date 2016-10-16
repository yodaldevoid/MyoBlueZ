#include <errno.h>

#include <glib.h>

#include "myo-bluez.h"

static GMainLoop *loop;

void on_imu(myohw_imu_data_t data) {
	printf(
			"On_IMU:\n"
			"Quat - X: %d | Y: %d | Z: %d | W: %d\n"
			"Acc - X: %d | Y: %d | Z: %d\n"
			"Gyro - X: %d | Y: %d | Z: %d\n"
			"--------------------------------------\n",
			data.orientation.x, data.orientation.y, data.orientation.z, data.orientation.w,
			data.accelerometer[0], data.accelerometer[1], data.accelerometer[2],
			data.gyroscope[0], data.gyroscope[1], data.gyroscope[2]
	);
}

void on_arm(myohw_classifier_event_t event) {
	switch(event.type) {
		case myohw_classifier_event_arm_synced:
			printf(
					"On_Arm:\n"
					"Side: %s | XDirection: %s\n"
					"--------------------------------------\n",
					side2str(event.arm), dir2str(event.x_direction)
			);
			break;
		case myohw_classifier_event_arm_unsynced:
			debug("Unsynced");
			break;
		case myohw_classifier_event_pose:
			printf(
					"On_Pose:\n"
					"Pose: %s\n"
					"--------------------------------------\n",
					pose2str(event.pose)
			);
			break;
		case myohw_classifier_event_unlocked:
			debug("Unlocked");
			break;
		case myohw_classifier_event_locked:
			debug("Locked");
			break;
		case myohw_classifier_event_sync_failed:
			debug("Sync failed");
			break;
		default:
			debug("Unknown event type %d", event.type);
	}
}

void on_emg(int16_t *emg, uint8_t moving) {
	printf(
			"On_EMG:\n"
			"EMG 1: %u | EMG 2: %u\n"
			"EMG 3: %u | EMG 4: %u\n"
			"EMG 5: %u | EMG 6: %u\n"
			"EMG 7: %u | EMG 8: %u\n"
			"Moving: %u\n"
			"--------------------------------------\n",
			emg[0], emg[1], emg[2], emg[3],
			emg[4], emg[5], emg[6], emg[7],
			moving
	);
}

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

	myo_imu_cb_register(myo, on_imu);
	myo_arm_cb_register(myo, on_arm);
	myo_emg_cb_register(myo, on_emg);

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