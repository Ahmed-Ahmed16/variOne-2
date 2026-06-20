#include "WebUIMenu.h"
#include "core/display.h"
#include "core/wifi/webInterface.h"

// Launch straight into the WebUI start options (my Network / AP mode), or the
// Stop / screen options when it is already running — same entry point the Files
// submenu used before WebUI was promoted to a top-level feature.
void WebUIMenu::optionsMenu() { loopOptionsWebUi(); }

// Globe icon — a scaled-up twin of the status-bar drawWebUISmall() mark
// (circle + meridian + parallels) that already appears top-right while the
// WebUI server is live, so the menu entry and the running indicator match.
void WebUIMenu::drawIcon(float scale) {
    clearIconArea();
    uint16_t c = bruceConfig.priColor;
    int r = scale * 24;
    int p = r * 0.55; // parallel half-length near the poles

    tft.drawCircle(iconCenterX, iconCenterY, r, c);
    tft.drawCircle(iconCenterX, iconCenterY, r - 1, c);

    // vertical meridian + axis
    tft.drawEllipse(iconCenterX, iconCenterY, r / 2, r, c);
    tft.drawLine(iconCenterX, iconCenterY - r, iconCenterX, iconCenterY + r, c);

    // parallels
    tft.drawLine(iconCenterX - p, iconCenterY - r / 2, iconCenterX + p, iconCenterY - r / 2, c);
    tft.drawLine(iconCenterX - r, iconCenterY, iconCenterX + r, iconCenterY, c);
    tft.drawLine(iconCenterX - p, iconCenterY + r / 2, iconCenterX + p, iconCenterY + r / 2, c);
}
