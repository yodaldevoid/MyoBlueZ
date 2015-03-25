#ifndef MYO-BLUEZ_H_
#define MYO-BLUEZ_H_

#include <stdbool.h>

#include <glib.h>
#include <gio/gio.h>

enum MyoStatus {
    DISCONNECTED,
    CONNECTED
};

enum ArmDataType {
    WORN = 1,
    REMOVED,
    POSE,
};

enum ArmSide {
    NONE,
    RIGHT,
    LEFT,
};

enum ArmXDirection {
    UNKNOWN,
    X_TO_WRIST,
    X_TO_ELBOW,
};

int myo_get_name(char *str);
void myo_get_version(char *ver);
void myo_EMG_notify_enable(bool enable);
void myo_IMU_notify_enable(bool enable);
void myo_arm_indicate_enable(bool enable);
void myo_update_enable(bool emg, bool imu, bool arm);

#endif
