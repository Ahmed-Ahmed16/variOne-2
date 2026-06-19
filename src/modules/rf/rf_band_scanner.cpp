// rf_band_scanner: all-band CC1101 frequency sweep. Implements rf_band_scanner()
// declared in rf_band_scanner.h. Sweeps the three CC1101 sub-bands in turn,
// setting the radio frequency (setMHZ) and sampling RSSI (ELECHOUSE_cc1101
// .getRssi()) at a ~1 MHz step, tracking the strongest frequency observed and
// showing a live blue/black status screen. Loops until BACK (EscPress) is
// pressed. Reuses the shared SPI/CC1101 helpers from rf_utils.* (the same path
// rf_spectrum / rf_waterfall use) and guards on CC1101 presence like they do.

#include "rf_band_scanner.h"
#include "core/display.h"
#include "rf_utils.h"
#include "structs.h"
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <globals.h>

#ifndef TFT_MOSI
#define TFT_MOSI -1
#endif

// CC1101 supported sub-bands. Kept slightly inside the hardware limits.
struct RfSubBand {
    float start;
    float end;
};

static const RfSubBand kBands[] = {
    {300.0f, 348.0f},
    {387.0f, 464.0f},
    {779.0f, 928.0f},
};
static const int kBandCount = sizeof(kBands) / sizeof(kBands[0]);

// ~1 MHz step. Total steps ~= (48 + 77 + 149) / 1 = 274 samples. With a short
// per-step settle (~5 ms) one full sweep lands well under a few seconds.
static const float kStepMhz = 1.0f;

void rf_band_scanner() {
    if (bruceConfigPins.rfModule != CC1101_SPI_MODULE) {
        displayError("Band Scanner needs a CC1101!", true);
        return;
    }
    if (!initRfModule("rx", kBands[0].start)) {
        displayError("CC1101 not found!", true);
        return;
    }

    drawMainBorderWithTitle("RF Band Scanner");

    // Fixed-pixel layout (matches the minimal RF screens, e.g. rf_spectrum).
    const int kLineH = 12;       // line pitch at text size 1
    const int kBaseY = 26;       // first status line, below the title border
    const int kBlockX = 8;
    const int kBlockW = tftWidth - 16;

    // A single "strongest freq ever" with a strict > comparison froze Best at the
    // first sample (300 MHz) on a flat noise floor (issue 9). Instead: track a
    // running noise floor (quietest reading) and, each sweep, the loudest freq;
    // report it as real activity only when it rises kActiveMarginDb above floor.
    const int kActiveMarginDb = 10;
    int noise_floor = 0;   // starts high; pulled down by the first samples
    bool floorInit = false;

    float peak_freq = 0.0f; // committed peak of the last completed sweep
    int peak_rssi = -128;
    bool peak_active = false;
    unsigned long lastDraw = 0;
    bool quit = false;

    while (!quit) {
        // Reset per-sweep peak so Best tracks current activity, not a stale max.
        float sweep_peak_freq = 0.0f;
        int sweep_peak_rssi = -128;

        for (int b = 0; b < kBandCount && !quit; b++) {
            for (float f = kBands[b].start; f <= kBands[b].end; f += kStepMhz) {
                setMHZ(f);
                // Keep CC1101/TFT shared-SPI happy, then let the radio settle.
                if (bruceConfigPins.CC1101_bus.mosi == TFT_MOSI) {
                    tft.drawPixel(0, 0, 0);
                    delayMicroseconds(150);
                } else {
                    delayMicroseconds(100);
                }
                vTaskDelay(pdMS_TO_TICKS(5));

                int rssi = ELECHOUSE_cc1101.getRssi();
                if (bruceConfigPins.CC1101_bus.mosi == TFT_MOSI) tft.drawPixel(0, 0, 0);

                // Running noise floor = quietest reading seen.
                if (!floorInit || rssi < noise_floor) {
                    noise_floor = rssi;
                    floorInit = true;
                }
                // Loudest freq within this sweep.
                if (rssi > sweep_peak_rssi) {
                    sweep_peak_rssi = rssi;
                    sweep_peak_freq = f;
                }

                // Throttle the redraw so per-step work stays short (and the
                // EscPress check below stays responsive). ~120 ms cadence.
                unsigned long now = millis();
                if (now - lastDraw >= 120) {
                    lastDraw = now;
                    tft.setTextSize(1);
                    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
                    tft.fillRect(kBlockX, kBaseY, kBlockW, kLineH * 6, bruceConfig.bgColor);
                    tft.setCursor(kBlockX, kBaseY);
                    tft.printf("Band %d/%d", b + 1, kBandCount);
                    tft.setCursor(kBlockX, kBaseY + kLineH);
                    tft.printf("Now  : %.2f MHz ", f);
                    tft.setCursor(kBlockX, kBaseY + kLineH * 2);
                    tft.printf("Floor: %d dBm ", floorInit ? noise_floor : 0);
                    tft.setCursor(kBlockX, kBaseY + kLineH * 3);
                    if (peak_active)
                        tft.printf("Peak : %.2f MHz", peak_freq);
                    else
                        tft.print("Peak : scanning...   ");
                    tft.setCursor(kBlockX, kBaseY + kLineH * 4);
                    if (peak_active) tft.printf("Signal: %d dBm ", peak_rssi);
                    else tft.print("Signal: none         ");
                    tft.setCursor(kBlockX, kBaseY + kLineH * 5);
                    tft.print("Press BACK to stop.");
                }

                // BACK must always be cancellable. Poll every step; per-step
                // work above is far below the ~75 ms tap window.
                if (check(EscPress)) {
                    quit = true;
                    break;
                }
            }
        }

        // Commit this sweep's peak; it counts as activity only above the floor.
        peak_freq = sweep_peak_freq;
        peak_rssi = sweep_peak_rssi;
        peak_active = floorInit && (sweep_peak_rssi > noise_floor + kActiveMarginDb);
    }

    returnToMenu = true;
    deinitRfModule();
}
