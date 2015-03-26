#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <bluetooth/bluetooth.h>

#include <glib.h>
#include <gio/gio.h>

#include "myo-bluez.h"

#define ASSERT(GERR, MSG) \
    if(GERR != NULL) { \
        g_assert(GERR); \
        fprintf(stderr, "%s: %s\n", MSG, GERR->message); \
        g_error_free(GERR); \
        GERR = NULL; \
    }
    
static const char *GAP_UUID = "00001800-0000-1000-8000-00805f9b34fb";
static const char *GAP_CHAR_UUIDS[] = {"00002a00-0000-1000-8000-00805f9b34fb",
                                        "00002a01-0000-1000-8000-00805f9b34fb",
                                        "00002a04-0000-1000-8000-00805f9b34fb"};

static const char *BATT_UUID = "0000180f-0000-1000-8000-00805f9b34fb";
static const char *BATT_CHAR_UUIDS[] = {"00002a19-0000-1000-8000-00805f9b34fb"};

static const char *MYO_UUID = "d5060001-a904-deb9-4748-2c7f4a124842";
static const char *MYO_CHAR_UUIDS[] = {"d5060101-a904-deb9-4748-2c7f4a124842",
                                        "d5060201-a904-deb9-4748-2c7f4a124842",
                                        "d5060401-a904-deb9-4748-2c7f4a124842"};

static const char *IMU_UUID = "d5060002-a904-deb9-4748-2c7f4a124842";
static const char *IMU_CHAR_UUIDS[] = {"d5060402-a904-deb9-4748-2c7f4a124842",
                                        "d5060502-a904-deb9-4748-2c7f4a124842"};

static const char *ARM_UUID = "d5060003-a904-deb9-4748-2c7f4a124842";
static const char *ARM_CHAR_UUIDS[] = {"d5060103-a904-deb9-4748-2c7f4a124842"};

static const char *EMG_UUID = "d5060004-a904-deb9-4748-2c7f4a124842";
static const char *EMG_CHAR_UUIDS[] = {"d5060104-a904-deb9-4748-2c7f4a124842"};
    
static GError *error;
static GMainLoop *loop;

static int new;
static gulong myo_conn_id;
static gulong service_conn_id;

static GDBusObjectManagerClient *bluez_manager;
static GDBusProxy *adapter;
static GDBusProxy *myo;

static MyoStatus status = DISCONNECTED;

typedef struct {
    const char *UUID;
    GDBusProxy *proxy;
    GDBusObjectManagerClient *gatt_manager;
    const char **char_UUIDs;
    GDBusProxy **char_proxies;
    int num_chars;
} GattService;

//TODO: add unknown services
#define NUM_SERVICES 6
static GattService services[NUM_SERVICES];

#define generic_access_service services[0]
#define battery_service services[1]
#define myo_control_service services[2]
#define imu_service services[3]
#define arm_service services[4]
#define emg_service services[5]

#define version_data myo_control_service.char_proxies[1]
#define cmd_input myo_control_service.char_proxies[2]
#define imu_data imu_service.char_proxies[0]
#define arm_data arm_service.char_proxies[0]
#define emg_data emg_service.char_proxies[0]

static gulong imu_sig_id;
static gulong arm_sig_id;
static gulong emg_sig_id;

static void interface_added_characteristic(GDBusObjectManager *manager,
        GDBusObject *object, GDBusInterface *interface, gpointer user_data);
static void interface_added_service(GDBusObjectManager *manager,
        GDBusObject *object, GDBusInterface *interface, gpointer user_data);

static void init_GattService(GattService *service, const char *UUID,
                                    const char **char_UUIDs, int num_chars) {
    service->UUID = UUID;
    service->char_UUIDs = char_UUIDs;
    service->num_chars = num_chars;
    service->char_proxies = calloc(num_chars, sizeof(GDBusProxy*));
}

static gint is_adapter(gconstpointer a, gconstpointer b) {
    GDBusInterface *interface;
    
    interface = g_dbus_object_get_interface((GDBusObject*) a,
                                                        "org.bluez.Adapter1");
    if(NULL == interface) {
        return 1;
    } else {
        g_object_unref(interface);
        return 0;
    }
}

static gint is_myo(gconstpointer a, gconstpointer b) {
    GDBusProxy *proxy;
    GVariant *UUIDs_var;
    GVariantIter *iter;
    const gchar *uuid;
    
    gint ret;
    
    ret = 1;
    
    proxy = (GDBusProxy*) g_dbus_object_get_interface((GDBusObject*) a,
                                                        "org.bluez.Device1");
    if(proxy != NULL) {
        UUIDs_var = g_dbus_proxy_get_cached_property(proxy, "UUIDs");
        if(UUIDs_var != NULL) {
            g_variant_get(UUIDs_var, "as", &iter);
            while(g_variant_iter_loop(iter, "s", &uuid)) {
                if(strcmp(uuid, MYO_UUID) == 0) {
                    ret = 0;
                    break;
                }
            }
            
            g_variant_iter_free (iter);
        }
        
        g_variant_unref(UUIDs_var);
        g_object_unref(proxy);
    }
    
    return ret;
}

static gint is_service(gconstpointer a, gconstpointer b) {
    GDBusInterface *interface;
    
    interface = g_dbus_object_get_interface((GDBusObject*) a,
                                                    "org.bluez.GattService1");
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
                                                    "org.bluez.GattService1");
    if(NULL == interface) {
        return 1;
    } else {
        g_object_unref(interface);
        return 0;
    }
}

static void set_characteristic(GDBusObject *object) {
    int i, j;
    GDBusProxy *service, *proxy;
    GVariant *UUID, *serv_UUID, *path;
    const char *UUID_str, *serv_UUID_str;
    
    printf("Set char\n");
    
    proxy = g_dbus_proxy_new_for_bus_sync(
                            G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE, NULL,
                            "org.bluez", g_dbus_object_get_object_path(object),
                            "org.bluez.GattCharacteristic1", NULL, &error);
    ASSERT(error, "Get characteristic failed");
    
    //check UUIDs for which service
    //get UUID
    UUID = g_dbus_proxy_get_cached_property((GDBusProxy*) object, "UUID");
    if(UUID == NULL) {
        return;
    }
    UUID_str = g_variant_get_string(UUID, NULL);
    
    printf("Char: %s\n", UUID_str);
    
    //get service object path
    path = g_dbus_proxy_get_cached_property((GDBusProxy*) object, "Service");
    if(path == NULL) {
        return;
    }
    
    service = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
                                    G_DBUS_PROXY_FLAGS_NONE, NULL, "org.bluez",
                                    g_variant_get_string(path, NULL),
                                    "org.bluez.GattService1", NULL, &error);
    ASSERT(error, "Get service for characteristic failed");
    
    //get service UUID string
    serv_UUID = g_dbus_proxy_get_cached_property(service, "UUID");
    if(serv_UUID == NULL) {
        printf("Set char failed\n");
        return;
    }
    serv_UUID_str = g_variant_get_string(serv_UUID, NULL);
    
    for(i = 0; i < NUM_SERVICES; i++) {
        if(strcmp(services[i].UUID, serv_UUID_str) == 0) {
            break;
        }
    }
    
    for(j = 0; j < services[i].num_chars; j++) {
        if(strcmp(services[i].char_UUIDs[j], UUID_str) == 0) {
            printf("Char set\n");
            services[i].char_proxies[j] = proxy;
            
            g_object_unref(service);
    
            g_variant_unref(serv_UUID);
            g_variant_unref(UUID);
            return;
        }
    }
    
    //proxy did not get set
    g_object_unref(service);
    
    g_variant_unref(serv_UUID);
    g_variant_unref(UUID);
    g_object_unref(proxy);
}

static void set_service(GDBusObject *object) {
    int i;
    GDBusProxy *proxy;
    GVariant *UUID;
    const char *UUID_str;
    
    printf("Set service\n");
    
    //check UUIDs for which service
    //get UUID
    proxy = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
                    G_DBUS_PROXY_FLAGS_NONE, NULL, "org.bluez", g_dbus_object_get_object_path(object),
                    "org.bluez.GATTService1", NULL, &error);
    ASSERT(error, "Get service proxy failed");
    UUID = g_dbus_proxy_get_cached_property(proxy, "UUID");
    if(UUID == NULL) {
        printf("Set service failed\n");
        return;
    }
    UUID_str = g_variant_get_string(UUID, NULL);
    
    printf("Service: %s\n", UUID_str);
    
    for(i = 0; i < NUM_SERVICES; i++) {
        if(strcmp(UUID_str, services[i].UUID) == 0) {
            printf("Service set\n");
            services[i].proxy = proxy;
            
            services[i].gatt_manager = (GDBusObjectManagerClient*)
                g_dbus_object_manager_client_new_for_bus_sync(
                    G_BUS_TYPE_SYSTEM, G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
                    "org.bluez", g_dbus_object_get_object_path(object), NULL, 
                    NULL, NULL, NULL, &error);
            ASSERT(error, "Get gatt manager failed");
            
            g_signal_connect(services[i].gatt_manager, "interface-added",
                                G_CALLBACK(interface_added_characteristic), NULL);
            g_variant_unref(UUID);
            return;
        }
    }
    
    //proxy was not set
    g_variant_unref(UUID);
    g_object_unref(proxy);
}

static int set_myo(GDBusObject *object) {
    GList *objects;
    GList *_object;
    
    GVariant *reply;
    
    printf("Myo found!\n");
    
    //get path from proxy
    myo = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
                                G_DBUS_PROXY_FLAGS_NONE, NULL, "org.bluez",
                                g_dbus_object_get_object_path(object),
                                "org.bluez.Device1", NULL, &error);
    ASSERT(error, "Get Myo Proxy failed");
    if(myo == NULL) {
        return 1;
    }
    
    printf("Connecting...\n");
    reply = g_dbus_proxy_call_sync(myo, "Connect", NULL, G_DBUS_CALL_FLAGS_NONE,
                                                            -1, NULL, &error);
    if(error != NULL) {
        g_assert(error);
        fprintf(stderr, "%s: %s\n", "Connection failed", error->message);
        g_error_free(error);
        error = NULL;
        return 1;
    }
    g_variant_unref(reply);
    
    //set watcher for disconnect
    g_signal_connect(myo, "g-properties-changed",
                                            G_CALLBACK(disconnect_cb), NULL);
    
    status = CONNECTED;
    printf("Connected!\n");
    
    //search for new services
    g_signal_connect(bluez_manager, "interface-added",
                                    G_CALLBACK(interface_added_service), NULL);
    
    //search for services that are already registered
    objects = g_dbus_object_manager_get_objects(
                                        (GDBusObjectManager*) bluez_manager);
    if(NULL == objects) {
        //failed
        fprintf (stderr, "Manger did not give us objects!\n");
        return 1;
    }
    
    do {
        _object = g_list_find_custom(objects, NULL, is_service);
        if(_object == NULL) {
            break;
        }
        set_service(_object->data);
        objects = g_list_delete_link(objects, _object);
    } while(object != NULL);
    
    g_list_free_full(objects, g_object_unref);
    
    myo_initialize();
    
    return 0;
}

static void disconnect_cb(GDBusProxy *proxy, GVariant *changed, GStrv invalid,
                                                        gpointer user_data) {
    GVariantIter *iter;
    const gchar *key;
    GVariant *value;
    
    //check if peroperty was "connected"
    if(g_variant_n_children(changed) > 0) {
        g_variant_get(changed, "a{sv}", &iter);
        while(g_variant_iter_loop(iter, "{&sv}", &key, &value)) {
            if(strcmp(key, "Connected") == 0) {
                if(!g_variant_get_boolean(value)) {
                    //disconnected
                    status = DISCONNECTED;
                    printf("Myo disconnected\n");
                    reply = g_dbus_proxy_call_sync(myo, "Disconnect", NULL,
                                    G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
                    ASSERT(error, "Disconnect failed");
                    g_variant_unref(reply);
                    
                    printf("Connecting...\n");
                    reply = g_dbus_proxy_call_sync(myo, "Connect", NULL,
                                    G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
                    if(error != NULL) {
                        g_assert(error);
                        fprintf(stderr, "%s: %s\n", "Connection failed",
                                                                error->message);
                        g_error_free(error);
                        error = NULL;
                        g_variant_iter_free(iter);
                        stop_myo();
                    }
                    g_variant_unref(reply);
                    
                    status = CONNECTED;
                    printf("Connected!\n");
                }
            }
        }
        g_variant_iter_free(iter);
    }
}

static void on_imu(short *quat, short *acc, short *gyro) {
    printf("On_IMU:\n"
            "Quat - X: %d | Y: %d | Z: %d | W: %d\n"
            "Acc - X: %d | Y: %d | Z: %d\n"
            "Gyro - X: %d | Y: %d | Z: %d\n"
            "--------------------------------------\n",
            quat[0], quat[1], quat[2], quat[3],
            acc[0], acc[1], acc[2],
            gyro[0], gyro[1], gyro[2]);
}

static void myo_imu_cb(GDBusProxy *proxy, GVariant *changed, GStrv invalid,
                                                        gpointer user_data) {
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
    printf("On_Arm:\n"
            "Side: %s | XDirection: %s\n"
            "--------------------------------------\n",
            side2str(side), dir2str(dir));
}

static void on_pose(libmyo_pose_t pose) {
    printf("On_Pose:\n"
            "Pose: %s\n"
            "--------------------------------------\n",
            pose2str(pose));
}

static void myo_arm_cb(GDBusProxy *proxy, GVariant *changed, GStrv invalid,
                                                        gpointer user_data) {
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
    printf("On_EMG:\n"
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

static void myo_emg_cb(GDBusProxy *proxy, GVariant *changed, GStrv invalid,
                                                        gpointer user_data) {
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

static void interface_added_myo(GDBusObjectManager *manager,
        GDBusObject *object, GDBusInterface *interface, gpointer user_data) {
    GVariant *reply;
    
    if(adapter != NULL && myo == NULL && is_myo(object, NULL)) {
        reply = g_dbus_proxy_call_sync(adapter, "StopDiscovery", NULL,
                                G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
        ASSERT(error, "StopDiscovery failed");
        g_variant_unref(reply);
        
        g_signal_handler_disconnect(bluez_manager, myo_conn_id);
        
        set_myo(object);
    }
}

static void interface_added_service(GDBusObjectManager *manager,
        GDBusObject *object, GDBusInterface *interface, gpointer user_data) {
    if(is_service(object, NULL)) {
        set_service(object);
    }
}

static void interface_added_characteristic(GDBusObjectManager *manager,
        GDBusObject *object, GDBusInterface *interface, gpointer user_data) {
    if(is_characteristic(object, NULL)) {
        set_characteristic(object);
    }
}

static int get_adapter() {
    GList *objects;
    GList *object;
    
    objects = g_dbus_object_manager_get_objects(
                                        (GDBusObjectManager*) bluez_manager);
    if(NULL == objects) {
        //failed
        fprintf (stderr, "Manger did not give us objects!\n");
        return 1;
    }
    object = g_list_find_custom(objects, NULL, is_adapter);
    if(NULL == object) {
        //failed
        fprintf (stderr, "Adapter not in objects!\n");
        return 1;
    }
    
    adapter = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
                    G_DBUS_PROXY_FLAGS_NONE, NULL, "org.bluez",
                    g_dbus_object_get_object_path((GDBusObject*) object->data),
                    "org.bluez.Adapter1", NULL, &error);
    ASSERT(error, "Get Proxy for Adapter failed");
    
    g_list_free_full(objects, g_object_unref);
    
    return 0;
}

int myo_get_name(char *str) {
    GVariant *name;
    int length;
    name = g_dbus_proxy_get_cached_property(myo, "Alias");
    if(name == NULL) {
        return -1;
    }
    str = g_variant_dup_string(name, &length);
    g_variant_unref(name);
    return length;
}

void myo_get_version(char *ver) {
    GVariant *ver_var;
    const gchar *ver_arr;
    
    if(version_data == NULL) {
        //error
        return;
    }
    
    ver_var = g_dbus_proxy_call_sync(version_data, "ReadValue", NULL,
                                    G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
    ASSERT(error, "Failed to get version");
    ver_arr = g_variant_get_fixed_array(ver_var, NULL, sizeof(gchar));
    
    //_, _, _, _, v0, v1, v2, v3 = unpack('BHBBHHHH', fw.payload)
    memcpy(ver, ver_arr + 4, 2);
    memcpy(ver, ver_arr + 6, 2);
    memcpy(ver, ver_arr + 8, 2);
    memcpy(ver, ver_arr + 10, 2);
}

void myo_EMG_notify_enable(bool enable) {
    if(emg_data == NULL) {
        return;
    }
    //call start notify or stop notify
    if(enable) {
        g_dbus_proxy_call_sync(emg_data, "StartNotify", NULL,
                                    G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
        ASSERT(error, "EMG notify enable failed");
        if(emg_sig_id == 0) {
            emg_sig_id = g_signal_connect(emg_data, "g-properties-changed",
                                                G_CALLBACK(myo_emg_cb), NULL);
        }
    } else {
        g_dbus_proxy_call_sync(emg_data, "StopNotify", NULL,
                                    G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
        ASSERT(error, "EMG notify disable failed");
        if(emg_sig_id != 0) {
            g_signal_handler_disconnect(emg_data, emg_sig_id);
        }
    }
}

void myo_IMU_notify_enable(bool enable) {
    if(imu_data == NULL) {
        return;
    }
    //call start notify or stop notify
    if(enable) {
        g_dbus_proxy_call_sync(imu_data, "StartNotify", NULL,
                                    G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
        ASSERT(error, "IMU notify enable failed");
        if(imu_sig_id == 0) {
            imu_sig_id = g_signal_connect(imu_data, "g-properties-changed",
                                                G_CALLBACK(myo_imu_cb), NULL);
        }
    } else {
        g_dbus_proxy_call_sync(imu_data, "StopNotify", NULL,
                                    G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
        ASSERT(error, "IMU notify disable failed");
        if(imu_sig_id != 0) {
            g_signal_handler_disconnect(imu_data, imu_sig_id);
        }
    }
}

void myo_arm_indicate_enable(bool enable) {
    if(arm_data == NULL) {
        return;
    }
    //call start notify or stop notify
    if(enable) {
        g_dbus_proxy_call_sync(arm_data, "StartNotify", NULL,
                                    G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
        ASSERT(error, "Arm notify enable failed");
        if(arm_sig_id == 0) {
            arm_sig_id = g_signal_connect(arm_data, "g-properties-changed",
                                                G_CALLBACK(myo_arm_cb), NULL);
        }
    } else {
        g_dbus_proxy_call_sync(arm_data, "StopNotify", NULL,
                                    G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
        ASSERT(error, "Arm notify disable failed");
        if(arm_sig_id != 0) {
            g_signal_handler_disconnect(arm_data, arm_sig_id);
        }
    }
}

void myo_update_enable(bool emg, bool imu, bool arm) {
    GVariant *reply, *data_var;
    unsigned char data[] = {0x01, 0x03, emg ? 0x01 : 0x00,
                                        imu ? 0x01 : 0x00,
                                        arm ? 0x01 : 0x00};
                                        
    data_var = g_variant_new_fixed_array(
                        G_VARIANT_TYPE_BYTE, data, 5, sizeof(unsigned char));
    
    reply = g_dbus_proxy_call_sync(cmd_input, "WriteValue",
                                    g_variant_new_tuple(&data_var, 1),
                                    G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
    ASSERT(error, "Update enable failed");
    g_variant_unref(reply);
}

static void myo_initialize() {
    unsigned char version[4];
    char name[25];
    
    //unsigned short C;
    //unsigned char emg_hz, emg_smooth, imu_hz;
    
    printf("Initializing...\n");
    /*
    //read firmware version
    myo_get_version(version);
    printf("firmware version: %d.%d.%d.%d\n", version[0], version[1],
                                                        version[2], version[3]);
    
    new = version[0];
    //if old do the thing
    //else do the other thing (configure)
    if(!new) {
        //don't know what these do; Myo Connect sends them, though we get data
        //fine without them
        
        //writeValue[0] = 0x01;
        //writeValue[1] = 0x02;
        //writeValue[2] = 0x00;
        //writeValue[3] = 0x00;
        //write_char(0x19, writeValue, 4);
        
        //writeValue[1] = 0x00;
        //write_char(0x2f, writeValue, 2);
        //write_char(0x2c, writeValue, 2);
        //write_char(0x32, writeValue, 2);
        //write_char(0x35, writeValue, 2);

        //enable EMG data
        myo_EMG_notify_enable(true);
        //enable IMU data
        myo_IMU_notify_enable(true);

        //Sampling rate of the underlying EMG sensor, capped to 1000. If it's
        //less than 1000, emg_hz is correct. If it is greater, the actual
        //framerate starts dropping inversely. Also, if this is much less than
        //1000, EMG data becomes slower to respond to changes. In conclusion,
        //1000 is probably a good value.
        
        //C = 1000;
        //emg_hz = 50;
        //strength of low-pass filtering of EMG data
        //emg_smooth = 100;

        //imu_hz = 50;

        //send sensor parameters, or we don't get any data
        //writeValue[0] = 2;
        //writeValue[1] = 9;
        //writeValue[2] = 2;
        //writeValue[3] = 1;
        //*((short*) (writeValue + 4)) = C;
        //writeValue[6] = emg_smooth;
        //writeValue[7] = C/emg_hz;
        //writeValue[8] = imu_hz;
        //writeValue[9] = 0;
        //writeValue[10] = 0;
        //write_char(0x19, writeValue, 11);
    } else {
        myo_get_name(name);
        printf("device name: %s", name);

        //enable IMU data
        //myo_IMU_notify_enable(true);
        //enable on/off arm notifications
        myo_arm_indicate_enable(true);

        myo_update_enable(false, true, false);
    }
    */
    
    printf("Initialized!\n");
}

static int myo_scan() {
    GList *objects;
    GList *object;
    
    GVariant *reply;
    
    //get adapter
    objects = g_dbus_object_manager_get_objects(
                                        (GDBusObjectManager*) bluez_manager);
    if(NULL == objects) {
        //failed
        fprintf (stderr, "Manger did not give us objects!\n");
        return 1;
    }
    
    if(myo == NULL) {
        //check to make sure Myo has not already been found
        object = g_list_find_custom(objects, NULL, is_myo);
        if(NULL != object) {
            //Myo has been found so we don't have to scan for it
            //TODO: might be a problem if there are multiple Myos and this one
            //cannot be connected to
            myo_connect((GDBusObject*) object->data);
        }
        
        //connecting to myo failed or myo not found, so go searching
        if(myo == NULL) {
            //scan to find Myo devices
            printf("Discovery started\n");
            
            myo_conn_id = g_signal_connect(bluez_manager, "interface-added",
                                        G_CALLBACK(interface_added_myo), NULL);
            
            reply = g_dbus_proxy_call_sync(adapter, "StartDiscovery", NULL,
                                    G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
            ASSERT(error, "StartDiscovery failed");
            g_variant_unref(reply);
        }
    }
    
    g_list_free_full(objects, g_object_unref);
    
    return 0;
}

static void stop_myo(int sig) {
    int i, j;
    GVariant *reply;
    
    //unref stuff
    if(myo != NULL) {
        if(status == CONNECTED) {
            //disconnect
            reply = g_dbus_proxy_call_sync(myo, "Disconnect", NULL,
                                    G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
            g_variant_unref(reply);
        }
        g_object_unref(myo);
        myo = NULL;
    }
    if(adapter != NULL) {
        g_object_unref(adapter);
        adapter = NULL;
    }
    if(bluez_manager != NULL) {
        g_object_unref(bluez_manager);
        bluez_manager = NULL;
    }
    
    for(i = 0; i < NUM_SERVICES; i++) {
        for(j = 0; j < services[i].num_chars; j++) {
            if(services[i].char_proxies[j] != NULL) {
                g_object_unref(services[i].char_proxies[j]);
            }
        }
        if(services[i].proxy != NULL) {
            g_object_unref(services[i].proxy);
        }
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
    signal(SIGINT, stop_myo);
    
    init_GattService(&generic_access_service, GAP_UUID, GAP_CHAR_UUIDS, 3);
    init_GattService(&battery_service, BATT_UUID, BATT_CHAR_UUIDS, 1);
    init_GattService(&myo_control_service, MYO_UUID, MYO_CHAR_UUIDS, 3);
    init_GattService(&imu_service, IMU_UUID, IMU_CHAR_UUIDS, 2);
    init_GattService(&arm_service, ARM_UUID, ARM_CHAR_UUIDS, 1);
    init_GattService(&emg_service, EMG_UUID, EMG_CHAR_UUIDS, 1);
    
    loop = g_main_loop_new(NULL, false);
    
    bluez_manager =
        (GDBusObjectManagerClient*) g_dbus_object_manager_client_new_for_bus_sync(
            G_BUS_TYPE_SYSTEM, G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
            "org.bluez", "/", NULL, NULL, NULL, NULL, &error);
    ASSERT(error, "Get ObjectManager failed");
    
    //scan for Myo
    if(get_adapter() || myo_scan()) {
        fprintf(stderr, "Error finding Myo!\n");
        
        stop_myo(0);
        
        return 1;
    }
    
    g_main_loop_run(loop);
    
    stop_myo(0);
    
    return 0;
}
