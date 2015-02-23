#include <stdlib.h>
#include <glib.h>
#include <gio/gio.h>
#include <string.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

static const PAYLOAD_END[] = {0x06, 0x42, 0x48, 0x12, 0x4A, 0x7F,
                                0x2C, 0x48, 0x47, 0xB9, 0xDE, 0x04,
                                0xA9, 0x01, 0x00, 0x06, 0xD5};

static GError *error;
static GMainLoop *loop;

static int new;

static GDBusProxy *adapter;
static GDBusProxy *device;

void interface_added_cb(GDBusObjectManager *manager, GDBusObject *object, GDBusInterface *interface,
                    gpointer user_data) {
    GDBusInterfaceInfo *info;
    bdaddr_t *addr;
    GDBusProxy *proxy;
    GVariant *addrVar, *reply;
    char *addrStr;
    bdaddr_t cmp;
    
    info = g_dbus_interface_get_info(interface);
    if(strcmp(info->name, "org.bluez.Device1")) {
        addr = (bdaddr_t*) user_data;
        proxy = (GDBusProxy*) interface;
        addrVar = g_dbus_proxy_get_cached_property(proxy, "Address");
        if(addrVar != NULL) {
            addrStr = g_variant_get_string(addrVar, NULL);
            str2ba(addrStr, &cmp);
            if(memcmp(addr, &cmp, sizeof(bdaddr_t)) == 0) {
                reply = g_dbus_proxy_call_sync(adapter, "StopDiscovery", NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
                //TODO: check for error
                g_variant_unref(reply);
                
                //get path from proxy
                
                device = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE, NULL,
                        "org.bluez", g_dbus_proxy_get_object_path(proxy), "org.bluez.Device1", NULL, &error);
                //TODO: check for error
                
                //set watcher for disconnect
                g_signal_connect(device, "g-properties-changed", disconnect_cb, NULL);
                
                myo_connect();
            }
        }
        
        g_variant_unref(addrVar);
    }
}

void myo_scan(int adapter_id, bdaddr_t *addr) {
    int dd, err, len;
    
    unsigned char buf[HCI_MAX_EVENT_SIZE];
    struct hci_filter nf, of;
    socklen_t olen;
    unsigned char done;
    
    evt_le_meta_event *meta;
	le_advertising_info *info;
    
    char adapterPath[16];
    GVariant *reply;
    
    addr = NULL;
    
    dd = hci_open_dev(adapter_id);
    if (dd < 0) {
		perror("Could not open device");
		return;
	}
    
    err = hci_le_set_scan_parameters(dd, 1, htobs(0x0010), htobs(0x0010),
						LE_PUBLIC_ADDRESS, 0, 10000);
    if (err < 0) {
		perror("Set scan parameters failed");
		return;
	}
    
    err = hci_le_set_scan_enable(dd, 1, 1, 10000);
    if (err < 0) {
		perror("Enable scan failed");
		return;
	}
    
    printf("Scanning...\n");
    
    //save old filter
    olen = sizeof(of);
	if (getsockopt(dd, SOL_HCI, HCI_FILTER, &of, &olen) < 0) {
		printf("Could not get socket options\n");
        
        //TODO: turn off scanning
		return;
	}
    
    //make new filter
    hci_filter_clear(&nf);
	hci_filter_set_ptype(HCI_EVENT_PKT, &nf);
	hci_filter_set_event(EVT_LE_META_EVENT, &nf);
    
    //set new filter
    if (setsockopt(dd, SOL_HCI, HCI_FILTER, &nf, sizeof(nf)) < 0) {
		printf("Could not set socket options\n");
        
        //TODO: turn off scanning
		return;
	}
    
    done = FALSE;
    err = FALSE;
    len = 0;
    while(!done && !err) {
        while ((len = read(dd, buf, sizeof(buf))) < 0) {
            if (errno == EINTR) {
                done = TRUE;
                break;
            }

            if(errno == EAGAIN || errno == EINTR) {
                continue;
            }
            
            err = TRUE;
        }
        
        if(!done && !err) {
            meta = (void *) (buf + (1 + HCI_EVENT_HDR_SIZE));
            len -= (1 + HCI_EVENT_HDR_SIZE);

            if (meta->subevent != EVT_LE_ADVERTISING_REPORT) {
                continue;
            }

            info = (le_advertising_info*) (meta->data + 1);
            if(info->length == 0) {
                continue;
            }
            
            //TODO: Print data
            
            //check
            if(memcmp(&(info->data) + (info->length - 18), PAYLOAD_END, 17) == 0) {
                printf("Found a Myo!\n");
                memcpy(addr, &info->bdaddr, sizeof(info->bdaddr));
            }
        }
    }
    
    //set old filter
    setsockopt(dd, SOL_HCI, HCI_FILTER, &of, sizeof(of));
    
    //disable with enable
    err = hci_le_set_scan_enable(dd, 0, 1, 10000);
	if (err < 0) {
		perror("Disable scan failed");
	}
    
    hci_close_dev(dd);
    
    //now scan to populate devices
    sprintf(adapterPath, "/org/bluez/hci%d", adapter_id);
    adapter = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE, NULL,
            "org.bluez", adapterPath, "org.bluez.Adapter1", NULL, &error);
    //TODO: check for error
    
    g_signal_connect(manager, "interface-added", interface_added_cb, addr);
    
    reply = g_dbus_proxy_call_sync(adapter, "StartDiscovery", NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
    //TODO: check for error
    g_variant_unref(reply);
}

void disconnect_cb(GDBusProxy *proxy, GVariant *changed, GStrv invalid,
                    gpointer user_data) {
    GVariantIter *iter;
    const gchar *key;
    GVariant *value;
    
    //check if peroperty was "connected"
    if(g_variant_n_children(changed) > 0) {
        g_variant_get(changed_properties, "a{sv}", &iter);
        while(g_variant_iter_loop(iter, "{&sv}", &key, &value)) {
            if(strcmp(key, "Connected") == 0) {
                if(!g_variant_get_boolean(value)) {
                    //disconnected
                    //TODO: check for success
                    myo_connect();
                }
            }
        }
        g_variant_iter_free (iter);
    }
}

void myo_connect() {
    GVariant *reply;
    
    printf("Connecting...\n");
    reply = g_dbus_proxy_call_sync(device, "Connect", NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
    //TODO: check for error
    g_variant_unref(reply);
    
    printf("Connected!\n");
    
    myo_initialize();
}

static void char_read_cb(guint8 status, const guint8 *pdu, guint16 plen, gpointer user_data) {
	if (status != 0) {
		perror("Characteristic value/descriptor read failed: %s\n", att_ecode2str(status));
		return;
	}

	readValueSize = dec_read_resp(pdu, plen, readValue, sizeof(readValue));
	if (readValueSize < 0) {
		perror("Protocol error\n");
	}
}

void read(int handle) {
    
}

void write(int handle, unsigned char *data, int dataLen) {
    
}

void myo_initialize() {
    unsigned char writeValue[16];
    
    //read firmware version
    read(0x17)
    printf("firmware version: %d.%d.%d.%d\n", readValue[4], readValue[5], readValue[6], readValue[7]);
    
    new = readValue[4];
    freeReadArray();
    //if old do the thing
    //else do the other thing (configure)
    if(!new) {
        //don't know what these do; Myo Connect sends them, though we get data fine without them
        writeValue[0] = 0x01;
        writeValue[1] = 0x02;
        writeValue[2] = 0x00;
        writeValue[3] = 0x00;
        write(0x19, writeValue, 4);
        
        writeValue[1] = 0x00;
        write(0x2f, writeValue, 2);
        write(0x2c, writeValue, 2);
        write(0x32, writeValue, 2);
        write(0x35, writeValue, 2);

        //enable EMG data
        write(0x28, writeValue, 2);
        //enable IMU data
        write(0x1d, writeValue, 2);

        //Sampling rate of the underlying EMG sensor, capped to 1000. If it's less than 1000, emg_hz is correct. If it is greater, the actual framerate starts dropping inversely. Also, if this is much less than 1000, EMG data becomes slower to respond to changes. In conclusion, 1000 is probably a good value.
        C = 1000
        emg_hz = 50
        //strength of low-pass filtering of EMG data
        emg_smooth = 100

        imu_hz = 50

        //send sensor parameters, or we don't get any data
        writeValue[0] = 2;
        writeValue[1] = 9;
        writeValue[2] = 2;
        writeValue[3] = 1;
        *((short*) &(writeValue + 4)) = C;
        writeValue[6] = emg_smooth;
        writeValue[7] = C/emg_hz;
        writeValue[8] = imu_hz;
        writeValue[9] = 0;
        writeValue[10] = 0;
        write(0x19, writeValue, 11);
    } else {
        read(0x03);
        printf("device name: %s", readValue);
        freeReadValue();

        //enable IMU data
        writeValue[0] = 0x01;
        writeValue[1] = 0x00;
        write(0x1d, writeValue, 2)
        //enable on/off arm notifications
        writeValue[0] = 0x02;
        write(0x24, writeValue, 2)

        //self.write_attr(0x19, b'\x01\x03\x00\x01\x01')
        start_raw()
    }
}

int main(void) {
    bdaddr_t dst;
    int adapter_id;
    
    GDBusObjectManagerClient *manager;
    GVariant *reply;
    
    error = g_error_new();
    loop = g_main_loop_new(NULL, FALSE);
    
    manager = (GDBusObjectManagerClient*) g_dbus_object_manager_client_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
            G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE, "org.bluez", "/", NULL, NULL, NULL, &error);
    //TODO: check for error
    
    adapter_id = hci_get_route(NULL);
    
    //scan for Myo
    scan_myo(adapter_id, &dst);
    if(NULL == dst) {
        perror("Error finding Myo!");
        if(NULL != adapter) {
            g_object_unref(adapter);
        }
        g_object_unref(manager);
        return 1;
    }
    
    g_main_loop_run(loop);
    
    g_object_unref(adapter);
    g_object_unref(device);
    g_main_loop_unref(loop);
    return 0;
}
