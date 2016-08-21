#ifndef MYO_BLUEZ_H
#define MYO_BLUEZ_H 

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include <gio/gio.h>

#include "myo-bluetooth/myohw.h"

#ifdef DEBUG
#define debug(M, ...) fprintf(stderr, "DEBUG %s:%d: " M "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#else
#define debug(M, ...)
#endif

#define side2str(SIDE) \
	(SIDE == myohw_arm_right ? "Right" : (SIDE == myohw_arm_left ? "Left" : "Unknown"))
#define dir2str(DIR) \
	(DIR == myohw_x_direction_toward_wrist ? "Wrist" : (DIR == myohw_x_direction_toward_elbow ? "Elbow" : "Unknown"))

#define MYOBLUEZ_ERROR 1
#define MYOBLUEZ_OK 0

typedef void* myobluez_myo_t;

typedef enum {
	DISCONNECTED,
	CONNECTING,
	CONNECTED
} MyoStatus;

int myo_get_name(myobluez_myo_t *myo, char *str);
void myo_get_version(myobluez_myo_t *myo, myohw_fw_version_t *ver);
void myo_EMG_notify_enable(myobluez_myo_t *myo, bool enable);
void myo_IMU_notify_enable(myobluez_myo_t *myo, bool enable);
void myo_arm_indicate_enable(myobluez_myo_t *myo, bool enable);
void myo_update_enable(
		myobluez_myo_t *myo,
		myohw_emg_mode_t emg,
		myohw_imu_mode_t imu,
		myohw_classifier_mode_t arm);
char* pose2str(myohw_pose_t pose);

int myobluez_init();
void myobluez_deinit();

#endif
