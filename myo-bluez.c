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

static GError* gerr;
static GCancellable* cancel;

static int new;

static GDBusObjectManagerClient* manager;
static GDBusProxy* device;

static int myo_found;

void interface_added_cb(GDBusObjectManager* manager, GDBusObject* object, GDBusInterface* interface,
                    gpointer user_data) {
    GDBusInterfaceInfo* info;
    bdaddr_t* addr;
    GDBusProxy* proxy;
    GVariant* addrVar;
    char* addrStr;
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
                myo_found = TRUE;
            }
        }
        
        g_variant_unref(addrVar);
    }
}

void disconnect_cb(GDBusProxy* proxy, GVariant* changed_properties, GStrv invalidated_properties,
                    gpointer user_data) {
    
}

void myo_scan(int devId, bdaddr_t* addr) {
    int dd, err, len;
    
    unsigned char buf[HCI_MAX_EVENT_SIZE];
    struct hci_filter nf, of;
    socklen_t olen;
    unsigned char done;
    
    evt_le_meta_event *meta;
	le_advertising_info *info;
    
    char adapterPath[16];
    GDBusProxy* adapter;
    GVariant* reply;
    
    addr = NULL;
    
    dd = hci_open_dev(dev_id);
    if (dd < 0) {
		perror("Could not open device");
		exit(1);
	}
    
    err = hci_le_set_scan_parameters(dd, 1, htobs(0x0010), htobs(0x0010),
						LE_PUBLIC_ADDRESS, 0, 10000);
    if (err < 0) {
		perror("Set scan parameters failed");
		exit(1);
	}
    
    err = hci_le_set_scan_enable(dd, 1, 1, 10000);
    if (err < 0) {
		perror("Enable scan failed");
		exit(1);
	}
    
    printf("Scanning...\n");
    
    //save old filter
    olen = sizeof(of);
	if (getsockopt(dd, SOL_HCI, HCI_FILTER, &of, &olen) < 0) {
		printf("Could not get socket options\n");
		return -1;
	}
    
    //make new filter
    hci_filter_clear(&nf);
	hci_filter_set_ptype(HCI_EVENT_PKT, &nf);
	hci_filter_set_event(EVT_LE_META_EVENT, &nf);
    
    //set new filter
    if (setsockopt(dd, SOL_HCI, HCI_FILTER, &nf, sizeof(nf)) < 0) {
		printf("Could not set socket options\n");
		return -1;
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
		exit(1);
	}
    
    hci_close_dev(dd);
    
    //now scan to populate devices
    sprintf(adapterPath, "/org/bluez/hci%d", adapterID);
    adapter = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE, NULL,
            "org.bluez", adapterPath, "org.bluez.Adapter1", cancel, &gerr);
    //TODO: check for error and cancel
    
    g_signal_connect(manager, "interface-added", interface_added_cb, addr);
    
    reply = g_dbus_proxy_call_sync(adapter, "StartDiscovery", NULL, G_DBUS_CALL_FLAGS_NONE, -1, cancel, &gerr);
    //TODO: check for error and cancel
    g_variant_unref(reply);
    
    myo_found = FALSE;
    while(!myo_found) {
        reply = g_dbus_proxy_call_sync(adapter, "StopDiscovery", NULL, G_DBUS_CALL_FLAGS_NONE, -1, cancel, &gerr);
        //TODO: check for error and cancel
        g_variant_unref(reply);
    }
    
    g_object_unref(adapter);
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
    gatt_read_char(attrib, handle, char_read_cb, attrib);
    
    while(!callback) {
        //sleep
    }
}

void write(int handle, unsigned char* data, int dataLen) {
    gatt_write_cmd(attrib, handle, data, dataLen, NULL, NULL);
}

static gboolean channel_watcher(GIOChannel *chan, GIOCondition cond, gpointer user_data) {
	disconnect_io();
	return FALSE;
}

int main(void) {
    bdaddr_t dst;
    int adapterId;
    char devicePath[38];
    GVariant* reply;
    
    unsigned char writeValue[16];
    
    gerr = g_error_new();
    cancel = g_cancellable_new();
    
    manager = (GDBusObjectManagerClient*) g_dbus_object_manager_client_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
            G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE, "org.bluez", "/", NULL, NULL, cancel, &gerr);
    //TODO: check for error and cancel
    
    adapterId = hci_get_route(NULL);
    
    //scan for Myo
    scan_myo(adapterId, &dst);
    if(NULL == dst) {
        perror("Error finding Myo!");
        exit(1);
    }
    
    sprintf(devicePath, "/org/bluez/hci%d/dev_%2.2X_%2.2X_%2.2X_%2.2X_%2.2X_%2.2X", adapterPath, dst->b[5],
            dst->b[4], dst->b[3], dst->b[2], dst->b[1], dst->b[0]);
    
    device = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE, NULL,
            "org.bluez", devicePath, "org.bluez.Device1", cancel, &gerr);
    //TODO: check for error and cancel
    
    printf("Connecting...\n");
    reply = g_dbus_proxy_call_sync(device, "Connect", NULL, G_DBUS_CALL_FLAGS_NONE, -1, cancel, &gerr);
    //TODO: check for error and cancel
    g_variant_unref(reply);
    
    printf("Connected!\n");
    //set watcher for disconnect
    g_signal_connect(device, "interface-added", disconnect_cb, NULL);
    
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
        self.start_raw()
    }
    
    g_object_unref(device);
    
    //TODO: main loop stuff
}
