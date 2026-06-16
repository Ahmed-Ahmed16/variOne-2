// vemo_status.cpp — VariOne Vemo status screens (PRD: VariOne UI layer).
// Renders the Vemo mascot for scan / success / error moments. Per the UI
// revamp plan (Decision 4/7): scan screens show a SMALL persistent Vemo head
// with the status text beside it (never a text band over the face); success and
// error keep full-screen Vemo art with the status text pinned to a BOTTOM band.
// If the theme provides no vemo_* image (or its cached .bin is missing/corrupt),
// every path degrades gracefully to the stock Bruce text band.

#include "vemo_status.h"
#include "core/display.h"
#include <globals.h>

namespace {

#ifdef HAS_SCREEN

constexpr int kHeadSize = 64;

// Status text band pinned to the BOTTOM of the screen, so full-screen Vemo art
// is never covered on the face (plan Decision 4).
void drawBottomBand(const String &text, uint16_t fg, uint16_t bg) {
    const int h = 24;
    const int y = tftHeight - h - 4;
    tft.fillRoundRect(8, y, tftWidth - 16, h, 6, bg);
    tft.setTextColor(fg, bg);
    tft.setTextSize(FM);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(text, tftWidth / 2, y + h / 2);
    tft.setTextDatum(TL_DATUM);
}

// Small persistent Vemo head + adjacent status text (plan Decision 4/7).
// Returns false if the small head image is unavailable or its decode fails, so
// the caller can fall back to the stock Bruce text band.
bool drawScanHeadAndText(const String &text) {
    if (!bruceConfig.theme.vemo_head || bruceConfig.theme.paths.vemo_head == "") return false;

    tft.fillScreen(bruceConfig.bgColor);
    const int headX = 8;
    const int headY = (tftHeight - kHeadSize) / 2;
    if (!drawImg(
            *bruceConfig.themeFS(),
            bruceConfig.getThemeItemImg(bruceConfig.theme.paths.vemo_head),
            headX,
            headY,
            false,
            0,
            false
        )) {
        return false; // missing/corrupt .bin -> fall back to stock band
    }

    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.setTextSize(FM);
    tft.setTextDatum(ML_DATUM);
    tft.drawString(text, headX + kHeadSize + 8, tftHeight / 2);
    tft.setTextDatum(TL_DATUM);
    return true;
}

// Full-screen Vemo art for success/error. Returns false if unavailable / decode
// fails so the caller can fall back to the stock band.
bool drawFullVemo(VariOneUI::VemoStatus status) {
    bool available;
    String path;
    if (status == VariOneUI::VemoStatus::Success) {
        available = bruceConfig.theme.vemo_success;
        path = bruceConfig.theme.paths.vemo_success;
    } else {
        available = bruceConfig.theme.vemo_error;
        path = bruceConfig.theme.paths.vemo_error;
    }
    if (!available || path == "") return false;

    tft.fillScreen(bruceConfig.bgColor);
    return drawImg(*bruceConfig.themeFS(), bruceConfig.getThemeItemImg(path), 0, 0, true, 0, false);
}

#endif // HAS_SCREEN

} // namespace

namespace VariOneUI {

void showVemoStatus(const String &message, VemoStatus status, bool waitKeyPress) {
#ifndef HAS_SCREEN
    Serial.println("VEMO: " + message);
    return;
#else
    if (status == VemoStatus::Scan) {
        if (!drawScanHeadAndText(message)) {
            displayTextLine(message, waitKeyPress); // stock fallback (handles its own wait)
            return;
        }
    } else {
        if (!drawFullVemo(status)) {
            // No Vemo art (or decode failed) -> stock centered band, which also
            // handles the key-wait itself.
            if (status == VemoStatus::Success) displaySuccess(message, waitKeyPress);
            else displayError(message, waitKeyPress);
            return;
        }
        if (status == VemoStatus::Success) drawBottomBand(message, TFT_WHITE, TFT_DARKGREEN);
        else drawBottomBand(message, TFT_WHITE, TFT_RED);
    }

    // Custom-drawn paths honor waitKeyPress here (stock paths returned above).
    if (waitKeyPress) {
        delay(200);
        while (!check(AnyKeyPress)) vTaskDelay(10 / portTICK_PERIOD_MS);
    }
#endif
}

} // namespace VariOneUI
