#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>

#include <glib.h>
#include <gio/gio.h>

#include <bluetooth/bluetooth.h>

#define ASSERT(GERR, MSG) \
    if(GERR != NULL) { \
        g_assert(GERR); \
        fprintf (stderr, "%s: %s\n",MSG ,GERR->message); \
        g_error_free(GERR); \
    }
    
#define GAP_UUID "00001800-0000-1000-8000-00805f9b34fb"
#define BATT_UUID "0000180f-0000-1000-8000-00805f9b34fb"
#define MYO_UUID "d5060001-a904-deb9-4748-2c7f4a124842"
#define IMU_UUID "d5060002-a904-deb9-4748-2c7f4a124842"
#define ARM_UUID "d5060003-a904-deb9-4748-2c7f4a124842"
#define EMG_UUID "d5060004-a904-deb9-4748-2c7f4a124842"

enum MyoStatus {
    DISCONNECTED,
    CONNECTED
} status;
    
static GError *error;
static GMainLoop *loop;

static int new;
static gulong myo_conn_id;
static gulong service_conn_id;

static GDBusObjectManagerClient *bluez_manager;
static GDBusProxy *adapter;
static GDBusProxy *myo;

typedef GattService service {
    char UUID[37];
    GDBusProxy *proxy;
    char **char_UUIDs;
    GDBusProxy **char_proxies;
    int num_chars;
};

static GDBusObjectManagerClient *gatt_manager;
static GattService generic_access_service;
static GattService battery_service;
static GattService myo_control_service;
static GattService IMU_service;
static GattService arm_service;
static GattService emg_service;

int myo_connect();
void myo_initialize();

void init_GattService(GattService *service, char *UUID, char **char_UUIDS, int num_chars) {
    int i;
    
    strcpy(service->UUID, UUID);
    service->char_UUIDs = malloc((num_chars + 1) * sizeof(char*));
    for(i = 0; i < num_chars; i++) {
        service->char_UUIDs[i] = malloc(37 * sizeof(char));
        strcpy(service->char_UUIDs[i], char_UUIDS[i]);
    }
    service->char_UUIDs[num_chars] = NULL;
}

gint is_adapter(gconstpointer a, gconstpointer b) {
    GDBusInterface *interface;
    
    interface = g_dbus_object_get_interface((GDBusObject*) a, "org.bluez.Adapter1");
    if(NULL == interface) {
        return 1;
    } else {
        g_object_unref(interface);
        return 0;
    }
}

gint is_myo(gconstpointer a, gconstpointer b) {
    GDBusProxy *proxy;
    GVariant *UUIDs_var;
    GVariantIter *iter;
    const gchar *uuid;
    
    gint ret;
    
    ret = 1;
    
    proxy = (GDBusProxy*) g_dbus_object_get_interface((GDBusObject*) a, "org.bluez.Device1");
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

gint is_service(gconstpointer a, gconstpointer b) {
    GDBusInterface *interface;
    
    interface = g_dbus_object_get_interface((GDBusObject*) a, "org.bluez.GattService1");
    if(NULL == interface) {
        return 1;
    } else {
        g_object_unref(interface);
        return 0;
    }
}

void set_service(GDBusObject *object) {
    GVariant *UUID;
    const char *UUID_str;
    
    //check UUIDs for which service
    //get UUID
    UUID = g_dbus_proxy_get_cached_property((GDBusProxy*) object, "UUID");
    if(UUID == NULL) {
        return;
    }
    UUID_str = g_variant_get_string(UUID);
    
    if(strcmp(UUID_str, GAP_UUID) == 0) {
        generic_access_service->service = g_dbus_proxy_new_for_bus_sync(
                            G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE, NULL,
                            "org.bluez", g_dbus_object_get_object_path(object),
                            "org.bluez.GATTService1", NULL, &error);
        ASSERT(error, "Get generic_access_service failed");
    } else if(strcmp(UUID_str, BATT_UUID)) {
        battery_service->service = g_dbus_proxy_new_for_bus_sync(
                            G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE, NULL,
                            "org.bluez", g_dbus_object_get_object_path(object),
                            "org.bluez.GATTService1", NULL, &error);
        ASSERT(error, "Get generic_access_service failed");
    } else if(strcmp(UUID_str, MYO_UUID)) {
        myo_control_service->service = g_dbus_proxy_new_for_bus_sync(
                            G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE, NULL,
                            "org.bluez", g_dbus_object_get_object_path(object),
                            "org.bluez.GATTService1", NULL, &error);
        ASSERT(error, "Get generic_access_service failed");
    } else if(strcmp(UUID_str, IMU_UUID)) {
        IMU_service->service = g_dbus_proxy_new_for_bus_sync(
                            G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE, NULL,
                            "org.bluez", g_dbus_object_get_object_path(object),
                            "org.bluez.GATTService1", NULL, &error);
        ASSERT(error, "Get generic_access_service failed");
    } else if(strcmp(UUID_str, ARM_UUID)) {
        arm_service->service = g_dbus_proxy_new_for_bus_sync(
                            G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE, NULL,
                            "org.bluez", g_dbus_object_get_object_path(object),
                            "org.bluez.GATTService1", NULL, &error);
        ASSERT(error, "Get generic_access_service failed");
    } else if(strcmp(UUID_str, EMG_UUID)) {
        emg_service->service = g_dbus_proxy_new_for_bus_sync(
                            G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE, NULL,
                            "org.bluez", g_dbus_object_get_object_path(object),
                            "org.bluez.GATTService1", NULL, &error);
        ASSERT(error, "Get generic_access_service failed");
    }
    
    g_variant_unref(UUID);
}

void disconnect_cb(GDBusProxy *proxy, GVariant *changed, GStrv invalid,
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
                    myo_connect();
                }
            }
        }
        g_variant_iter_free (iter);
    }
}

void myo_data_cb() {
    //TODO: data callback
}

void interface_added_myo(GDBusObjectManager *manager, GDBusObject *object,
                            GDBusInterface *interface, gpointer user_data) {
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

void interface_added_service(GDBusObjectManager *manager, GDBusObject *object,
                            GDBusInterface *interface, gpointer user_data) {
    if(is_service(object, NULL)) {
        set_service(object);
    }
}

int get_adapter() {
    GList *objects;
    GList *object;
    
    objects = g_dbus_object_manager_get_objects((GDBusObjectManager*) bluez_manager);
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

int myo_scan() {
    GList *objects;
    GList *object;
    
    GVariant *reply;
    
    //get adapter
    objects = g_dbus_object_manager_get_objects((GDBusObjectManager*) bluez_manager);
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
            //TODO: might be a problem if there are multiple Myos and this one cannot be connected to
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

int myo_connect(GDBusObject *object) {
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
    g_signal_connect(myo, "g-properties-changed", G_CALLBACK(disconnect_cb), NULL);
    
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
    objects = g_dbus_object_manager_get_objects((GDBusObjectManager*) bluez_manager);
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

int myo_get_name(char *str) {
    //get name of myo from generic access and return length
    //TODO: get name
}

void myo_get_version(char *ver_arr) {
    //get version from myo control service
    //TODO: get version
}

void myo_EMG_notify_enable(bool enable){
    GVariant *chars;
    const char **char_paths;
    GDBusProxy _char;
    
    //get emg data characteristic from service
    chars = g_dbus_proxy_get_cached_property(emg_service, "Characteristics");
    char_paths = g_variant_get_objv(chars, NULL);
    if(char_paths[0] == NULL) {
        //error
    }
    _char = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
                G_DBUS_PROXY_FLAGS_NONE, NULL, "org.bluez", char_paths[0],
                "org.bluez.GattCharacteristic1", NULL, &error);
    ASSERT(error, "Failed to get EMG characteristic");
    //call start notify or stop notify
    g_dbus_proxy_call_sync(_char, enable ? "StartNotify" : "StopNotify", NULL,
                                    G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
    ASSERT(error, "EMG notify enable/disable failed");
}

void myo_IMU_notify_enable(bool enable){
    GVariant *chars, *UUID;
    const char **char_paths;
    GDBusProxy _char;
    const char *UUID_str;
    
    //get IMU data characteristic from service
    chars = g_dbus_proxy_get_cached_property(emg_service, "Characteristics");
    char_paths = g_variant_get_objv(chars, NULL);
    if(char_paths[0] == NULL) {
        //error
    }
    _char = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
                    G_DBUS_PROXY_FLAGS_NONE, NULL, "org.bluez", char_paths[0],
                    "org.bluez.GattCharacteristic1", NULL, &error);
    ASSERT(error, "Failed to get IMU characteristic");
    //check UUID because the IMU service has two chars
    UUID = g_dbus_proxy_get_cached_property(_char, "UUID");
    if(UUID == NULL) {
        return;
    }
    UUID_str = g_variant_get_string(UUID);
    if(strcmp(UUID_str, "d5060402-a904-deb9-4748-2c7f4a124842") != 0) {
        g_object_unref(_char);
        _char = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
                    G_DBUS_PROXY_FLAGS_NONE, NULL, "org.bluez", char_paths[1],
                    "org.bluez.GattCharacteristic1", NULL, &error);
        ASSERT(error, "Failed to get other IMU characteristic");
    }
    //call start notify or stop notify
    g_dbus_proxy_call_sync(_char, enable ? "StartNotify" : "StopNotify", NULL,
                                    G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
    ASSERT(error, "IMU notify enable/disable failed");
}

void myo_arm_indicate_enable(bool enable) {
    GVariant *chars;
    const char **char_paths;
    GDBusProxy _char;
    
    //get arm data characteristic from service
    chars = g_dbus_proxy_get_cached_property(emg_service, "Characteristics");
    char_paths = g_variant_get_objv(chars, NULL);
    if(char_paths[0] == NULL) {
        //error
    }
    _char = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
                G_DBUS_PROXY_FLAGS_NONE, NULL, "org.bluez", char_paths[0],
                "org.bluez.GattCharacteristic1", NULL, &error);
    ASSERT(error, "Failed to get arm characteristic");
    //call start notify or stop notify
    g_dbus_proxy_call_sync(_char, enable ? "StartNotify" : "StopNotify", NULL,
                                    G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
    ASSERT(error, "Arm notify enable/disable failed");
}

void myo_update_enable(bool emg, bool imu, bool arm) {
    //TODO: update enable
}

void myo_initialize() {
    unsigned char version[4];
    char name[25];
    
    unsigned short C;
    unsigned char emg_hz, emg_smooth, imu_hz;
    
    printf("Initializing...\n");
    
    //read firmware version
    myo_get_version(version);
    printf("firmware version: %d.%d.%d.%d\n", version[0], version[1],
                                                        version[2], version[3]);
    
    new = version[0];
    //if old do the thing
    //else do the other thing (configure)
    if(!new) {
        //don't know what these do; Myo Connect sends them, though we get data fine without them
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
        myo_EMG_notify_enable(TRUE);
        //enable IMU data
        myo_IMU_notify_enable(TRUE);

        //Sampling rate of the underlying EMG sensor, capped to 1000. If it's less than 1000, emg_hz is correct. If it is greater, the actual framerate starts dropping inversely. Also, if this is much less than 1000, EMG data becomes slower to respond to changes. In conclusion, 1000 is probably a good value.
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

void stop_myo(int sig) {
    GVariant *reply;
    
    //unref stuff
    if(myo != NULL) {
        if(status == CONNECTED) {
            //disconnect
            reply = g_dbus_proxy_call_sync(myo, "Disconnect", NULL, G_DBUS_CALL_FLAGS_NONE,
                        -1, NULL, &error);
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
