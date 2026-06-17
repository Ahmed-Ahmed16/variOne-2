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

    float best_freq = kBands[0].start;
    int best_rssi = -128;
    unsigned long lastDraw = 0;
    bool quit = false;

    while (!quit) {
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

                if (rssi > best_rssi) {
                    best_rssi = rssi;
                    best_freq = f;
                }

                // Throttle the redraw so per-step work stays short (and the
                // EscPress check below stays responsive). ~120 ms cadence.
                unsigned long now = millis();
                if (now - lastDraw >= 120) {
                    lastDraw = now;
                    tft.setTextSize(1);
                    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
                    tft.fillRect(kBlockX, kBaseY, kBlockW, kLineH * 5, bruceConfig.bgColor);
                    tft.setCursor(kBlockX, kBaseY);
                    tft.printf("Band %d/%d", b + 1, kBandCount);
                    tft.setCursor(kBlockX, kBaseY + kLineH);
                    tft.printf("Now : %.2f MHz ", f);
                    tft.setCursor(kBlockX, kBaseY + kLineH * 2);
                    tft.printf("Best: %.2f MHz ", best_freq);
                    tft.setCursor(kBlockX, kBaseY + kLineH * 3);
                    tft.printf("RSSI: %d dBm ", best_rssi);
                    tft.setCursor(kBlockX, kBaseY + kLineH * 4);
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
    }

    returnToMenu = true;
    deinitRfModule();
}
