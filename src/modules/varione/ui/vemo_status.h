#ifndef VARIONE_VEMO_STATUS_H
#define VARIONE_VEMO_STATUS_H

#include <Arduino.h>

namespace VariOneUI {

enum class VemoStatus {
    Scan,
    Success,
    Error,
};

// One-shot status screen. Scan = small Vemo head + adjacent text; Success/Error =
// full-screen Vemo art + bottom band. Degrades to the stock Bruce band when no
// vemo_* image (or its cached .bin) is available.
void showVemoStatus(const String &message, VemoStatus status = VemoStatus::Scan, bool waitKeyPress = false);

// Phase 3 scan-loop helpers (anti-flicker). beginVemoScan() draws the persistent
// Vemo head + footer hint ONCE; updateVemoScanText() repaints only the status text
// region on later loop iterations (no fillScreen, no head redraw). Both fall back
// to the stock text band when no head art is available. Call beginVemoScan() again
// after a full-screen takeover (e.g. loopOptions) before resuming text updates.
void beginVemoScan(const String &message, const String &footerHint = "");
void updateVemoScanText(const String &message);

// Boot splash: zoom-in "pop" of the Vemo mascot (small -> large), then the
// VARIONE wordmark + motto. Skippable by any key. No-op without a screen.
void vemoBootSplash();

// RSSI (dBm) -> friendly 0..4 bar glyph string, e.g. "[||..]". Used in scan result
// rows instead of raw dBm for awareness demos (plan Tier-1 chrome).
String rssiBars(int rssiDbm);

// Idle screen: centered sleeping Vemo head + "Zzz" on the minimal blue/black
// theme. Drawn by the main menu after an idle timeout; any key restores the menu.
// Falls back to a centered "Zzz..." text band if the sleeping art is unavailable.
void drawVemoSleep();

} // namespace VariOneUI

#endif // VARIONE_VEMO_STATUS_H
