int main(void) {
    
    //scan for Myo
    //get channel with gatt_connect
    gatt_connect(NULL, dst, BDADDR_LE_PUBLIC, BT_IO_SEC_LOW, 0, mtu, BtIOConnect, gerr);
    //if channel != NULL
    //set watcher for disconnect
    //read firmware version
    //print firmware version
    //if old do the thing
    //else do the other thing (configure)
}
