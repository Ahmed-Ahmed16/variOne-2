#include "BadUSBMenu.h"
#include "core/display.h"
#include "core/utils.h"
#include "modules/badusb_ble/ducky_typer.h"

// Selecting the top-level BadUSB item launches the USB HID ducky runner directly:
// it presents the SD/LittleFS script picker, waits for the host to enumerate the
// keyboard, runs the chosen payload, and loops until the user backs out. The call
// blocks and sets returnToMenu=true on exit, so no extra loop is needed here.
void BadUSBMenu::optionsMenu() { ducky_setup(hid_usb, false); }

// USB-keyboard glyph drawn from TFT primitives (theme-agnostic, matches the other
// vector icons): a rounded chassis with a 2-row key grid and a spacebar.
void BadUSBMenu::drawIcon(float scale) {
    clearIconArea();

    int w = scale * 62;
    int h = scale * 40;
    if (w % 2 != 0) w++;
    if (h % 2 != 0) h++;

    int x = iconCenterX - w / 2;
    int y = iconCenterY - h / 2;
    int r = scale * 5;

    // Keyboard chassis (double outline for a crisp edge at small scales).
    tft.drawRoundRect(x, y, w, h, r, bruceConfig.priColor);
    tft.drawRoundRect(x + 1, y + 1, w - 2, h - 2, r, bruceConfig.priColor);

    // Two rows of keys.
    int cols = 4;
    int kw = scale * 8;
    int kh = scale * 6;
    int gap = scale * 4;
    int gridW = cols * kw + (cols - 1) * gap;
    int gx = iconCenterX - gridW / 2;
    int gy = y + scale * 7;

    for (int row = 0; row < 2; row++) {
        for (int col = 0; col < cols; col++) {
            tft.fillRect(gx + col * (kw + gap), gy + row * (kh + gap), kw, kh, bruceConfig.priColor);
        }
    }

    // Spacebar.
    int sbW = gridW * 2 / 3;
    int sbY = gy + 2 * (kh + gap);
    tft.fillRect(iconCenterX - sbW / 2, sbY, sbW, kh, bruceConfig.priColor);
}
