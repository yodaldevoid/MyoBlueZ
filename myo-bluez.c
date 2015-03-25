#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <bluetooth/bluetooth.h>

#include myo-bluez.h

#define ASSERT(GERR, MSG) \
    if(GERR != NULL) { \
        g_assert(GERR); \
        fprintf (stderr, "%s: %s\n",MSG ,GERR->message); \
        g_error_free(GERR); \
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

static MyoStatus status;

static typedef struct GattService {
    const char *UUID;
    GDBusProxy *proxy;
    GDBusObjectManagerClient *gatt_manager;
    const char **char_UUIDs;
    GDBusProxy **char_proxies;
    int num_chars;
} GattService;

//TODO: add unknown services
#define NUM_SERVICES 6
static GattService services[NUM_SERVICES] = {
    {GAP_UUID, NULL, NULL, GAP_CHAR_UUIDS, NULL, 3},
    {BATT_UUID, NULL, NULL, BATT_CHAR_UUIDS, NULL, 1},
    {MYO_UUID, NULL, NULL, MYO_CHAR_UUIDS, NULL, 3},
    {IMU_UUID, NULL, NULL, IMU_CHAR_UUIDS, NULL, 2},
    {ARM_UUID, NULL, NULL, ARM_CHAR_UUIDS, NULL, 1},
    {EMG_UUID, NULL, NULL, EMG_CHAR_UUIDS, NULL, 1}
};

#define generic_access_service services[0]
#define battery_service services[1]
#define myo_control_service services[2]
#define imu_service services[3]
#define arm_service services[4]
#define emg_service services[5]

#define version myo_control_service.char_proxies[1]
#define cmd_input myo_control_service.char_proxies[2]
#define imu_data imu_service.char_proxies[0]
#define arm_data arm_service.char_proxies[0]
#define emg_data emg_service.char_proxies[0]

static gulong imu_sig_id;
static gulong arm_sig_id;
static gulong emg_sig_id;

static int myo_connect();

static void init_GattService(GattService *service) {
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

static void set_service(GDBusObject *object) {
    GVariant *UUID;
    const char *UUID_str;
    
    //check UUIDs for which service
    //get UUID
    UUID = g_dbus_proxy_get_cached_property((GDBusProxy*) object, "UUID");
    if(UUID == NULL) {
        return;
    }
    UUID_str = g_variant_get_string(UUID);
    
    for(i = 0; i < NUM_SERVICES; i++) {
        if(strcmp(UUID_str, services[i].UUID) == 0) {
            services[i].service = g_dbus_proxy_new_for_bus_sync(
                            G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE, NULL,
                            "org.bluez", g_dbus_object_get_object_path(object),
                            "org.bluez.GATTService1", NULL, &error);
            ASSERT(error, "Get generic_access_service failed");
            
            services.gatt_manager = (GDBusObjectManagerClient*)
                g_dbus_object_manager_client_new_for_bus_sync(
                    G_BUS_TYPE_SYSTEM, G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
                    "org.bluez", g_dbus_object_get_object_path(object), NULL, 
                    NULL, NULL, NULL, &error);
            ASSERT(error, "Get ObjectManager failed");
            
            g_signal_connect(gatt_manager, "interface-added",
                                G_CALLBACK(is_interface_characteristic), NULL);
        }
    }
    
    g_variant_unref(UUID);
}

static void set_characteristic(GDBusObject *object) {
    int i, j;
    GDBusProxy *service;
    GVariant *UUID, *serv_UUID, *path;
    const char *UUID_str, *serv_UUID_str;
    
    //check UUIDs for which service
    //get UUID
    UUID = g_dbus_proxy_get_cached_property((GDBusProxy*) object, "UUID");
    if(UUID == NULL) {
        return;
    }
    UUID_str = g_variant_get_string(UUID);
    
    //get service object path
    path = g_dbus_proxy_get_cached_property((GDBusProxy*) object, "Service");
    if(path == NULL) {
        return;
    }
    
    service = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
                                    G_DBUS_PROXY_FLAGS_NONE, NULL, "org.bluez",
                                    g_variant_get_string(path),
                                    "org.bluez.GattService1", NULL, &error);
    ASSERT(error, "Get service for characteristic failed");
    
    //get service UUID string
    serv_UUID = g_dbus_proxy_get_cached_property(service, "UUID");
    if(serv_UUID == NULL) {
        return;
    }
    serv_UUID_str = g_variant_get_string(serv_UUID);
    
    for(i = 0; i < NUM_SERVICES; i++) {
        if(strcmp(services[i].UUID, serv_UUID_str) == 0) {
            break;
        }
    }
    
    for(j = 0; j < services[i].num_chars; j++) {
        if(strcmp(services[i].char_UUIDs[j], UUID_str) == 0) {
            services[i].char_proxies[j] = g_dbus_proxy_new_for_bus_sync(
                            G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE, NULL,
                            "org.bluez", g_dbus_object_get_object_path(object),
                            "org.bluez.GattCharacteristic1", NULL, &error);
            ASSERT(error, "Get characteristic failed");
        }
    }
    
    g_object_unref(service);
    
    g_variant_unref(serv_UUID);
    g_variant_unref(UUID);
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
                    //TODO: Exit if connect fails
                    myo_connect();
                }
            }
        }
        g_variant_iter_free (iter);
    }
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
                    on_arm(vlas[1], vals[2]);
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
        
        myo_connect(object);
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
    name = g_dbus_proxy_get_cached_property((GDBusProxy*) object, "Alias");
    str = g_variant_dup_string(name, &length);
    g_variant_unref(name);
    return length;
}

void myo_get_version(char *ver) {
    GVariant *ver_var;
    gchar *ver_arr;
    
    ver_var = g_dbus_proxy_call_sync(cmd_input, "WriteValue", data_var,
                                    G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
                                    
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
    g_dbus_proxy_call_sync(emg_data, enable ? "StartNotify" : "StopNotify",
                                NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
    ASSERT(error, "EMG notify enable/disable failed");
    
    emg_sig_id = g_signal_connect(myo, "g-properties-changed", G_CALLBACK(myo_emg_cb), NULL);
}

void myo_IMU_notify_enable(bool enable) {
    if(imu_data == NULL) {
        return;
    }
    //call start notify or stop notify
    g_dbus_proxy_call_sync(imu_data, enable ? "StartNotify" : "StopNotify",
                                NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
    ASSERT(error, "IMU notify enable/disable failed");
    
    imu_sig_id = g_signal_connect(myo, "g-properties-changed", G_CALLBACK(myo_imu_cb), NULL);
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
    GVariant *data_var;
    unsigned char data[] = {0x01, 0x03, emg ? 0x01 : 0x00,
                                        imu ? 0x01 : 0x00,
                                        arm ? 0x01 : 0x00};
    
    g_dbus_proxy_call_sync(cmd_input, "WriteValue",
                    g_variant_new_fixed_array(
                        G_VARIANT_TYPE_BYTE, data, 5, sizeof(unsigned char)),
                    G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
}

static void myo_initialize() {
    unsigned char version[4];
    char name[25];
    
    //unsigned short C;
    //unsigned char emg_hz, emg_smooth, imu_hz;
    
    printf("Initializing...\n");
    
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
        myo_IMU_notify_enable(true);
        //enable on/off arm notifications
        myo_arm_indicate_enable(true);

        myo_update_enable(false, true, false);
    }
    
    printf("Initialized!\n");
}

static int myo_connect(GDBusObject *object) {
    GList *objects;
    GList *object;
    
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
    
    //set watcher for disconnect
    g_signal_connect(myo, "g-properties-changed",
                                            G_CALLBACK(disconnect_cb), NULL);
    
    printf("Connecting...\n");
    reply = g_dbus_proxy_call_sync(myo, "Connect", NULL, G_DBUS_CALL_FLAGS_NONE,
                                                            -1, NULL, &error);
    ASSERT(error, "Connect failed");
    //TODO: return error on failed connect
    g_variant_unref(reply);
    
    status = CONNECTED;
    printf("Connected!\n");
    
    //search for
    g_signal_connect(bluez_manager, "interface-added",
                                G_CALLBACK(is_interface_service), NULL);
    
    //search for services that are already registered
    objects = g_dbus_object_manager_get_objects(
                                        (GDBusObjectManager*) bluez_manager);
    if(NULL == objects) {
        //failed
        fprintf (stderr, "Manger did not give us objects!\n");
        return 1;
    }
    
    do {
        object = g_list_find_custom(objects, NULL, is_service);
        if(object == NULL) {
            break;
        }
        set_service(object->data);
        objects = g_list_delete_link(objects, object);
    } while(object != NULL);
    
    g_list_free_full(objects, g_object_unref);
    
    myo_initialize();
    
    return 0;
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
        for(j = 0; j < service[i].num_chars; j++) {
            if(service[i].char_proxies[j] != NULL) {
                g_object_unref(service[i].char_proxies[j]);
            }
        }
        if(service[i].proxy != NULL) {
            g_object_unref(service[i].proxy);
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
    
    myo_conn_id = 0;
    service_conn_id = 0;
    
    init_GattService(generic_access_service);
    init_GattService(battery_service);
    init_GattService(myo_control_service);
    init_GattService(imu_service);
    init_GattService(arm_service);
    init_GattService(emg_service);
    
    error = NULL;
    loop = g_main_loop_new(NULL, false);
    status = DISCONNECTED;
    
    bluez_manager =
        (GDBusObjectManagerClient*) g_dbus_object_manager_client_new_for_bus_sync(
            G_BUS_TYPE_SYSTEM, G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
            "org.bluez", "/", NULL, NULL, NULL, NULL, &error);
    ASSERT(error, "Get ObjectManager failed");
    
    if(get_adapter()) {
        //error
    }
    
    //scan for Myo
    if(myo_scan()) {
        perror("Error finding Myo!\n");
        
        stop_myo(0);
        
        return 1;
    }
    
    g_main_loop_run(loop);
    
    stop_myo(0);
    
    return 0;
}
