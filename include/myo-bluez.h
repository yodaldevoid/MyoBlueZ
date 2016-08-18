#ifndef MYO_BLUEZ_H
#define MYO_BLUEZ_H 

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include <gio/gio.h>

#ifdef DEBUG
#define debug(M, ...) fprintf(stderr, "DEBUG %s:%d: " M "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#else
#define debug(M, ...)
#endif

#define side2str(SIDE) \
	(SIDE == NONE ? "None" : (SIDE == RIGHT ? "Right" : "Left"))
#define dir2str(DIR) \
	(DIR == UNKNOWN ? "Unknown" : (DIR == X_TO_WRIST ? "Wrist" : "Elbow"))

#define MYOBLUEZ_ERROR 1
#define MYOBLUEZ_OK 0

typedef void* myobluez_myo_t;

typedef enum {
	DISCONNECTED,
	CONNECTING,
	CONNECTED
} MyoStatus;

typedef enum {
	WORN = 1,
	REMOVED,
	POSE,
} ArmDataType;

typedef enum {
	NONE,
	RIGHT,
	LEFT,
} ArmSide;

typedef enum {
	UNKNOWN,
	X_TO_WRIST,
	X_TO_ELBOW,
} ArmXDirection;

typedef enum {
	libmyo_pose_rest           = 0, //Rest pose.
	libmyo_pose_fist           = 1, //User is making a fist.
	libmyo_pose_wave_in        = 2, //User has an open palm rotated towards the posterior of their wrist.
	libmyo_pose_wave_out       = 3, //User has an open palm rotated towards the anterior of their wrist.
	libmyo_pose_fingers_spread = 4, //User has an open palm with their fingers spread away from each other.
	libmyo_pose_double_tap     = 5, //User tapped their thumb and middle finger together twice in succession.

	libmyo_num_poses,               //Number of poses supported; not a valid pose.

	libmyo_pose_unknown = 0xffff    //Unknown pose.
} libmyo_pose_t;

int myo_get_name(myobluez_myo_t *myo, char *str);
void myo_get_version(myobluez_myo_t *myo, unsigned char *ver);
void myo_EMG_notify_enable(myobluez_myo_t *myo, bool enable);
void myo_IMU_notify_enable(myobluez_myo_t *myo, bool enable);
void myo_arm_indicate_enable(myobluez_myo_t *myo, bool enable);
void myo_update_enable(myobluez_myo_t *myo, bool emg, bool imu, bool arm);
char* pose2str(libmyo_pose_t pose);

int myobluez_init();
void myobluez_deinit();

#endif
