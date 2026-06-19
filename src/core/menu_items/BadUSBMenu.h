/*
 * BadUSBMenu — top-level main-menu feature for BadUSB (USB HID payload injection).
 * Promoted out of Others -> "USB HID Tools" so BadUSB stands alone as a first-class
 * feature (matches the device's marketing). Selecting it launches the ducky payload
 * runner directly (script picker -> run). See ducky_setup() in ducky_typer.cpp.
 */
#ifndef __BADUSB_MENU_H__
#define __BADUSB_MENU_H__

#include <MenuItemInterface.h>

class BadUSBMenu : public MenuItemInterface {
public:
    BadUSBMenu() : MenuItemInterface("BadUSB") {}

    void optionsMenu(void);
    void drawIcon(float scale);
    bool hasTheme() { return false; }
    String themePath() { return ""; }
};

#endif
