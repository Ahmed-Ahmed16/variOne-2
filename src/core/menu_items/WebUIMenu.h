/*
 * WebUIMenu — top-level main-menu feature that launches the on-device WebUI
 * (file manager / device control served over Wi-Fi). Promoted out of the Files
 * submenu so the WebUI is a first-class carousel entry, ordered before Files.
 * Mirrors the MenuItemInterface pattern used by the other src/core/menu_items.
 */
#ifndef __WEBUI_MENU_H__
#define __WEBUI_MENU_H__

#include <MenuItemInterface.h>

class WebUIMenu : public MenuItemInterface {
public:
    WebUIMenu() : MenuItemInterface("WebUI") {}

    void optionsMenu(void);
    void drawIcon(float scale);
    bool hasTheme() { return false; }
    String themePath() { return ""; }
};

#endif
