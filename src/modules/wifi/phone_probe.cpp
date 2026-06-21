// Phone probe-request sniffer. Implements wifi_phone_probe() from phone_probe.h:
// promiscuous capture while hopping channels 1-13, parses management probe-request
// frames (FC type 0=mgmt, subtype 4 => byte0 0x40), pulls the source MAC and the
// requested SSID tag, dedups into a most-recent-first list, and renders it on the
// TFT. Directed probes (a named SSID a phone remembers) are the interesting ones;
// wildcard probes show as "<any>". Self-contained — own callback + WiFi teardown.

#include "phone_probe.h"

#include <Arduino.h>
#include <WiFi.h>
#include "esp_wifi.h"

#include "core/display.h"
#include "core/wifi/webInterface.h"
#include "globals.h"

#define PP_FIRST_CH 1
#define PP_LAST_CH 13
#define PP_DWELL_MS 120 // per-channel listen window
#define PP_RING 24      // raw capture ring (callback -> main loop)
#define PP_LIST 32      // unique devices kept
#define PP_SSID_MAX 32

struct PProbe {
    uint8_t mac[6];
    char ssid[PP_SSID_MAX + 1];
    int8_t rssi;
    uint8_t ch;
};

// Raw ring filled by the callback, drained by the main loop.
static volatile PProbe pp_ring[PP_RING];
static volatile uint8_t pp_head = 0; // next write slot (callback)
static uint8_t pp_tail = 0;          // next read slot (main loop)

// Deduped device list, newest first.
static PProbe pp_list[PP_LIST];
static int pp_count = 0;

static void pp_rx_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT) return;
    const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;
    const uint8_t *p = pkt->payload;
    int len = pkt->rx_ctrl.sig_len;
    if (len < 26) return;
    if (p[0] != 0x40) return; // not a probe request

    PProbe e;
    memcpy(e.mac, p + 10, 6); // addr2 = source (the phone)
    e.rssi = pkt->rx_ctrl.rssi;
    e.ch = pkt->rx_ctrl.channel;

    // First tagged parameter at offset 24 must be the SSID element (tag 0).
    uint8_t tag = p[24];
    uint8_t slen = p[25];
    if (tag == 0 && slen > 0 && slen <= PP_SSID_MAX && (26 + slen) <= len) {
        memcpy(e.ssid, p + 26, slen);
        e.ssid[slen] = '\0';
        for (int i = 0; i < slen; i++) {
            if (e.ssid[i] < 32 || e.ssid[i] > 126) e.ssid[i] = '.';
        }
    } else {
        e.ssid[0] = '\0'; // wildcard / broadcast probe
    }

    uint8_t h = pp_head;
    memcpy((void *)&pp_ring[h], &e, sizeof(PProbe));
    pp_head = (h + 1) % PP_RING;
}

static void pp_ingest(const PProbe &e) {
    // Move-to-front if this MAC+SSID already seen, else insert at front.
    for (int i = 0; i < pp_count; i++) {
        if (memcmp(pp_list[i].mac, e.mac, 6) == 0 && strcmp(pp_list[i].ssid, e.ssid) == 0) {
            PProbe tmp = e;
            for (int j = i; j > 0; j--) pp_list[j] = pp_list[j - 1];
            pp_list[0] = tmp;
            return;
        }
    }
    if (pp_count < PP_LIST) pp_count++;
    for (int j = pp_count - 1; j > 0; j--) pp_list[j] = pp_list[j - 1];
    pp_list[0] = e;
}

void wifi_phone_probe() {
    cleanlyStopWebUiForWiFiFeature();

    esp_wifi_set_promiscuous(false);
    if (!WiFi.mode(WIFI_MODE_STA)) {
        displayError("WiFi start failed", true);
        return;
    }
    delay(80);

    pp_head = pp_tail = 0;
    pp_count = 0;

    wifi_promiscuous_filter_t filt;
    filt.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
    esp_wifi_set_promiscuous_filter(&filt);
    esp_wifi_set_promiscuous_rx_cb(pp_rx_cb);
    esp_wifi_set_promiscuous(true);

    const int lineH = 9;
    const int top = STATUS_BAR_HEIGHT + 16; // below status bar (30) + title band
    const int footerH = 12;
    const int clearX = BORDER_OFFSET_FROM_SCREEN_EDGE + 1;
    const int clearW = tftWidth - 2 * clearX;
    int rows = (tftHeight - top - footerH) / lineH;
    if (rows < 1) rows = 1;

    uint8_t ch = PP_FIRST_CH;
    uint32_t lastHop = 0;
    bool redraw = true;

    drawMainBorderWithTitle("Phone Probes");
    tft.setTextSize(1);

    while (true) {
        if (check(EscPress)) break;

        // Channel hop.
        if (millis() - lastHop > PP_DWELL_MS) {
            ch++;
            if (ch > PP_LAST_CH) ch = PP_FIRST_CH;
            esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
            lastHop = millis();
        }

        // Drain the capture ring.
        while (pp_tail != pp_head) {
            PProbe e;
            memcpy(&e, (const void *)&pp_ring[pp_tail], sizeof(PProbe));
            pp_tail = (pp_tail + 1) % PP_RING;
            pp_ingest(e);
            redraw = true;
        }

        if (redraw) {
            tft.fillRect(
                clearX, top, clearW, tftHeight - top - BORDER_OFFSET_FROM_SCREEN_EDGE, bruceConfig.bgColor
            );
            tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
            int shown = min(rows, pp_count);
            for (int i = 0; i < shown; i++) {
                const PProbe &e = pp_list[i];
                char macTail[8];
                snprintf(macTail, sizeof(macTail), "%02X%02X", e.mac[4], e.mac[5]);
                String ssid = (e.ssid[0] == '\0') ? String("<any>") : String(e.ssid);
                if (ssid.length() > 12) ssid = ssid.substring(0, 12);
                String row = String(macTail) + " " + ssid + " " + String((int)e.rssi);
                tft.drawString(row, clearX, top + i * lineH);
            }
            String foot = String(pp_count) + " seen  c" + String(ch) + " BACK=exit";
            tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
            tft.drawString(foot, clearX, tftHeight - footerH);
            redraw = false;
        }
        delay(5);
    }

    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(NULL);
    WiFi.mode(WIFI_MODE_NULL);
}
