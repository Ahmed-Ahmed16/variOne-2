// WiFi 2.4 GHz channel activity grapher. Implements the bar-graph utility declared
// in channel_graph.h: promiscuous capture while hopping channels 1-14, per-sweep
// packet counts drawn as TFT-scaled bars (green/yellow/red by relative level),
// current channel outlined, BACK to exit. Self-contained — owns its own promiscuous
// callback and WiFi teardown so it never depends on attack-module state.

#include "channel_graph.h"

#include <Arduino.h>
#include <WiFi.h>
#include "esp_wifi.h"

#include "core/display.h"
#include "core/wifi/webInterface.h"
#include "globals.h"

#define CG_FIRST_CH 1
#define CG_LAST_CH 13
#define CG_NUM_CH (CG_LAST_CH - CG_FIRST_CH + 1)
#define CG_DWELL_MS 90 // per-channel listen time (sweep ~ CG_NUM_CH * dwell)

// Raw packet tally filled by the promiscuous callback for the channel currently
// being listened on; consumed and reset once per full sweep.
static volatile uint32_t cg_acc[CG_NUM_CH] = {0};
static volatile int cg_idx = 0; // index into cg_acc for the active channel

static void cg_rx_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
    int i = cg_idx;
    if (i >= 0 && i < CG_NUM_CH) cg_acc[i]++;
}

static uint16_t cg_color(uint32_t v, uint32_t maxv) {
    if (maxv == 0) return TFT_DARKGREY;
    uint32_t pct = (v * 100) / maxv;
    if (pct > 66) return TFT_RED;
    if (pct > 33) return TFT_ORANGE;
    return TFT_GREEN;
}

void wifi_channel_graph() {
    cleanlyStopWebUiForWiFiFeature();

    esp_wifi_set_promiscuous(false);
    if (!WiFi.mode(WIFI_MODE_STA)) {
        displayError("WiFi start failed", true);
        return;
    }
    delay(80);

    wifi_promiscuous_filter_t filt;
    filt.filter_mask = WIFI_PROMIS_FILTER_MASK_ALL;
    esp_wifi_set_promiscuous_filter(&filt);
    esp_wifi_set_promiscuous_rx_cb(cg_rx_cb);
    esp_wifi_set_promiscuous(true);

    // Per-channel displayed value (EMA-smoothed across sweeps for a live feel).
    uint32_t disp[CG_NUM_CH] = {0};

    // ── Layout (clears below the status bar + title; stays inside the border) ──
    const int top = STATUS_BAR_HEIGHT + 16; // below status bar (30) + title band
    const int footerH = 20;                  // two footer lines (best-ch + hint)
    const int labelH = 8;                    // channel-number row under the bars
    const int marginL = 8;
    const int drawW = tftWidth - marginL - 4;
    const int barTop = top;
    const int barAreaH = tftHeight - barTop - footerH - labelH;
    const int clearX = BORDER_OFFSET_FROM_SCREEN_EDGE + 1;
    const int clearW = tftWidth - 2 * clearX;

    drawMainBorderWithTitle("Channel Graph");
    tft.setTextSize(1);

    bool first = true;
    while (true) {
        // ── One sweep across all channels ──
        for (int i = 0; i < CG_NUM_CH; i++) {
            cg_idx = i;
            cg_acc[i] = 0;
            esp_wifi_set_channel(CG_FIRST_CH + i, WIFI_SECOND_CHAN_NONE);
            uint32_t t0 = millis();
            while (millis() - t0 < CG_DWELL_MS) {
                if (check(EscPress)) goto done;
                delay(2);
            }
            // EMA: fast attack, gentle decay -> bars react but don't flicker.
            uint32_t s = cg_acc[i];
            disp[i] = first ? s : (disp[i] * 1 + s * 3) / 4;
        }
        first = false;

        // ── Draw ──
        uint32_t maxv = 1;
        uint32_t total = 0;
        int peakCh = CG_FIRST_CH;
        for (int i = 0; i < CG_NUM_CH; i++) {
            total += disp[i];
            if (disp[i] > maxv) {
                maxv = disp[i];
                peakCh = CG_FIRST_CH + i;
            }
        }

        // Least-contested of the non-overlapping channels (1/6/11) = the one to use.
        int bestCh = 1;
        uint32_t bestVal = 0xFFFFFFFF;
        const int nonOverlap[3] = {1, 6, 11};
        for (int k = 0; k < 3; k++) {
            int idx = nonOverlap[k] - CG_FIRST_CH;
            if (idx >= 0 && idx < CG_NUM_CH && disp[idx] < bestVal) {
                bestVal = disp[idx];
                bestCh = nonOverlap[k];
            }
        }
        (void)peakCh;

        tft.fillRect(clearX, top, clearW, tftHeight - top - BORDER_OFFSET_FROM_SCREEN_EDGE, bruceConfig.bgColor);
        for (int i = 0; i < CG_NUM_CH; i++) {
            int ch = CG_FIRST_CH + i;
            int x = marginL + (i * drawW) / CG_NUM_CH;
            int w = max(1, (marginL + ((i + 1) * drawW) / CG_NUM_CH) - x - 1);
            int h = (int)((uint32_t)barAreaH * disp[i] / maxv);
            if (h < 1 && disp[i] > 0) h = 1;
            int y = barTop + barAreaH - h;
            if (h > 0) tft.fillRect(x, y, w, h, cg_color(disp[i], maxv));
            // Channel number under every bar; the recommended channel in green.
            tft.setTextColor(ch == bestCh ? TFT_GREEN : bruceConfig.priColor, bruceConfig.bgColor);
            tft.drawString(String(ch), x, barTop + barAreaH + 1);
        }

        // Footer: recommended (least-contested) 2.4 GHz channel to use.
        tft.setTextColor(TFT_GREEN, bruceConfig.bgColor);
        tft.drawString("Best ch" + String(bestCh) + " of 1/6/11", marginL, tftHeight - footerH);
        tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
        tft.drawString("BACK=exit", marginL, tftHeight - footerH + 9);

        if (check(EscPress)) break;
    }

done:
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(NULL);
    WiFi.mode(WIFI_MODE_NULL);
}
