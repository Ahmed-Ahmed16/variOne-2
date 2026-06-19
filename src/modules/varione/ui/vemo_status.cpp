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
    const int textSize = text.length() > 12 ? FP : FM;
    tft.fillRoundRect(8, y, tftWidth - 16, h, 6, bg);
    tft.setTextColor(fg, bg);
    tft.setTextSize(textSize);
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

// ---- Vemo vector mascot ---------------------------------------------------
// Vemo is drawn from primitives (no PNG), so every expression is crisp at any
// size and identical across scan / idle / success / error. This replaces the
// old theme PNG heads (which scaled up blurry). Brand palette is fixed to the
// mascot identity: white body, dark-navy bandit mask + ears, theme-accent eyes
// + the cyan "V" muzzle. Only the accent tracks the active theme (priColor).
enum class Mood { Scan, Idle, Sleep, Success, Error };

constexpr uint16_t kVemoNavy = 0x192A; // dark navy (mask + ears + outline)

void drawVemoFace(int cx, int cy, int s, Mood mood) {
    const uint16_t white = TFT_WHITE;
    const uint16_t cyan = bruceConfig.priColor; // theme accent
    const uint16_t bg = bruceConfig.bgColor;

    const int headW = s;
    const int headH = (s * 92) / 100;
    const int hx = cx - headW / 2;
    const int hy = cy - headH / 2;
    const int rad = s / 4;
    const int earR = (s * 17) / 100;
    const int eyeDX = (s * 21) / 100;
    const int eyeR = (s * 9) / 100;
    const int eyeY = cy - s / 20;
    int eyes[2] = {cx - eyeDX, cx + eyeDX};

    // Antenna (active moods only) — drawn first so the head overlaps its base.
    if (mood == Mood::Scan || mood == Mood::Idle) {
        int ay = hy - s / 6;
        tft.drawWideLine(cx, hy, cx, ay, 2, kVemoNavy, bg);
        tft.fillCircle(cx, ay, max(2, s / 18), cyan);
    }

    // Ears: navy disc with a white inner disc, behind the head's top corners.
    for (int i = -1; i <= 1; i += 2) {
        int ex = cx + i * (s * 32) / 100;
        int ey = hy + earR / 3;
        tft.fillCircle(ex, ey, earR, kVemoNavy);
        tft.fillCircle(ex, ey, (earR * 5) / 10, white);
    }

    // Head: white rounded square with a 2 px navy outline.
    tft.fillRoundRect(hx, hy, headW, headH, rad, white);
    tft.drawRoundRect(hx, hy, headW, headH, rad, kVemoNavy);
    tft.drawRoundRect(hx + 1, hy + 1, headW - 2, headH - 2, rad - 1, kVemoNavy);

    // Bandit mask: navy rounded band across the eyes.
    int mW = (s * 84) / 100, mH = (s * 34) / 100;
    int mX = cx - mW / 2, mY = eyeY - mH / 2;
    tft.fillRoundRect(mX, mY, mW, mH, mH / 2, kVemoNavy);

    for (int i = 0; i < 2; i++) {
        int ex = eyes[i];
        if (mood == Mood::Sleep) {
            // Closed, content eyes: a shallow downward "v" (lashes) per eye.
            tft.drawWideLine(ex - eyeR, eyeY - eyeR / 3, ex, eyeY + eyeR / 2, 2, cyan, kVemoNavy);
            tft.drawWideLine(ex, eyeY + eyeR / 2, ex + eyeR, eyeY - eyeR / 3, 2, cyan, kVemoNavy);
        } else if (mood == Mood::Error) {
            // X eyes.
            tft.drawWideLine(ex - eyeR, eyeY - eyeR, ex + eyeR, eyeY + eyeR, 2, cyan, kVemoNavy);
            tft.drawWideLine(ex - eyeR, eyeY + eyeR, ex + eyeR, eyeY - eyeR, 2, cyan, kVemoNavy);
        } else {
            // Open eyes (scan / idle / success): cyan disc + white glint.
            tft.fillCircle(ex, eyeY, eyeR, cyan);
            tft.fillCircle(ex - eyeR / 3, eyeY - eyeR / 3, max(1, eyeR / 3), white);
        }
    }

    // Brand "V" muzzle in cyan, below the mask.
    int vy = mY + mH + s / 12;
    int vw = (s * 12) / 100;
    tft.drawWideLine(cx - vw, vy, cx, vy + vw, 2, cyan, white);
    tft.drawWideLine(cx, vy + vw, cx + vw, vy, 2, cyan, white);

    // Success sparkles flanking the head.
    if (mood == Mood::Success) {
        for (int i = -1; i <= 1; i += 2) {
            int sx = cx + i * (s * 48) / 100;
            int sy = hy + s / 8;
            tft.drawWideLine(sx - 3, sy, sx + 3, sy, 2, cyan, bg);
            tft.drawWideLine(sx, sy - 3, sx, sy + 3, 2, cyan, bg);
        }
    }
}

// Scan screen: small vector Vemo head (left) + adjacent text + footer hint.
// Always succeeds (no asset dependency) so it never falls back to the stock band.
bool drawScanFull(const String &text, const String &footer) {
    tft.fillScreen(bruceConfig.bgColor);
    drawVemoFace(8 + kHeadSize / 2, tftHeight / 2, kHeadSize, Mood::Scan);
    repaintScanText(text);
    drawFooterHint(footer);
    return true;
}

// Full-screen vector Vemo for success/error (large, centered; caller adds band).
bool drawFullVemo(VariOneUI::VemoStatus status) {
    tft.fillScreen(bruceConfig.bgColor);
    Mood m = (status == VariOneUI::VemoStatus::Success) ? Mood::Success : Mood::Error;
    drawVemoFace(tftWidth / 2, 50, 84, m);
    return true;
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

void vemoBootSplash() {
#ifndef HAS_SCREEN
    Serial.println("VEMO boot splash");
#else
    const int cx = tftWidth / 2;
    const int cy = 58;
    const int sFinal = (tftWidth < tftHeight ? tftWidth : tftHeight) * 70 / 100;

    // Zoom-in pop: ramp the vector head small -> large. Any key skips straight
    // to the settled frame.
    bool skipped = false;
    for (int s = 12; s <= sFinal; s += 6) {
        tft.fillScreen(bruceConfig.bgColor);
        drawVemoFace(cx, cy, s, Mood::Success); // Success = smiling/happy Vemo
        if (check(AnyKeyPress)) {
            skipped = true;
            break;
        }
        delay(18);
    }

    // Settle on the final happy face + wordmark + motto.
    tft.fillScreen(bruceConfig.bgColor);
    drawVemoFace(cx, cy, sFinal, Mood::Success);

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.setTextSize(FM);
    tft.drawString("VARIONE", cx, 118);
    tft.setTextSize(FP);
    tft.setTextColor(TFT_WHITE, bruceConfig.bgColor);
    tft.drawString("Tiny outside.", cx, 134);
    tft.drawString("Powerful inside.", cx, 146);
    tft.setTextDatum(TL_DATUM);

    // Hold briefly so the wordmark reads; any key cuts it short.
    if (!skipped) {
        uint32_t hold = millis() + 1400;
        while (millis() < hold) {
            if (check(AnyKeyPress)) break;
            delay(20);
        }
    }
#endif
}

void drawVemoSleep() {
#ifndef HAS_SCREEN
    Serial.println("VEMO idle: sleeping");
#else
    tft.fillScreen(bruceConfig.bgColor);

    const int s = 76;
    const int cx = tftWidth / 2;
    const int cy = tftHeight / 2 - 2;
    drawVemoFace(cx, cy, s, Mood::Sleep);

    // "Zzz" rising from the top-right of the head (small -> large).
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.setTextDatum(ML_DATUM);
    tft.setTextSize(FP);
    tft.drawString("z", cx + s / 2 - 6, cy - s / 3);
    tft.setTextSize(FM);
    tft.drawString("Z", cx + s / 2 + 4, cy - s / 2 - 2);
    tft.setTextDatum(TL_DATUM);
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
