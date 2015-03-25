#ifndef MYO_BLUEZ_H
#define MYO_BLUEZ_H 

#include <stdbool.h>

typedef enum {
    DISCONNECTED,
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

int myo_get_name(char *str);
void myo_get_version(char *ver);
void myo_EMG_notify_enable(bool enable);
void myo_IMU_notify_enable(bool enable);
void myo_arm_indicate_enable(bool enable);
void myo_update_enable(bool emg, bool imu, bool arm);

#endif
