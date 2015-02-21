#include <stdlib.h>
#include <glib.h>
#include <string.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#include <dbus/dbus.h>

static const PAYLOAD_END[] = {0x06, 0x42, 0x48, 0x12, 0x4A, 0x7F,
                                0x2C, 0x48, 0x47, 0xB9, 0xDE, 0x04,
                                0xA9, 0x01, 0x00, 0x06, 0xD5};

DBusConnection* conn;

static new;

static char adapter[] = "/org/bluez/hci0";
static char device[] = "/org/bluez/hci0/dev_00_00_00_00_00_00";

void myo_scan(int devId, bdaddr_t* addr) {
    int dd, err, len;
    
    unsigned char buf[HCI_MAX_EVENT_SIZE];
    struct hci_filter nf, of;
    socklen_t olen;
    unsigned char done;
    
    evt_le_meta_event *meta;
	le_advertising_info *info;
    
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
}

int myo_connect() {
    DBusMessage* msg;
    DBusError err;
    
    msg = dbus_message_new_method_call("org.bluez", device, "org.bluez.Device1", "Connect");
    dbus_error_init(&error);
    dbus_connection_send_with_reply_and_block(conn, msg, -1, &err);
    if(dbus_error_is_set(&error)) {
        //error stuff
        dbus_error_free(&error);
        return 1;
    }
    return 0;
}

void freeReadValue() {
    readValueSize = -1;
    free(readValue);
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

static void events_handler(const uint8_t *pdu, uint16_t len, gpointer user_data) {
	uint8_t *opdu;
	uint16_t handle, i, olen = 0;
	size_t plen;

	handle = get_le16(&pdu[1]);

	switch (pdu[0]) {
	case ATT_OP_HANDLE_NOTIFY:
		g_print("Notification handle = 0x%04x value: ", handle);
		break;
	case ATT_OP_HANDLE_IND:
		g_print("Indication   handle = 0x%04x value: ", handle);
		break;
	default:
		g_print("Invalid opcode\n");
		return;
	}

	for (i = 3; i < len; i++)
		g_print("%02x ", pdu[i]);

	g_print("\n");

	if (pdu[0] == ATT_OP_HANDLE_NOTIFY)
		return;

	opdu = g_attrib_get_buffer(attrib, &plen);
	olen = enc_confirmation(opdu, plen);

	if (olen > 0)
		g_attrib_send(attrib, 0, opdu, olen, NULL, NULL, NULL);
}

static void connect_cb(GIOChannel *io, GError *err, gpointer user_data) {
	uint16_t mtu;
	uint16_t cid;

	if (err) {
		set_state(STATE_DISCONNECTED);
		error("%s\n", err->message);
		return;
	}

	bt_io_get(io, &err, BT_IO_OPT_IMTU, &mtu, BT_IO_OPT_CID, &cid, BT_IO_OPT_INVALID);

	if (err) {
		g_printerr("Can't detect MTU, using default: %s", err->message);
		g_error_free(err);
		mtu = ATT_DEFAULT_LE_MTU;
	}

	if (cid == ATT_CID) {
		mtu = ATT_DEFAULT_LE_MTU;
    }

	attrib = g_attrib_new(iochannel, mtu);
    g_attrib_register(attrib, ATT_OP_HANDLE_NOTIFY, GATTRIB_ALL_HANDLES,
						events_handler, attrib, NULL);
	g_attrib_register(attrib, ATT_OP_HANDLE_IND, GATTRIB_ALL_HANDLES,
						events_handler, attrib, NULL);
    //do somthing to keep track of the state
	//set_state(STATE_CONNECTED);
}

static void disconnect_io() {
	if (conn_state == STATE_DISCONNECTED)
		return;

	g_attrib_unref(attrib);
	attrib = NULL;

	g_io_channel_shutdown(iochannel, FALSE, NULL);
	g_io_channel_unref(iochannel);
	iochannel = NULL;

	set_state(STATE_DISCONNECTED);
}

static gboolean channel_watcher(GIOChannel *chan, GIOCondition cond, gpointer user_data) {
	disconnect_io();
	return FALSE;
}

int main(void) {
    bdaddr_t dst;
    int adapterId, err;
    
    unsigned char writeValue[16];
    
    adapterId = hci_get_route(NULL);
    sprintf(adapter, "/org/bluez/hci%d", adapterId);
    //scan for Myo
    scan_myo(hciNum, &dst);
    if(NULL == dst) {
        perror("Error finding Myo!");
        exit(1);
    }
    
    sprintf(device, "%s/dev_%2.2X_%2.2X_%2.2X_%2.2X_%2.2X_%2.2X", adapter, dst->b[5], dst->b[4], dst->b[3], dst->b[2], dst->b[1], dst->b[0]);
    
    conn = dbus_bus_get(DBUS_BUS_SYSTEM, NULL);
    
    //get channel with gatt_connect
    printf("Connecting...\n");
    err = myo_connect();
    
    if(!err) {
        printf("Connected!\n");
        //set watcher for disconnect
        g_io_add_watch(iochannel, G_IO_HUP, channel_watcher, NULL);
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
    }
    
    dbus_connection_close(conn)
}
