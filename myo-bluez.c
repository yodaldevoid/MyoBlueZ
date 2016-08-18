#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <bluetooth/bluetooth.h>

#include <glib.h>

#include "myo-bluez.h"
#include "myo-bluetooth/myohw.h"

#define ASSERT(GERR, MSG) \
	if(GERR != NULL) { \
		debug("%s; %s", MSG, GERR->message); \
		g_clear_error(&GERR); \
	}

#define DEVICE_IFACE "org.bluez.Device1"
#define GATT_SERVICE_IFACE "org.bluez.GattService1"
#define GATT_CHARACTERISTIC_IFACE "org.bluez.GattCharacteristic1"

static const char *BATT_UUID = "0000180f-0000-1000-8000-00805f9b34fb";
static const char *BATT_CHAR_UUIDS[] = {"00002a19-0000-1000-8000-00805f9b34fb"};

static const char *MYO_UUID = "d5060001-a904-deb9-4748-2c7f4a124842";
static const char *MYO_CHAR_UUIDS[] = {
		"d5060101-a904-deb9-4748-2c7f4a124842",
		"d5060201-a904-deb9-4748-2c7f4a124842",
		"d5060401-a904-deb9-4748-2c7f4a124842"
};

static const char *IMU_UUID = "d5060002-a904-deb9-4748-2c7f4a124842";
static const char *IMU_CHAR_UUIDS[] = {
		"d5060402-a904-deb9-4748-2c7f4a124842",
		"d5060502-a904-deb9-4748-2c7f4a124842"
};

static const char *ARM_UUID = "d5060003-a904-deb9-4748-2c7f4a124842";
static const char *ARM_CHAR_UUIDS[] = {"d5060103-a904-deb9-4748-2c7f4a124842"};

static const char *EMG_UUID = "d5060004-a904-deb9-4748-2c7f4a124842";
static const char *EMG_CHAR_UUIDS[] = {"d5060104-a904-deb9-4748-2c7f4a124842"};

static GError *error;
static GMainLoop *loop;

static gulong cb_id;

static GDBusObjectManagerClient *bluez_manager;

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

//TODO: add unknown services
#define battery_service services[0]
#define myo_control_service services[1]
#define imu_service services[2]
#define arm_service services[3]
#define emg_service services[4]

#define version_data myo_control_service.char_proxies[1]
#define cmd_input myo_control_service.char_proxies[2]
#define imu_data imu_service.char_proxies[0]
#define arm_data arm_service.char_proxies[0]
#define emg_data emg_service.char_proxies[0]

#define MAX_MYOS 4
static Myo myos[MAX_MYOS];
static int num_myos;

static void set_myo(const gchar *path);

static void init_GattService(GattService *service, const char *UUID, const char **char_UUIDs, int num_chars) {
	service->UUID = UUID;
	service->char_UUIDs = char_UUIDs;
	service->num_chars = num_chars;
	service->char_proxies = calloc(num_chars, sizeof(GDBusProxy*));
}

static gint is_device(gconstpointer a, gconstpointer b) {
	GDBusInterface *interface;

	interface = g_dbus_object_get_interface((GDBusObject*) a, DEVICE_IFACE);
	if(NULL == interface) {
		return 1;
	} else {
		g_object_unref(interface);
		return 0;
	}
}

static gint is_service(gconstpointer a, gconstpointer b) {
	GDBusInterface *interface;

	interface = g_dbus_object_get_interface((GDBusObject*) a,
			GATT_SERVICE_IFACE);
	if(NULL == interface) {
		return 1;
	} else {
		g_object_unref(interface);
		return 0;
	}
}

static gint is_characteristic(gconstpointer a, gconstpointer b) {
	GDBusInterface *interface;

	interface = g_dbus_object_get_interface((GDBusObject*) a,
			GATT_CHARACTERISTIC_IFACE);
	if(NULL == interface) {
		return 1;
	} else {
		g_object_unref(interface);
		return 0;
	}
}

static Myo* get_myo_from_proxy(GDBusProxy *proxy) {
	int i;

	for(i = 0; i < num_myos; i++) {
		if(myos[i].proxy == proxy) {
			debug("Myo Index: %d", i);
			return &myos[i];
		}
	}

	return NULL;
}

static void set_characteristic(GattService *serv, const gchar *char_path) {
	int i;
	GDBusProxy *proxy;
	GVariant *UUID;
	const char *UUID_str;

	debug("Setting Characteristic at %s", char_path);

	proxy = g_dbus_proxy_new_for_bus_sync(
			G_BUS_TYPE_SYSTEM,
			G_DBUS_PROXY_FLAGS_NONE, NULL, "org.bluez", char_path,
			GATT_CHARACTERISTIC_IFACE, NULL, &error);
	ASSERT(error, "Get characteristic proxy failed");
	if(proxy == NULL) {
		fprintf(stderr, "Get characteristic proxy failed\n");
	}

	UUID = g_dbus_proxy_get_cached_property(proxy, "UUID");
	if(UUID == NULL) {
		//This shouldn't happen
		debug("Characteristic UUID not set");
		return;
	}
	UUID_str = g_variant_get_string(UUID, NULL);

	for(i = 0; i < serv->num_chars; i++) {
		if(strcmp(serv->char_UUIDs[i], UUID_str) == 0) {
			debug("Characteristic set");
			serv->char_proxies[i] = proxy;

			g_variant_unref(UUID);
			return;
		}
	}

	debug("Characteristic was not set!");
	g_variant_unref(UUID);
	g_object_unref(proxy);
	return;
}

static void set_service(Myo *myo, const gchar *serv_path) {
	int i;
	GDBusProxy *proxy, *chara;
	GVariant *UUID, *serv;
	const char *UUID_str;

	GList *objects, *object;
	const gchar *char_path, *char_serv_path;

	debug("Setting Service at %s", serv_path);

	proxy = g_dbus_proxy_new_for_bus_sync(
			G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE, NULL, "org.bluez",
			serv_path, GATT_SERVICE_IFACE, NULL, &error);
	ASSERT(error, "Get service proxy failed");
	if(proxy == NULL) {
		return;
	}

	UUID = g_dbus_proxy_get_cached_property(proxy, "UUID");
	if(UUID == NULL) {
		//This shouldn't happen
		debug("Service UUID not set");
		return;
	}
	UUID_str = g_variant_get_string(UUID, NULL);

	for(i = 0; i < NUM_SERVICES; i++) {
		if(strcmp(UUID_str, myo->services[i].UUID) == 0) {
			debug("Service set");
			myo->services[i].proxy = proxy;

			g_variant_unref(UUID);

			//search for chars
			objects = g_dbus_object_manager_get_objects(
					(GDBusObjectManager*) bluez_manager);
			if(NULL == objects) {
				debug("Manager did not give us objects!");
				return;
			}

			debug("Searching objects for characteristics, expecting %d", myo->services[i].num_chars);
			do {
				object = NULL;
				object = g_list_find_custom(objects, NULL, is_characteristic);
				if(object != NULL) {
					char_path = g_dbus_object_get_object_path((GDBusObject*) object->data);
					chara = g_dbus_proxy_new_for_bus_sync(
							G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE, NULL, "org.bluez",
							char_path, GATT_CHARACTERISTIC_IFACE, NULL, &error);
					ASSERT(error, "Get characteristic proxy failed");
					if(chara == NULL) {
						objects = g_list_delete_link(objects, object);
						continue;
					}

					serv = g_dbus_proxy_get_cached_property(chara, "Service");
					if(serv != NULL) {
						char_serv_path = g_variant_get_string(serv, NULL);
					} else {
						debug("Characteristic Service not set");
						g_variant_unref(serv);
						g_list_free_full(objects, g_object_unref);
						return;
					}

					if(strcmp(serv_path, char_serv_path) == 0) {
						set_characteristic(&myo->services[i], char_path);
					}
					g_variant_unref(serv);
					objects = g_list_delete_link(objects, object);
				}
			} while(object != NULL);

			g_list_free_full(objects, g_object_unref);

			return;
		}
	}

	debug("Service was not set!");
	g_variant_unref(UUID);
	g_object_unref(proxy);
	return;
}

static void set_services(Myo *myo) {
	GList *objects, *object;
	GVariant *device;
	GDBusProxy *serv;
	const gchar *myo_path, *serv_path, *serv_dev_path;

	myo_path = g_dbus_proxy_get_object_path(myo->proxy);

	//search for services that are already registered
	objects = g_dbus_object_manager_get_objects(
			(GDBusObjectManager*) bluez_manager);
	if(NULL == objects) {
		//failed
		fprintf(stderr, "Manager did not give us objects!\n");
		return;
	}

	debug("Searching objects for services");
	do {
		object = NULL;
		object = g_list_find_custom(objects, NULL, is_service);
		if(object != NULL) {
			serv_path = g_dbus_object_get_object_path((GDBusObject*) object->data);
			serv = g_dbus_proxy_new_for_bus_sync(
					G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE, NULL, "org.bluez",
					serv_path, GATT_SERVICE_IFACE, NULL, &error);
			ASSERT(error, "Get service proxy failed");
			if(serv == NULL) {
				objects = g_list_delete_link(objects, object);
				continue;
			}

			device = g_dbus_proxy_get_cached_property(serv, "Device");
			if(device != NULL) {
				serv_dev_path = g_variant_get_string(device, NULL);
			} else {
				debug("Service Device not set");
				g_variant_unref(device);
				g_list_free_full(objects, g_object_unref);
				return;
			}

			if(strcmp(myo_path, serv_dev_path) == 0) {
				set_service(myo, serv_path);
			}
			g_variant_unref(device);
			objects = g_list_delete_link(objects, object);
		}
	} while(object != NULL);

	g_list_free_full(objects, g_object_unref);
}

static void myo_signal_cb(GDBusProxy *proxy, GVariant *changed, GStrv invalid, gpointer user_data) {
	GVariantIter *iter;
	const gchar *key;
	GVariant *value, *reply;

	Myo *myo = NULL;

	if(g_variant_n_children(changed) > 0) {
		g_variant_get(changed, "a{sv}", &iter);
		while(g_variant_iter_loop(iter, "{&sv}", &key, &value)) {
			if(strcmp(key, "Connected") == 0) {
				//check for disconnect
				if(!g_variant_get_boolean(value)) {
					myo = get_myo_from_proxy(proxy);

					if(myo == NULL || !G_IS_DBUS_PROXY(myo->proxy)) {
						//couldn't find myo in registered myos
						//this REALLY shouldn't ever happen
						return;
					}

					//disconnected
					printf("Myo disconnected\n");
					reply = g_dbus_proxy_call_sync(
							proxy, "Disconnect", NULL, G_DBUS_CALL_FLAGS_NONE,
							-1, NULL, &error);
					ASSERT(error, "Disconnect failed");
					g_variant_unref(reply);
					myo->status = DISCONNECTED;

					printf("Connecting...\n");
					do {
						reply = g_dbus_proxy_call_sync(
								proxy, "Pair", NULL, G_DBUS_CALL_FLAGS_NONE,
								-1, NULL, &error);
						ASSERT(error, "Connection failed");
					} while(reply == NULL);
					g_variant_unref(reply);

					myo->status = CONNECTED;
					printf("Connected!\n");
				}
			}  else if(strcmp(key, "ServicesResolved") == 0) {
				if(g_variant_get_boolean(value)) {
					myo = get_myo_from_proxy(proxy);
					if(myo == NULL) {
						//couldn't find myo in registered myos
						//this REALLY shouldn't ever happen
						return;
					}
					debug("ServicesResolved");
					set_services(myo);
				}
			}
		}
		g_variant_iter_free(iter);
	}
}

static void device_UUID_cb(GDBusProxy *proxy, GVariant *changed, GStrv invalid, gpointer user_data) {
	GVariantIter *iter;
	const gchar *key;
	GVariant *value;

	//check if UUIDs was set
	if(g_variant_n_children(changed) > 0) {
		g_variant_get(changed, "a{sv}", &iter);
		while(g_variant_iter_loop(iter, "{&sv}", &key, &value)) {
			if(strcmp(key, "UUIDs") == 0) {
				//if UUIDs set, kill notifier and call set_myo
				//TODO: might cause problems if not all UUIDs are set at once
				g_signal_handler_disconnect(proxy, *((gulong*) user_data));
				free(user_data);
				set_myo(g_dbus_proxy_get_object_path(proxy));
				break;
			}
		}
		g_variant_iter_free(iter);
	}
}

static void set_myo(const gchar *path) {
	gulong *sig_id;

	GDBusProxy *proxy;
	GVariant *UUIDs, *reply, *serv_res;
	GVariantIter *iter;
	gchar *uuid;

	Myo *myo;

	if(num_myos == MAX_MYOS) {
		fprintf(stderr, "Maximum myos already registered\n");
		return;
	}

	//get path from proxy
	proxy = (GDBusProxy*) g_dbus_object_manager_get_interface(
			(GDBusObjectManager*) bluez_manager, path, DEVICE_IFACE);
		fprintf(stderr, "Get device proxy failed\n");
	if(!G_IS_DBUS_PROXY(proxy)) {
		return;
	}
	//get UUIDs
	//if cannot get UUIDs set notify
	UUIDs = g_dbus_proxy_get_cached_property(proxy, "UUIDs");
	if(UUIDs == NULL || (g_variant_n_children(UUIDs) == 0)) {
		debug("Device UUIDs not set");
		//set notifier
		if(UUIDs != NULL) {
			g_variant_unref(UUIDs);
		}
		sig_id = (gulong*) malloc(sizeof(gulong));
		*sig_id = g_signal_connect(proxy, "g-properties-changed",
									G_CALLBACK(device_UUID_cb), sig_id);
		g_object_unref(proxy);
		return;
	}

	myo = NULL;
	g_variant_get(UUIDs, "as", &iter);
	while(g_variant_iter_loop(iter, "&s", &uuid)) {
		if(strcmp(uuid, MYO_UUID) == 0) {
			debug("Myo Index:%d", num_myos);
			myo = &myos[num_myos++];
			myo->proxy = proxy;
			break;
		}
	}
	g_variant_iter_free(iter);
	g_variant_unref(UUIDs);

	if(myo == NULL) {
		g_object_unref(proxy);
		return;
	}

	printf("Myo found!\n");

	g_signal_connect(myo->proxy, "g-properties-changed", G_CALLBACK(myo_signal_cb), NULL);

	printf("Connecting...\n");
	do {
		reply = NULL;
		reply = g_dbus_proxy_call_sync(
				myos->proxy, "Connect", NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
		ASSERT(error, "Connection failed");
	} while(reply == NULL);
	g_variant_unref(reply);

	myos->status = CONNECTED;
	printf("Connected!\n");

	//check ServicesResolved
	serv_res = g_dbus_proxy_get_cached_property(myo->proxy, "ServicesResolved");
	if(serv_res != NULL && g_variant_get_boolean(serv_res) && myo->services[0].proxy == NULL) {
		g_variant_unref(serv_res);
		debug("ServicesResolved");
		set_services(myo);
	}

	return;
}

static void object_added_cb(GDBusObjectManager *manager, GDBusObject *object, gpointer user_data) {
	if(is_device(object, NULL) == 0) {
		debug("object_added_devce");
		set_myo(g_dbus_object_get_object_path(object));
	} 
}

static void on_imu(short *quat, short *acc, short *gyro) {
	printf(
			"On_IMU:\n"
			"Quat - X: %d | Y: %d | Z: %d | W: %d\n"
			"Acc - X: %d | Y: %d | Z: %d\n"
			"Gyro - X: %d | Y: %d | Z: %d\n"
			"--------------------------------------\n",
			quat[0], quat[1], quat[2], quat[3],
			acc[0], acc[1], acc[2],
			gyro[0], gyro[1], gyro[2]);
}

static void myo_imu_cb(GDBusProxy *proxy, GVariant *changed, GStrv invalid, gpointer user_data) {
	GVariantIter *iter;
	const gchar *key, *vals;
	GVariant *value;

	short quat[4], acc[3], gyro[3];

	if(g_variant_n_children(changed) > 0) {
		g_variant_get(changed, "a{sv}", &iter);
		while(g_variant_iter_loop(iter, "{&sv}", &key, &value)) {
			if(strcmp(key, "Value") == 0) {
				vals = g_variant_get_fixed_array(value, NULL, sizeof(gchar));
				memcpy(quat, vals, 8);
				memcpy(acc, vals + 8, 6);
				memcpy(gyro, vals + 14, 6);
				on_imu(quat, acc, gyro);
			}
		}
		g_variant_iter_free (iter);
	}
}

static void on_arm(ArmSide side, ArmXDirection dir) {
	printf(
			"On_Arm:\n"
			"Side: %s | XDirection: %s\n"
			"--------------------------------------\n",
			side2str(side), dir2str(dir));
}

static void on_pose(libmyo_pose_t pose) {
	printf(
			"On_Pose:\n"
			"Pose: %s\n"
			"--------------------------------------\n",
			pose2str(pose));
}

static void myo_arm_cb(GDBusProxy *proxy, GVariant *changed, GStrv invalid, gpointer user_data) {
	GVariantIter *iter;
	const gchar *key, *vals;
	GVariant *value;

	if(g_variant_n_children(changed) > 0) {
		g_variant_get(changed, "a{sv}", &iter);
		while(g_variant_iter_loop(iter, "{&sv}", &key, &value)) {
			if(strcmp(key, "Value") == 0) {
				vals = g_variant_get_fixed_array(value, NULL, sizeof(gchar));
				//typ, val, xdir = unpack('3B', pay)
				if(vals[0] == WORN) { // on arm
					on_arm(vals[1], vals[2]);
				} else if(vals[0] == REMOVED) { // removed from arm
					on_arm(NONE, UNKNOWN);
				} else if(vals[0] == POSE) { // pose
					on_pose(vals[1]);
				}
			}
		}
		g_variant_iter_free (iter);
	}
}

static void on_emg(short *emg, unsigned char moving) {
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
			moving);
}

static void myo_emg_cb(GDBusProxy *proxy, GVariant *changed, GStrv invalid, gpointer user_data) {
	GVariantIter *iter;
	const gchar *key, *vals;
	GVariant *value;

	short emg[8];
	unsigned char moving;

	if(g_variant_n_children(changed) > 0) {
		g_variant_get(changed, "a{sv}", &iter);
		while(g_variant_iter_loop(iter, "{&sv}", &key, &value)) {
			if(strcmp(key, "Value") == 0) {
				vals = g_variant_get_fixed_array(value, NULL, sizeof(gchar));
				//not entirely sure what the last byte is, but it's a bitmask
				//that seems to indicate which sensors think they're being moved
				//around or something
				memcpy(emg, vals, 16);
				moving = vals[16];
				on_emg(emg, moving);
			}
		}
		g_variant_iter_free (iter);
	}
}

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

int myo_get_name(myobluez_myo_t *bmyo, char *str) {
	GVariant *name;
	gsize length;
	Myo *myo = (Myo*) bmyo;

	name = g_dbus_proxy_get_cached_property(myo->proxy, "Alias");
	if(name == NULL) {
		return -1;
	}
	str = g_variant_dup_string(name, &length);
	g_variant_unref(name);
	return (int) length;
}

void myo_get_version(myobluez_myo_t *bmyo, unsigned char *ver) {
	GVariant *ver_var;
	const gchar *ver_arr;
	Myo *myo = (Myo*) bmyo;

	if(myo->version_data == NULL) {
		//error
		return;
	}

	ver_var = g_dbus_proxy_call_sync(
			myo->version_data, "ReadValue", NULL, G_DBUS_CALL_FLAGS_NONE, -1,
			NULL, &error);
	ASSERT(error, "Failed to get version");
	ver_arr = g_variant_get_fixed_array(ver_var, NULL, sizeof(gchar));

	//_, _, _, _, v0, v1, v2, v3 = unpack('BHBBHHHH', fw.payload)
	memcpy(ver, ver_arr + 4, 2);
	memcpy(ver, ver_arr + 6, 2);
	memcpy(ver, ver_arr + 8, 2);
	memcpy(ver, ver_arr + 10, 2);
}

void myo_EMG_notify_enable(myobluez_myo_t *bmyo, bool enable) {
	Myo *myo = (Myo*) bmyo;

	if(myo->emg_data == NULL) {
		return;
	}
	//call start notify or stop notify
	if(enable) {
		g_dbus_proxy_call_sync(myo->emg_data, "StartNotify", NULL,
								G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
		ASSERT(error, "EMG notify enable failed");
		if(myo->emg_sig_id == 0) {
			myo->emg_sig_id = g_signal_connect(myo->emg_data,
					"g-properties-changed", G_CALLBACK(myo_emg_cb), NULL);
		}
	} else {
		g_dbus_proxy_call_sync(myo->emg_data, "StopNotify", NULL,
				G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
		ASSERT(error, "EMG notify disable failed");
		if(myo->emg_sig_id != 0) {
			g_signal_handler_disconnect(myo->emg_data, myo->emg_sig_id);
		}
	}
}

void myo_IMU_notify_enable(myobluez_myo_t *bmyo, bool enable) {
	Myo *myo = (Myo*) bmyo;

	if(myo->imu_data == NULL) {
		return;
	}
	//call start notify or stop notify
	if(enable) {
		g_dbus_proxy_call_sync(myo->imu_data, "StartNotify", NULL,
								G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
		ASSERT(error, "IMU notify enable failed");
		if(myo->imu_sig_id == 0) {
			myo->imu_sig_id = g_signal_connect(myo->imu_data, "g-properties-changed",
											G_CALLBACK(myo_imu_cb), NULL);
		}
	} else {
		g_dbus_proxy_call_sync(myo->imu_data, "StopNotify", NULL,
								G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
		ASSERT(error, "IMU notify disable failed");
		if(myo->imu_sig_id != 0) {
			g_signal_handler_disconnect(myo->imu_data, myo->imu_sig_id);
		}
	}
}

void myo_arm_indicate_enable(myobluez_myo_t *bmyo, bool enable) {
	Myo *myo = (Myo*) bmyo;

	if(myo->arm_data == NULL) {
		return;
	}
	//call start notify or stop notify
	if(enable) {
		g_dbus_proxy_call_sync(myo->arm_data, "StartNotify", NULL,
								G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
		ASSERT(error, "Arm notify enable failed");
		if(myo->arm_sig_id == 0) {
			myo->arm_sig_id = g_signal_connect(myo->arm_data, "g-properties-changed",
											G_CALLBACK(myo_arm_cb), NULL);
		}
	} else {
		g_dbus_proxy_call_sync(myo->arm_data, "StopNotify", NULL,
				G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
		ASSERT(error, "Arm notify disable failed");
		if(myo->arm_sig_id != 0) {
			g_signal_handler_disconnect(myo->arm_data, myo->arm_sig_id);
		}
	}
}

void myo_update_enable(myobluez_myo_t *bmyo, bool emg, bool imu, bool arm) {
	GVariant *reply, *data_var;
	Myo *myo = (Myo*) bmyo;
	unsigned char data[] = {
			0x01, 0x03, 
			emg ? 0x01 : 0x00,
			imu ? 0x01 : 0x00,
			arm ? 0x01 : 0x00
	};

	data_var = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, data, 5,
											sizeof(unsigned char));

	reply = g_dbus_proxy_call_sync(myo->cmd_input, "WriteValue",
									g_variant_new_tuple(&data_var, 1),
									G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
	ASSERT(error, "Update enable failed");
	g_variant_unref(reply);
}

static void myo_initialize(Myo *myo) {
	unsigned char version[4];
	char name[25];

	//unsigned short C;
	//unsigned char emg_hz, emg_smooth, imu_hz;

	printf("Initializing...\n");

	//read firmware version
	myo_get_version((myobluez_myo_t) myo, version);
	printf("firmware version: %d.%d.%d.%d\n",
			version[0], version[1], version[2], version[3]);

	myo_get_name((myobluez_myo_t) myo, name);
	printf("device name: %s", name);

	//enable IMU data
	//myo_IMU_notify_enable(true);
	//enable on/off arm notifications
	myo_arm_indicate_enable((myobluez_myo_t) myo, true);

	myo_update_enable((myobluez_myo_t) myo, false, true, false);

	printf("Initialized!\n");
}

static int scan_myos() {
	GList *objects;
	GList *object;

	objects = g_dbus_object_manager_get_objects(
			(GDBusObjectManager*) bluez_manager);
	if(NULL == objects) {
		//failed
		fprintf(stderr, "Manager did not give us objects!\n");
		return 1;
	}

	debug("Searching objects for myo");
	do {
		object = NULL;
		object = g_list_find_custom(objects, NULL, is_device);
		if(object != NULL) {
			set_myo(g_dbus_object_get_object_path((GDBusObject*) object->data));
			objects = g_list_delete_link(objects, object);
		}
	} while(object != NULL && num_myos != MAX_MYOS);
	debug("Finished searching objects for myo");

	g_list_free_full(objects, g_object_unref);

	return 0;
}

static void stop_myobluez(int sig) {
	int i, j, k;
	GVariant *reply;

	//unref stuff
	for(i = 0; i < MAX_MYOS; i++) {
		for(k = 0; k < NUM_SERVICES; k++) {
			for(j = 0; j < myos[i].services[k].num_chars; j++) {
				if(G_IS_DBUS_PROXY(myos[i].services[k].char_proxies[j])) {
					g_object_unref(myos[i].services[k].char_proxies[j]);
					myos[i].services[k].char_proxies[j] = NULL;
				}
			}
			if(G_IS_DBUS_PROXY(myos[i].services[k].proxy)) {
				debug("Freeing service proxy");
				g_object_unref(myos[i].services[k].proxy);
				myos[i].services[k].proxy = NULL;
			}
		}

		if(G_IS_DBUS_PROXY(myos[i].proxy)) {
			if(myos[i].status == CONNECTED) {
				//disconnect
				debug("Disconnecting from myo");
				reply = g_dbus_proxy_call_sync(
						myos[i].proxy, "Disconnect", NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL,
						&error);
				ASSERT(error, "Disconnect failed");
				g_variant_unref(reply);
			}
			//TODO: maybe remove device to work around bluez bug
			debug("Freeing myo proxy");
			g_object_unref(myos[i].proxy);
			myos[i].proxy = NULL;
		}
	}

	if(cb_id != 0 && G_IS_OBJECT(bluez_manager)) {
		debug("Disconnecting signal handler");
		g_signal_handler_disconnect(bluez_manager, cb_id);
		cb_id = 0;
	}
	
	if(G_IS_OBJECT(bluez_manager)) {
		debug("Freeing bluez manager");
		g_object_unref(bluez_manager);
		bluez_manager = NULL;
	}

	if(loop != NULL) {
		if(g_main_loop_is_running(loop)) {
			g_main_loop_quit(loop);
		}

		g_main_loop_unref(loop);
		loop = NULL;
	}
}

int main(void) {
	int i;

	signal(SIGINT, stop_myobluez);

	for(i = 0; i < MAX_MYOS; i++) {
		init_GattService(&myos[i].battery_service, BATT_UUID, BATT_CHAR_UUIDS, 1);
		init_GattService(&myos[i].myo_control_service, MYO_UUID, MYO_CHAR_UUIDS, 3);
		init_GattService(&myos[i].imu_service, IMU_UUID, IMU_CHAR_UUIDS, 2);
		init_GattService(&myos[i].arm_service, ARM_UUID, ARM_CHAR_UUIDS, 1);
		init_GattService(&myos[i].emg_service, EMG_UUID, EMG_CHAR_UUIDS, 1);
	}
	num_myos = 0;

	loop = g_main_loop_new(NULL, false);

	bluez_manager =
			(GDBusObjectManagerClient*) g_dbus_object_manager_client_new_for_bus_sync(
					G_BUS_TYPE_SYSTEM, G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
					"org.bluez", "/", NULL, NULL, NULL, NULL, &error);
	ASSERT(error, "Get ObjectManager failed");
	if(bluez_manager == NULL) {
		// we have major problems
		stop_myobluez(0);
		fprintf(stderr, "Error: Is Bluez running?\n");
		return 1;
	}

	cb_id = g_signal_connect(bluez_manager, "object-added",
			G_CALLBACK(object_added_cb), NULL);

	if(scan_myos()) {
		fprintf(stderr, "Error finding Myo!\n");
		stop_myobluez(0);
		return 1;
	}

	debug("Running Main Loop\n");
	g_main_loop_run(loop);

	return 0;
}
