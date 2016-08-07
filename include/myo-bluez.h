#ifndef MYO_BLUEZ_H
#define MYO_BLUEZ_H 

#include <stdbool.h>
#include <gio/gio.h>

#define side2str(SIDE) \
	(SIDE == NONE ? "None" : (SIDE == RIGHT ? "Right" : "Left"))
#define dir2str(DIR) \
	(DIR == UNKNOWN ? "Unknown" : (DIR == X_TO_WRIST ? "Wrist" : "Elbow"))

typedef enum {
	DISCONNECTED,
	SEARCHING,
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

typedef struct {
	const char *UUID;
	GDBusProxy *proxy;
	const char **char_UUIDs;
	GDBusProxy **char_proxies;
	int num_chars;
} GattService;

#define NUM_SERVICES 5

typedef struct {
	GDBusProxy *proxy;
	const char *address;

	GattService services[NUM_SERVICES];

	gulong imu_sig_id;
	gulong arm_sig_id;
	gulong emg_sig_id;

	MyoStatus status;
} Myo;

int myo_get_name(Myo *myo, char *str);
void myo_get_version(Myo *myo, char *ver);
void myo_EMG_notify_enable(Myo *myo, bool enable);
void myo_IMU_notify_enable(Myo *myo, bool enable);
void myo_arm_indicate_enable(Myo *myo, bool enable);
void myo_update_enable(Myo *myo, bool emg, bool imu, bool arm);

char* pose2str(libmyo_pose_t pose) {
	switch(pose) {
		case libmyo_pose_rest:
			return "Rest";
		case libmyo_pose_fist:
			return "Fist";
		case libmyo_pose_wave_in:
			return "Wave in";
		case libmyo_pose_wave_out:
			return "Wave out";
		case libmyo_pose_fingers_spread:
			return "Spread";
		case libmyo_pose_double_tap:
			return "Double Tap";
		default:
			return "Unknown";
	}
}

#endif
