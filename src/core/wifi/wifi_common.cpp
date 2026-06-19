#include "core/wifi/wifi_common.h"
#include "core/display.h"    // using displayRedStripe  and loop options
#include "core/mykeyboard.h" // usinf keyboard when calling rename
#include "core/powerSave.h"
#include "core/settings.h"
#include "core/utils.h"
#include "core/wifi/wifi_mac.h" // Set Mac Address - @IncursioHack
#include "modules/varione/ui/vemo_status.h"
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <globals.h>

static TaskHandle_t timezoneTaskHandle = NULL;
static bool wifiTransitioning = false;

void ensureWifiPlatform() {
    static bool netifInitialized = false;
    static bool eventLoopCreated = false;
    static portMUX_TYPE platformMux = portMUX_INITIALIZER_UNLOCKED;

    portENTER_CRITICAL(&platformMux);
    bool needNetif = !netifInitialized;
    bool needLoop = !eventLoopCreated;
    portEXIT_CRITICAL(&platformMux);

    if (needNetif) {
        ESP_ERROR_CHECK(esp_netif_init());
        portENTER_CRITICAL(&platformMux);
        netifInitialized = true;
        portEXIT_CRITICAL(&platformMux);
    }

    if (needLoop) {
        esp_err_t err = esp_event_loop_create_default();
        if (err != ESP_ERR_INVALID_STATE) { ESP_ERROR_CHECK(err); }
        portENTER_CRITICAL(&platformMux);
        eventLoopCreated = true;
        portEXIT_CRITICAL(&platformMux);
    }
}

bool _wifiConnect(const String &ssid, int encryption) {
    String password = bruceConfig.getWifiPassword(ssid);
    if (password == "" && encryption > 0) { password = keyboard(password, 63, "Network Password:", true); }
    bool connected = _connectToWifiNetwork(ssid, password);
    bool retry = false;

    while (!connected) {
        wakeUpScreen();

        options = {
            {"Retry",  [&]() { retry = true; } },
            {"Cancel", [&]() { retry = false; }},
        };
        loopOptions(options);

        if (!retry) {
            wifiDisconnect();
            return false;
        }

        password = keyboard(password, 63, "Network Password:", true);
        connected = _connectToWifiNetwork(ssid, password);
    }

    if (connected) {
        wifiConnected = true;
        wifiIP = WiFi.localIP().toString();
        bruceConfig.addWifiCredential(ssid, password);

        // Start timezone update in background if not already running
        if (timezoneTaskHandle == NULL) {
            xTaskCreate(updateTimezoneTask, "updateTimezone", 4096, NULL, 1, &timezoneTaskHandle);
        }
    }

    delay(200);
    return connected;
}

bool _connectToWifiNetwork(const String &ssid, const String &pwd) {
    drawMainBorderWithTitle("WiFi Connect");
    padprintln("");
    padprint("Connecting to: " + ssid + ".");
    WiFi.mode(WIFI_MODE_STA);
    vTaskDelay(10 / portTICK_PERIOD_MS);
    WiFi.begin(ssid, pwd);

    int i = 1;
    while (WiFi.status() != WL_CONNECTED) {
        if (tft.getCursorX() >= tftWidth - 12) {
            padprintln("");
            padprint("");
        }
#ifdef HAS_SCREEN
        tft.print(".");
#else
        Serial.print(".");
#endif

        if (i > 20) {
#if defined(VARIONE_VEMO_UI)
            VariOneUI::showVemoStatus("WiFi Offline", VariOneUI::VemoStatus::Error);
#else
            displayError("Wifi Offline");
#endif
            vTaskDelay(500 / portTICK_RATE_MS);
            break;
        }

        vTaskDelay(500 / portTICK_RATE_MS);
        i++;
    }

    bool connected = WiFi.status() == WL_CONNECTED;
#if defined(VARIONE_VEMO_UI)
    if (connected) VariOneUI::showVemoStatus("WiFi Connected", VariOneUI::VemoStatus::Success);
#endif
    return connected;
}

bool _setupAP() {
    // Guard the SSID: an empty SSID makes softAP() fail to broadcast.
    String apSsid = bruceConfig.wifiAp.ssid;
    if (apSsid.isEmpty()) apSsid = "VariOne";

    // Guard the password: WPA2 requires >= 8 chars. A 1-7 char password makes
    // softAP() silently return false and the AP never appears. Fall back to an
    // open AP in that case.
    String apPwd = bruceConfig.wifiAp.pwd;
    const char *pwdArg = (apPwd.length() >= 8) ? apPwd.c_str() : nullptr;

    // Clean radio state, then AP mode. On the S3 a leftover STA/promiscuous
    // state can leave the AP "started" (softAP() returns true) but not actually
    // broadcasting a beacon.
    WiFi.mode(WIFI_OFF);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    WiFi.mode(WIFI_AP);
    vTaskDelay(100 / portTICK_PERIOD_MS);

    // Defensive: a prior WiFi attack leaves the radio in promiscuous/sniff
    // state (wifi_atk_unsetWifi never disabled it). With the driver now started
    // in AP mode, force promiscuous off and clear the RX callback so the AP
    // actually emits a beacon. One spot fixes every AP raise regardless of
    // prior state (menu / debrief / WebUI).
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(NULL);

    // Lock an explicit regulatory country with MANUAL policy. The default
    // country "01" (worldwide) uses AUTO policy, which waits to detect a
    // country from a received beacon before it will actively transmit the
    // AP's own beacon -- so the AP starts but never broadcasts a visible SSID.
    // MANUAL policy lets the AP beacon immediately on channels 1-11.
    wifi_country_t country = {};
    strncpy(country.cc, "EG", sizeof(country.cc)); // Egypt: channels 1-13
    country.schan = 1;
    country.nchan = 13;
    country.policy = WIFI_COUNTRY_POLICY_MANUAL;
    esp_wifi_set_country(&country);

    // Start the AP on a fixed channel and the DEFAULT IP (192.168.4.1). Do NOT
    // call softAPConfig() here: reconfiguring the IP to a non-default gateway
    // after softAP() was observed to start the AP without a visible beacon on
    // this S3 board.
    bool ok = WiFi.softAP(apSsid.c_str(), pwdArg, 1, 0, 4, false);
    if (!ok) {
        Serial.println("[AP] softAP() failed to start");
        wifiConnected = false;
        return false;
    }
    // Force a healthy TX power; some S3 clones boot AP mode at very low power.
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    esp_wifi_set_max_tx_power(80); // 80 = 20 dBm (0.25 dBm units)

    wifiIP = WiFi.softAPIP().toString(); // update global var
    Serial.println("[AP] SSID: " + apSsid + "  IP: " + wifiIP);

    // --- diagnostics: what is the radio actually set to? ---
    wifi_mode_t m = WIFI_MODE_NULL;
    esp_wifi_get_mode(&m);
    int8_t maxpow = 0;
    esp_wifi_get_max_tx_power(&maxpow);
    uint8_t prim = 0;
    wifi_second_chan_t sec = WIFI_SECOND_CHAN_NONE;
    esp_wifi_get_channel(&prim, &sec);
    wifi_country_t ctry;
    memset(&ctry, 0, sizeof(ctry));
    esp_wifi_get_country(&ctry);
    Serial.printf(
        "[AP][diag] mode=%d ch=%d maxtxpow=%d(*0.25dBm=%.1fdBm) country=%c%c stations=%d\n",
        (int)m,
        prim,
        maxpow,
        maxpow * 0.25f,
        ctry.cc[0] ? ctry.cc[0] : '?',
        ctry.cc[1] ? ctry.cc[1] : '?',
        WiFi.softAPgetStationNum()
    );

    wifiConnected = true;
    return true;
}

void wifiDisconnect() {
    wifiTransitioning = true;
    
    WiFi.softAPdisconnect(true); // turn off AP mode
    vTaskDelay(10 / portTICK_PERIOD_MS);
    WiFi.disconnect(true, true); // turn off STA mode
    vTaskDelay(10 / portTICK_PERIOD_MS);
    WiFi.mode(WIFI_OFF);         // enforces WIFI_OFF mode
    vTaskDelay(10 / portTICK_PERIOD_MS);
    
    wifiConnected = false;
    wifiTransitioning = false;
}

bool wifiConnectMenu(wifi_mode_t mode) {
    if (WiFi.isConnected()) return false; // safeguard

    // Check if WiFi is in transition
    if (wifiTransitioning) {
        displayTextLine("WiFi busy, please wait...");
        vTaskDelay(500 / portTICK_PERIOD_MS);
        return false;
    }

    switch (mode) {
        case WIFI_AP: // access point
            WiFi.mode(WIFI_AP);
            return _setupAP();
            break;

        case WIFI_STA: { // station mode
            int nets;
            WiFi.mode(WIFI_MODE_STA);

            // wifiMACMenu();
            applyConfiguredMAC();

            bool refresh_scan = false;
            do {
#if defined(VARIONE_VEMO_UI)
                VariOneUI::showVemoStatus("Scanning WiFi");
#else
                displayTextLine("Scanning..");
#endif
                nets = WiFi.scanNetworks();
                options = {};
                for (int i = 0; i < nets; i++) {
                    if (options.size() < 250) {
                        String ssid = WiFi.SSID(i);
                        int encryptionType = WiFi.encryptionType(i);
                        int32_t rssi = WiFi.RSSI(i);
                        int32_t ch = WiFi.channel(i);
                        // Check if the network is secured
                        String encryptionPrefix = (encryptionType == WIFI_AUTH_OPEN) ? "" : "#";
                        String encryptionTypeStr;
                        switch (encryptionType) {
                            case WIFI_AUTH_OPEN: encryptionTypeStr = "Open"; break;
                            case WIFI_AUTH_WEP: encryptionTypeStr = "WEP"; break;
                            case WIFI_AUTH_WPA_PSK: encryptionTypeStr = "WPA/PSK"; break;
                            case WIFI_AUTH_WPA2_PSK: encryptionTypeStr = "WPA2/PSK"; break;
                            case WIFI_AUTH_WPA_WPA2_PSK: encryptionTypeStr = "WPA/WPA2/PSK"; break;
                            case WIFI_AUTH_WPA2_ENTERPRISE: encryptionTypeStr = "WPA2/Enterprise"; break;
                            default: encryptionTypeStr = "Unknown"; break;
                        }

#if defined(VARIONE_VEMO_UI)
                        String optionText = encryptionPrefix + ssid + " " +
                                            VariOneUI::rssiBars(rssi) + " " + encryptionTypeStr +
                                            " ch" + String(ch);
#else
                        String optionText = encryptionPrefix + ssid + "(" + String(rssi) + "|" +
                                            encryptionTypeStr + "|ch." + String(ch) + ")";
#endif

                        options.push_back({optionText.c_str(), [=]() {
                                               _wifiConnect(ssid, encryptionType);
                                           }});
                    }
                }
                options.push_back({"Hidden SSID", [=]() {
                                       String __ssid = keyboard("", 32, "Your SSID");
                                       _wifiConnect(__ssid.c_str(), 8);
                                   }});
                addOptionToMainMenu();

                loopOptions(options);
                options.clear();

                if (check(EscPress)) {
                    refresh_scan = true;
                } else {
                    refresh_scan = false;
                }
            } while (refresh_scan);
        } break;

        case WIFI_AP_STA: // repeater mode
                          // _setupRepeater();
            break;

        default: // error handling
            Serial.println("Unknown wifi mode: " + String(mode));
            break;
    }

    if (returnToMenu) {
        wifiDisconnect(); // Forced turning off the wifi module if exiting back to the menu
        return false;
    }
    return wifiConnected;
}

void wifiConnectTask(void *pvParameters) {
    if (WiFi.status() == WL_CONNECTED) return;

    // Check if WiFi is in transition
    if (wifiTransitioning) {
        vTaskDelete(NULL);
        return;
    }

    WiFi.mode(WIFI_MODE_STA);
    int nets = WiFi.scanNetworks();
    String ssid;
    String pwd;

    for (int i = 0; i < nets; i++) {
        ssid = WiFi.SSID(i);
        pwd = bruceConfig.getWifiPassword(ssid);
        if (pwd == "") continue;

        WiFi.begin(ssid, pwd);
        for (int i = 0; i < 50; i++) {
            if (WiFi.status() == WL_CONNECTED) {
                wifiConnected = true;
                wifiIP = WiFi.localIP().toString();

                // Start timezone update in background if not already running
                if (timezoneTaskHandle == NULL) {
                    xTaskCreate(updateTimezoneTask, "updateTimezone", 4096, NULL, 1, &timezoneTaskHandle);
                }
                drawStatusBar();
                break;
            }
            vTaskDelay(100 / portTICK_RATE_MS);
        }
    }
    WiFi.scanDelete();

    vTaskDelete(NULL);
    return;
}

String checkMAC() { return String(WiFi.macAddress()); }

bool wifiConnecttoKnownNet(void) {
    if (WiFi.isConnected()) return true; // safeguard
    
    // Check if WiFi is in transition
    if (wifiTransitioning) {
        displayTextLine("WiFi busy, please wait...");
        vTaskDelay(500 / portTICK_PERIOD_MS);
        return false;
    }
    
    bool result = false;
    int nets;
    // WiFi.mode(WIFI_MODE_STA);
#if defined(VARIONE_VEMO_UI)
    VariOneUI::showVemoStatus("Scanning WiFi");
#else
    displayTextLine("Scanning Networks..");
#endif
    WiFi.disconnect(true, true);
    vTaskDelay(10 / portTICK_PERIOD_MS);
    nets = WiFi.scanNetworks();
    for (int i = 0; i < nets; i++) {
        vTaskDelay(10 / portTICK_PERIOD_MS);
        String ssid = WiFi.SSID(i);
        String password = bruceConfig.getWifiPassword(ssid);
        if (password != "") {
            Serial.println("Connecting to: " + ssid);
            result = _connectToWifiNetwork(ssid, password);
        }
        // Maybe it finds a known network and can't connect, then try the next
        // until it gets connected (or not)
        if (result) {
            Serial.println("Connected to: " + ssid);
            break;
        }
    }
    if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        wifiIP = WiFi.localIP().toString();

        // Start timezone update in background if not already running
        if (timezoneTaskHandle == NULL) {
            xTaskCreate(updateTimezoneTask, "updateTimezone", 4096, NULL, 1, &timezoneTaskHandle);
        }
    }
    return result;
}

void updateTimezoneTask(void *pvParameters) {
    // Wait a bit for connection to stabilize before updating timezone
    vTaskDelay(5000 / portTICK_PERIOD_MS);

    // Only update timezone if WiFi is still connected
    if (WiFi.isConnected() && wifiConnected) { updateClockTimezone(); }

    // Clear the task handle before deleting
    timezoneTaskHandle = NULL;
    vTaskDelete(NULL);
}
