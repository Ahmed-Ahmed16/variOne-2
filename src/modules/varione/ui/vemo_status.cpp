// vemo_status.cpp — VariOne Vemo status screens (PRD: VariOne UI layer).
// Renders the Vemo mascot for scan / success / error moments. Per the UI
// revamp plan (Decision 4/7): scan screens show a SMALL persistent Vemo head
// with the status text beside it (never a text band over the face); success and
// error keep full-screen Vemo art with the status text pinned to a BOTTOM band.
// If the theme provides no vemo_* image (or its cached .bin is missing/corrupt),
// every path degrades gracefully to the stock Bruce text band.
//
// Phase 3: scan loops draw the head once via beginVemoScan() then repaint only
// the text via updateVemoScanText() to avoid the full-redraw flicker.

#include "vemo_status.h"
#include "core/display.h"
#include <globals.h>

namespace {

#ifdef HAS_SCREEN

constexpr int kHeadSize = 64;
constexpr int kTextX = 8 + kHeadSize + 8; // right of the head
constexpr int kFooterH = FP * 8 + 4;

// Tracks whether a scan head is currently drawn, so updateVemoScanText() knows
// it can repaint just the text region instead of falling back to the stock band.
bool s_scanHeadActive = false;

// Default footer hint mapped to the real VariOne S3 6-button layout
// (LEFT/RIGHT navigate, OK selects, BACK escapes — see boards/varione-s3).
const char *kDefaultFooter = "<> Nav   OK Sel   BACK Esc";

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

// Footer hint line (Tier-1 chrome). Small, secondary color, bottom edge.
void drawFooterHint(const String &hint) {
    if (hint == "") return;
    tft.setTextColor(bruceConfig.secColor, bruceConfig.bgColor);
    tft.setTextSize(FP);
    tft.setTextDatum(BC_DATUM);
    tft.drawString(hint, tftWidth / 2, tftHeight - 2);
    tft.setTextDatum(TL_DATUM);
}

// Repaints only the status text beside the head (no fillScreen / no head redraw).
// Uses the small (FP) font: a 64 px head leaves ~80 px of width, so FM (12 px/char)
// would clip "Scanning ..." — FP (6 px/char) fits ~13 chars in that band.
void repaintScanText(const String &text) {
    const int bandH = FM * 8 + 6; // clear a generous band so longer prior text is wiped
    const int y = tftHeight / 2;
    tft.fillRect(kTextX - 2, y - bandH / 2, tftWidth - kTextX, bandH, bruceConfig.bgColor);
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.setTextSize(FP);
    tft.setTextDatum(ML_DATUM);
    tft.drawString(text, kTextX, y);
    tft.setTextDatum(TL_DATUM);
}

// Full scan screen: small Vemo head + adjacent text + footer hint (plan Decision
// 4/7). Returns false if the head image is unavailable or its decode fails, so
// the caller can fall back to the stock Bruce text band.
bool drawScanFull(const String &text, const String &footer) {
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

    repaintScanText(text);
    drawFooterHint(footer);
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

String rssiBars(int rssiDbm) {
    int bars;
    if (rssiDbm >= -55) bars = 4;
    else if (rssiDbm >= -65) bars = 3;
    else if (rssiDbm >= -75) bars = 2;
    else if (rssiDbm >= -85) bars = 1;
    else bars = 0;
    String s = "[";
    for (int i = 0; i < 4; i++) s += (i < bars) ? '|' : '.';
    s += "]";
    return s;
}

void beginVemoScan(const String &message, const String &footerHint) {
#ifndef HAS_SCREEN
    Serial.println("VEMO scan: " + message);
#else
    String footer = (footerHint == "") ? String(kDefaultFooter) : footerHint;
    s_scanHeadActive = drawScanFull(message, footer);
    if (!s_scanHeadActive) displayTextLine(message); // stock fallback
#endif
}

void updateVemoScanText(const String &message) {
#ifndef HAS_SCREEN
    Serial.println("VEMO scan: " + message);
#else
    if (s_scanHeadActive) repaintScanText(message);
    else displayTextLine(message); // no head -> stock band each time
#endif
}

void showVemoStatus(const String &message, VemoStatus status, bool waitKeyPress) {
#ifndef HAS_SCREEN
    Serial.println("VEMO: " + message);
    return;
#else
    if (status == VemoStatus::Scan) {
        if (!drawScanFull(message, kDefaultFooter)) {
            s_scanHeadActive = false;
            displayTextLine(message, waitKeyPress); // stock fallback (handles its own wait)
            return;
        }
        s_scanHeadActive = true;
    } else {
        s_scanHeadActive = false;
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
