#include <stdlib.h>
#include <glib.h>
#include <string.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#include <lib/uuid.h>
#include <btio/btio.h>
#include <att.h>
#include <gattrib.h>
#include <gatt.h>

static const PAYLOAD_END[] = {0x06, 0x42, 0x48, 0x12, 0x4A, 0x7F,
                                0x2C, 0x48, 0x47, 0xB9, 0xDE, 0x04,
                                0xA9, 0x01, 0x00, 0x06, 0xD5};

static GIOChannel *iochannel = NULL;
static GAttrib *attrib = NULL;
static GMainLoop *event_loop;

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

GIOChannel* myo_connect(int src, bdaddr_t* dst,
                         char dst_type, BtIOSecLevel sec_level,
                         int psm, int mtu, BtIOConnect connect_cb,
                         GError** gerr) {
    GIOChannel* chan;
    //from bluetooth.h
    bdaddr_t sba;
    uint8_t dest_type;
    //from glib.h
    GError* tmp_err = NULL;
    //from btio.h
    BtIOSecLevel sec;

    //bacpy(&dst, &dba);

    /* Local adapter */
    hci_devba(src, &sba);
    //bacpy(&sba, BDADDR_ANY);

    /* Not used for BR/EDR */
    if(dst_type == BDADDR_LE_RANDOM) {
        dest_type = BDADDR_LE_RANDOM;
    } else {
        dest_type = BDADDR_LE_PUBLIC;
    }

    //change to pass enum?
    switch(sec_level) {
        case: BT_IO_SEC_MEDIUM
            sec = BT_IO_SEC_MEDIUM;
            break;
        case: BT_IO_SEC_HIGH
            sec = BT_IO_SEC_HIGH;
            break;
        default:
            sec = BT_IO_SEC_LOW;
            break;
    }

    if(psm == 0) {
        //from btio.h
        //public adapter
        chan = bt_io_connect(connect_cb, NULL, NULL, &tmp_err,
                             BT_IO_OPT_SOURCE_BDADDR, &sba,
                             BT_IO_OPT_SOURCE_TYPE, BDADDR_LE_PUBLIC,
                             BT_IO_OPT_DEST_BDADDR, dst,
                             BT_IO_OPT_DEST_TYPE, dest_type,
                             BT_IO_OPT_CID, ATT_CID,
                             BT_IO_OPT_SEC_LEVEL, sec,
                             BT_IO_OPT_INVALID);
    } else {
        //random adapter
        chan = bt_io_connect(connect_cb, NULL, NULL, &tmp_err,
                             BT_IO_OPT_SOURCE_BDADDR, &sba,
                             BT_IO_OPT_DEST_BDADDR, dst,
                             BT_IO_OPT_PSM, psm,
                             BT_IO_OPT_IMTU, mtu,
                             BT_IO_OPT_SEC_LEVEL, sec,
                             BT_IO_OPT_INVALID);
    }

    if(tmp_err) {
        g_propagate_error(gerr, tmp_err);
        return NULL;
    }

    return chan;
}

char read(int handle) {
    gatt_read_char(attrib, handle, char_read_cb, attrib);
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
    GError* gerr = NULL;
    bdaddr_t dst;
    int hciNum;
    
    hciNum = hci_get_route(NULL);
    //scan for Myo
    scan_myo(hciNum, &dst);
    if(NULL == dst) {
        perror("Error finding Myo!");
        exit(1);
    }
    //get channel with gatt_connect
    printf("Connecting...\n");
    iochannel = myo_connect(hciNum, dst, BDADDR_LE_PUBLIC, BT_IO_SEC_LOW, 0, 0, connect_cb, &gerr);
    
    //if channel != NULL
    if(NULL != iochannel) {
        printf("Connected!\n");
        //set watcher for disconnect
        g_io_add_watch(iochannel, G_IO_HUP, channel_watcher, NULL);
        //read firmware version
        read(0x17)
        //print firmware version
        
        //if old do the thing
        
        //else do the other thing (configure)
        
    } else {
        //error stuff
        //set_state(STATE_DISCONNECTED);
		error("%s\n", gerr->message);
		g_error_free(gerr);
    }
}
