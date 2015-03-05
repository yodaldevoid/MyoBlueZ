#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <glib.h>
#include <gio/gio.h>

#include <bluetooth/bluetooth.h>

#define ASSERT(GERR, MSG) \
    if(GERR != NULL) { \
        g_assert(GERR); \
        fprintf (stderr, "%s: %s\n",MSG ,GERR->message); \
        g_error_free(GERR); \
    }
    
static GError *error;
static GMainLoop *loop;

static int new;

static GDBusObjectManagerClient *bluez_manager;
static GDBusProxy *adapter;
static GDBusProxy *myo;

void myo_connect();
void myo_initialize();

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
                    //TODO: check for success
                    printf("Myo disconnected\n");
                    myo_connect();
                }
            }
        }
        g_variant_iter_free (iter);
    }
}

void interface_added_cb(GDBusObjectManager *manager, GDBusObject *object, GDBusInterface *interface,
                    gpointer user_data) {
    GDBusProxy *proxy;
    GVariant *reply;
    
    if(is_myo(object, NULL)) {
            proxy = (GDBusProxy*) interface;
            
            reply = g_dbus_proxy_call_sync(adapter, "StopDiscovery",
                        NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
            ASSERT(error, "StopDiscovery failed");
            g_variant_unref(reply);
            
            printf("Myo found!\n");
            
            //get path from proxy
            myo = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
                        G_DBUS_PROXY_FLAGS_NONE, NULL, "org.bluez",
                        g_dbus_proxy_get_object_path(proxy),
                        "org.bluez.Device1", NULL, &error);
            ASSERT(error, "Get Myo Proxy failed");
            
            //set watcher for disconnect
            g_signal_connect(myo, "g-properties-changed",
                G_CALLBACK(disconnect_cb), NULL);
            
            myo_connect();
    }
    
    //TODO: GattService registration
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
                if(strcmp(uuid, "d5060001-a904-deb9-4748-2c7f4a124842") == 0) {
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

int myo_scan() {
    GVariant *reply;
    GList *objects;
    GList *object;
    
    //get adapter
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
    
    //check to make sure Myo has not already been found
    object = g_list_find_custom(objects, NULL, is_myo);
    if(NULL != object) {
        //Myo has been found so we don't have to scan for it
        //TODO: might be a problem if there are multiple Myos and this one cannot be connected to
        printf("Myo found!\n");
        
        //get path from proxy
        myo = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
                    G_DBUS_PROXY_FLAGS_NONE, NULL, "org.bluez",
                    g_dbus_object_get_object_path((GDBusObject*) object->data),
                    "org.bluez.Device1", NULL, &error);
        ASSERT(error, "Get Myo Proxy failed");
        
        //set watcher for disconnect
        g_signal_connect(myo, "g-properties-changed",
            G_CALLBACK(disconnect_cb), NULL);
        
        //TODO: maybe move to callback somewhere?
        myo_connect();
    } else {
        //scan to find Myo devices
        g_signal_connect(bluez_manager, "interface-added",
            G_CALLBACK(interface_added_cb), NULL);
        
        printf("Discovery started\n");
        
        reply = g_dbus_proxy_call_sync(adapter, "StartDiscovery", NULL,
            G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
        ASSERT(error, "StartDiscovery failed");
        g_variant_unref(reply);
    }
    
    g_list_free_full(objects, g_object_unref);
    
    return 0;
}

void myo_connect() {
    GVariant *reply;
    
    printf("Connecting...\n");
    reply = g_dbus_proxy_call_sync(myo, "Connect", NULL, G_DBUS_CALL_FLAGS_NONE,
                -1, NULL, &error);
    ASSERT(error, "Connect failed");
    g_variant_unref(reply);
    
    printf("Connected!\n");
    
    myo_initialize();
}

void read_char(int handle) {
    
}

void write_char(int handle, unsigned char *data, int dataLen) {
    
}

void myo_initialize() {
    unsigned char writeValue[16];
    
    unsigned short C;
    unsigned char emg_hz, emg_smooth, imu_hz;
    
    printf("Initializing...\n");
    
    //read firmware version
    read_char(0x17);
    //printf("firmware version: %d.%d.%d.%d\n", readValue[4], readValue[5], readValue[6], readValue[7]);
    
    //new = readValue[4];
    //if old do the thing
    //else do the other thing (configure)
    if(!new) {
        //don't know what these do; Myo Connect sends them, though we get data fine without them
        writeValue[0] = 0x01;
        writeValue[1] = 0x02;
        writeValue[2] = 0x00;
        writeValue[3] = 0x00;
        write_char(0x19, writeValue, 4);
        
        writeValue[1] = 0x00;
        write_char(0x2f, writeValue, 2);
        write_char(0x2c, writeValue, 2);
        write_char(0x32, writeValue, 2);
        write_char(0x35, writeValue, 2);

        //enable EMG data
        write_char(0x28, writeValue, 2);
        //enable IMU data
        write_char(0x1d, writeValue, 2);

        //Sampling rate of the underlying EMG sensor, capped to 1000. If it's less than 1000, emg_hz is correct. If it is greater, the actual framerate starts dropping inversely. Also, if this is much less than 1000, EMG data becomes slower to respond to changes. In conclusion, 1000 is probably a good value.
        C = 1000;
        emg_hz = 50;
        //strength of low-pass filtering of EMG data
        emg_smooth = 100;

        imu_hz = 50;

        //send sensor parameters, or we don't get any data
        writeValue[0] = 2;
        writeValue[1] = 9;
        writeValue[2] = 2;
        writeValue[3] = 1;
        *((short*) (writeValue + 4)) = C;
        writeValue[6] = emg_smooth;
        writeValue[7] = C/emg_hz;
        writeValue[8] = imu_hz;
        writeValue[9] = 0;
        writeValue[10] = 0;
        write_char(0x19, writeValue, 11);
    } else {
        read_char(0x03);
        //printf("device name: %s", readValue);

        //enable IMU data
        writeValue[0] = 0x01;
        writeValue[1] = 0x00;
        write_char(0x1d, writeValue, 2);
        //enable on/off arm notifications
        writeValue[0] = 0x02;
        write_char(0x24, writeValue, 2);

        //write_char(0x19, b'\x01\x03\x00\x01\x01'); //normal
        //start_raw();
    }
    
    printf("Initialized!");
}

int main(void) {
    error = NULL;
    loop = g_main_loop_new(NULL, FALSE);
    
    bluez_manager =
        (GDBusObjectManagerClient*) g_dbus_object_manager_client_new_for_bus_sync(
            G_BUS_TYPE_SYSTEM, G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
            "org.bluez", "/", NULL, NULL, NULL, NULL, &error);
    ASSERT(error, "Get ObjectManager failed");
    
    //scan for Myo
    if(myo_scan()) {
        perror("Error finding Myo!\n");
        if(NULL != adapter) {
            g_object_unref(adapter);
        }
        g_object_unref(bluez_manager);
        return 1;
    }
    
    g_main_loop_run(loop);
    
    g_object_unref(bluez_manager);
    g_object_unref(adapter);
    g_object_unref(myo);
    g_main_loop_unref(loop);
    return 0;
}
