#include <stdlib.h>
#include <glib.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#include <lib/uuid.h>
#include <btio/btio.h>
#include <att.h>
#include <gattrib.h>
#include <gatt.h>
#include "gatttool.h"

GIOChannel* gatt_connect(const char* src, const char* dst,
                         const char* dst_type, const char* sec_level,
                         int psm, int mtu, BtIOConnect connect_cb,
                         GError** gerr) {
    GIOChannel* chan;
    //from bluetooth.h
    bdaddr_t sba, dba;
    uint8_t dest_type;
    //from glib.h
    GError* tmp_err = NULL;
    //from btio.h
    BtIOSecLevel sec;

    //mac addr to array
    str2ba(dst, &dba);

    /* Local adapter */
    if(src != NULL) {
        if(!strncmp(src, "hci", 3)) {
            hci_devba(atoi(src + 3), &sba);
        } else {
            str2ba(src, &sba);
        }
    } else {
        bacpy(&sba, BDADDR_ANY);
    }

    //change to pass enum?
    /* Not used for BR/EDR */
    if(strcmp(dst_type, "random") == 0) {
        dest_type = BDADDR_LE_RANDOM;
    } else {
        dest_type = BDADDR_LE_PUBLIC;
    }

    //change to pass enum?
    if(strcmp(sec_level, "medium") == 0) {
        sec = BT_IO_SEC_MEDIUM;
    } else if(strcmp(sec_level, "high") == 0) {
        sec = BT_IO_SEC_HIGH;
    } else {
        sec = BT_IO_SEC_LOW;
    }

    if(psm == 0) {
        //from btio.h
        //public adapter
        chan = bt_io_connect(connect_cb, NULL, NULL, &tmp_err,
                             BT_IO_OPT_SOURCE_BDADDR, &sba,
                             BT_IO_OPT_SOURCE_TYPE, BDADDR_LE_PUBLIC,
                             BT_IO_OPT_DEST_BDADDR, &dba,
                             BT_IO_OPT_DEST_TYPE, dest_type,
                             BT_IO_OPT_CID, ATT_CID,
                             BT_IO_OPT_SEC_LEVEL, sec,
                             BT_IO_OPT_INVALID);
    } else {
        //random adapter
        chan = bt_io_connect(connect_cb, NULL, NULL, &tmp_err,
                             BT_IO_OPT_SOURCE_BDADDR, &sba,
                             BT_IO_OPT_DEST_BDADDR, &dba,
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
