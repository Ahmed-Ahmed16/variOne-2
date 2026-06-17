/*
 * VariOne AI Debrief — implementation. See debrief.h for responsibility.
 * Reuses Bruce's captive-portal pattern (src/modules/wifi/evil_portal.cpp) for
 * the SoftAP + DNS + AsyncWebServer stack, and the project QR lib for the join QR.
 */
#include "debrief.h"
#include "ai_debrief.h"
#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/sd_functions.h"
#include "core/wifi/webInterface.h" // shared port-80 server (webDebriefBegin/End)
#include "core/wifi/wifi_common.h"
#include <DNSServer.h>
#include <FS.h>
#include <SD.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <globals.h>
#include <qrcode.h>

// Report HTML, held at file scope so the serve loop references a stable buffer.
static String s_report;

// The report is served from the app's single shared AsyncWebServer (see
// webInterface ensureWebServer/webDebriefBegin) at /debrief. We do NOT run our
// own server here: a second port-80 owner is exactly what made AsyncTCP fail to
// rebind (`bind error -8`) and poisoned the WebUI on the next open. We only run
// the catch-all DNS so the join lands on the captive sheet.
static DNSServer s_dnsServer;

// ---- canned per-attack lesson content -------------------------------------

static String attackTitle(DebriefType t) {
    switch (t) {
        case DEBRIEF_DEAUTH: return "Wi-Fi Deauthentication Attack";
        case DEBRIEF_BEACON: return "Wi-Fi Beacon Spam";
        case DEBRIEF_EVIL_PORTAL: return "Evil Portal (Cloned AP + Captive Portal)";
        case DEBRIEF_BADUSB: return "USB HID Injection (BadUSB)";
    }
    return "Wi-Fi Attack";
}

static String lessonWhat(DebriefType t) {
    switch (t) {
        case DEBRIEF_DEAUTH:
            return "The device sent forged 802.11 deauthentication frames that appear to come "
                   "from the access point. Targeted clients were forced to disconnect.";
        case DEBRIEF_BEACON:
            return "The device broadcast many forged beacon frames advertising fake network "
                   "names (SSIDs) across multiple channels, so nearby devices listed networks "
                   "that do not really exist.";
        case DEBRIEF_EVIL_PORTAL:
            return "The device cloned an open access point and served a captive login page. "
                   "Any credentials a victim typed were captured locally.";
        case DEBRIEF_BADUSB:
            return "The device enumerated as an ordinary USB keyboard and, the moment it was "
                   "plugged in, typed a scripted sequence of keystrokes into the host by itself "
                   "at machine speed. No software exploit was used — just automated typing.";
    }
    return "";
}

static String lessonWhy(DebriefType t) {
    switch (t) {
        case DEBRIEF_DEAUTH:
            return "In WPA/WPA2, management frames (including deauth) are not authenticated. "
                   "Any radio in range can forge one, so the client cannot tell a real "
                   "disconnect from a spoofed one.";
        case DEBRIEF_BEACON:
            return "Beacon frames are unauthenticated and sent before any association. A client "
                   "will list whatever SSID it hears, so a name alone proves nothing about who "
                   "runs the network.";
        case DEBRIEF_EVIL_PORTAL:
            return "Users trust a familiar SSID name and a convincing login page. An open AP "
                   "needs no server authentication, so the victim has no way to verify the "
                   "portal is legitimate before typing credentials.";
        case DEBRIEF_BADUSB:
            return "Computers trust any USB keyboard implicitly — HID input has no "
                   "authentication. A device that claims to be a keyboard can issue commands "
                   "the instant it is connected, faster than a human and without any consent "
                   "prompt, as long as the session is unlocked.";
    }
    return "";
}

static String lessonMitigations(DebriefType t) {
    switch (t) {
        case DEBRIEF_DEAUTH:
            return "<li>Enable 802.11w Protected Management Frames (PMF).</li>"
                   "<li>Move to WPA3, which mandates PMF.</li>"
                   "<li>Use a WIDS/WIPS to detect deauth floods.</li>";
        case DEBRIEF_BEACON:
            return "<li>User awareness: never trust a network by its name alone.</li>"
                   "<li>Connect only to known SSIDs; forget open networks.</li>"
                   "<li>Enterprise WIDS detects abnormal beacon floods.</li>";
        case DEBRIEF_EVIL_PORTAL:
            return "<li>Never enter credentials on a captive portal.</li>"
                   "<li>Require HTTPS with certificate validation; watch for cert warnings.</li>"
                   "<li>Enable 2FA so captured passwords are not enough.</li>"
                   "<li>Prefer WPA2/WPA3-Enterprise over open Wi-Fi.</li>";
        case DEBRIEF_BADUSB:
            return "<li>Never plug in unknown or found USB devices.</li>"
                   "<li>Lock the screen whenever you step away — injection needs an unlocked session.</li>"
                   "<li>Use USB device control / HID allowlisting (USBGuard, group policy) to block new keyboards.</li>"
                   "<li>Disable USB ports you do not use; prefer data-blocker cables for charging.</li>";
    }
    return "";
}

static String lessonReplicate(DebriefType t) {
    switch (t) {
        case DEBRIEF_DEAUTH:
            return "Scan for APs, select a target, and send deauth frames on the target's "
                   "channel. Observe the client drop its connection in an authorized lab.";
        case DEBRIEF_BEACON:
            return "Run beacon spam with channel hopping enabled and watch the fake networks "
                   "appear in the Wi-Fi list of nearby test devices.";
        case DEBRIEF_EVIL_PORTAL:
            return "Clone the target SSID, serve the portal, optionally deauth to push clients "
                   "over, and review the captured submissions on the device.";
        case DEBRIEF_BADUSB:
            return "Write a Ducky/BadUSB script, load it on the device, and run it against an "
                   "authorized, unlocked test machine; watch it type the payload on its own.";
    }
    return "";
}

// ---- fact-driven dynamic insight (B3) --------------------------------------
// Build one sentence-or-two of analysis selected DETERMINISTICALLY from the
// real session facts (rate/duration/client buckets). Same attack reads the
// same; different attacks read differently. Stays fully on-device — no libs,
// no network — and sits at the same seam a future AI renderer would replace.

static String dynamicInsight(const DebriefFacts &f) {
    switch (f.type) {
        case DEBRIEF_DEAUTH: {
            float rate = f.durationS ? (float)f.frames / f.durationS : (float)f.frames;
            String pace = rate >= 200 ? "a heavy" : (rate >= 50 ? "a sustained" : "a light");
            String s = "This run pushed " + String(f.frames) + " deauth frames over " +
                       String(f.durationS) + " s — " + pace + " flood (~" + String((int)rate) +
                       " frames/s). ";
            if (rate >= 200)
                s += "At this pace clients drop almost immediately and struggle to re-associate.";
            else if (rate >= 50) s += "Targeted clients see repeated disconnects across the run.";
            else s += "Even this modest rate is enough to knock a client off briefly.";
            return s;
        }
        case DEBRIEF_BEACON: {
            String where = f.channels.isEmpty() ? String("multiple channels") : f.channels;
            String s = "Over " + String(f.durationS) + " s of " + f.target + " across " + where +
                       ", nearby devices filled their Wi-Fi lists with networks that do not exist. ";
            s += f.durationS >= 60
                     ? "Sustained spam like this makes the real network hard to pick out."
                     : "Even a short burst is enough to clutter the scan list.";
            return s;
        }
        case DEBRIEF_EVIL_PORTAL: {
            String s = String(f.clients) + (f.clients == 1 ? " client" : " clients") +
                       " connected and " + String(f.creds) +
                       (f.creds == 1 ? " credential was" : " credentials were") + " captured";
            if (f.durationS) s += " over " + String(f.durationS) + " s";
            s += ". ";
            if (f.creds > 0) s += "Each capture is a real account a defender would have to rotate.";
            else if (f.clients > 0)
                s += "Clients joined but did not submit — a good sign awareness is holding.";
            else s += "No clients joined this run; in an authorized test, a deauth nudge pushes them over.";
            return s;
        }
        case DEBRIEF_BADUSB: {
            String s = "In " + String(f.durationS) +
                       " s the device typed a full keystroke payload into the host at machine "
                       "speed — no click, no confirmation, faster than any human. ";
            s += "In that same window a real payload could open a shell, pull files, or install "
                 "persistence. The only thing it needed was an unlocked, trusting USB port.";
            return s;
        }
    }
    return "";
}

// ---- report builder -------------------------------------------------------

// Escape AI text for safe embedding in the report body, and turn blank lines /
// newlines into paragraph breaks. The AI output is plain prose (we ask for no
// markup), but we never trust it raw.
static String htmlEscapeAi(const String &in) {
    String out;
    out.reserve(in.length() + 32);
    for (size_t i = 0; i < in.length(); i++) {
        char c = in[i];
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '\n': out += "<br>"; break;
            case '\r': break;
            default: out += c;
        }
    }
    return out;
}

// aiText is optional: when non-empty an "AI analysis" section is rendered. The
// canned What/Why/Mitigations stays as the reliable backbone + offline fallback.
static String buildReportHtml(const DebriefFacts &f, const String &aiText = "") {
    String title = attackTitle(f.type);
    String h;
    h.reserve(4096);
    h += "<!doctype html><html><head><meta charset='utf-8'>";
    h += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    h += "<title>VariOne Debrief</title><style>";
    h += "body{font-family:-apple-system,Segoe UI,Roboto,sans-serif;margin:0;background:#0f1115;color:#e8eaed}";
    h += ".wrap{max-width:680px;margin:0 auto;padding:18px}";
    h += "h1{font-size:20px;margin:8px 0;color:#4ea1ff}h2{font-size:16px;margin:18px 0 6px;color:#9ad}";
    h += ".tag{display:inline-block;background:#1d2330;border:1px solid #2c3650;border-radius:6px;padding:2px 8px;font-size:12px;color:#9fb}";
    h += "table{width:100%;border-collapse:collapse;margin:8px 0;font-size:14px}";
    h += "td{padding:6px 8px;border-bottom:1px solid #222a38}td:first-child{color:#8a93a6;width:40%}";
    h += "p,li{font-size:14px;line-height:1.5}ul{margin:6px 0 6px 18px}";
    h += ".foot{margin-top:22px;font-size:12px;color:#6b7280}";
    h += "</style></head><body><div class='wrap'>";
    h += "<span class='tag'>VariOne Debrief</span>";
    h += "<h1>" + title + "</h1>";

    // Session facts (real data)
    h += "<h2>Session</h2><table>";
    h += "<tr><td>Target</td><td>" + (f.target.isEmpty() ? String("-") : f.target) + "</td></tr>";
    if (!f.bssid.isEmpty()) h += "<tr><td>BSSID</td><td>" + f.bssid + "</td></tr>";
    if (!f.channels.isEmpty()) h += "<tr><td>Channel(s)</td><td>" + f.channels + "</td></tr>";
    if (f.type == DEBRIEF_EVIL_PORTAL) {
        h += "<tr><td>Clients</td><td>" + String(f.clients) + "</td></tr>";
        h += "<tr><td>Credentials captured</td><td>" + String(f.creds) + "</td></tr>";
    } else if (f.type == DEBRIEF_DEAUTH) {
        h += "<tr><td>Frames sent</td><td>" + String(f.frames) + "</td></tr>";
    } else if (f.type == DEBRIEF_BADUSB) {
        h += "<tr><td>Vector</td><td>USB keyboard (HID)</td></tr>";
    }
    h += "<tr><td>Duration</td><td>" + String(f.durationS) + " s</td></tr>";
    h += "</table>";

    h += "<h2>Session insight</h2><p>" + dynamicInsight(f) + "</p>";
    if (!aiText.isEmpty()) {
        h += "<h2>AI analysis</h2><p>" + htmlEscapeAi(aiText) + "</p>";
    }
    h += "<h2>What happened</h2><p>" + lessonWhat(f.type) + "</p>";
    h += "<h2>Why it works</h2><p>" + lessonWhy(f.type) + "</p>";
    h += "<h2>Mitigations</h2><ul>" + lessonMitigations(f.type) + "</ul>";
    h += "<h2>Replicate (authorized testing)</h2><p>" + lessonReplicate(f.type) + "</p>";

    h += "<div class='foot'>Generated on-device by VariOne. Authorized lab/demo use only. ";
    if (!aiText.isEmpty())
        h += "(The AI analysis was generated by Gemini from aggregate session facts only — no "
             "captured data left the device. Lesson content is templated.)";
    else
        h += "(Lesson content is templated; the session insight is generated from your capture.)";
    h += "</div>";
    h += "</div></body></html>";
    return h;
}

// ---- captive portal + serving loop ----------------------------------------

static void serveReportLoop() {
    IPAddress apIP(192, 168, 4, 1);

    // Bring the AP up via the SAME proven path as "Start WiFi AP"
    // (_setupAP in wifi_common.cpp), which broadcasts and accepts connections
    // reliably. The debrief runs right after an attack that tears the WiFi
    // driver down (ESP_ERR_WIFI_NOT_INIT), so do a full clean teardown first
    // to reach the same clean state the WiFi menu has.
    esp_wifi_set_promiscuous(false);
    wifiDisconnect(); // softAPdisconnect + disconnect + WIFI_OFF
    delay(400);

    // Force the debrief AP OPEN (no password) so the one-scan QR join can never
    // fail on auth. Blank the AP password in-memory only (no saveFile) around
    // _setupAP(), then restore it so "Start WiFi AP" keeps its configured pwd.
    String savedPwd = bruceConfig.wifiAp.pwd;
    bruceConfig.wifiAp.pwd = "";
    bool ok = _setupAP();
    bruceConfig.wifiAp.pwd = savedPwd;
    if (!ok) {
        displayError("Debrief AP failed");
        delay(1500);
        return;
    }
    apIP = WiFi.softAPIP();

    String apSsid = bruceConfig.wifiAp.ssid;
    if (apSsid.isEmpty()) apSsid = "VariOne";
    Serial.println("[DEBRIEF][diag] OPEN AP up via _setupAP ip=" + apIP.toString());

    s_dnsServer.start(53, "*", apIP); // catch-all DNS -> captive portal

    webDebriefBegin(s_report); // arm /debrief on the shared port-80 server

    // Draw the WiFi-join QR (scan -> join OPEN AP -> captive pops the report).
    String joinQr = String("WIFI:T:nopass;S:") + apSsid + ";;";
#ifdef HAS_SCREEN
    QRcode qrcode(&tft);
    qrcode.init();
    qrcode.create(joinQr);
    tft.setTextColor(TFT_WHITE, bruceConfig.bgColor);
    tft.setTextSize(1);
    tft.setCursor(2, tftHeight - 8);
    tft.print("Join " + apSsid);
#endif

    Serial.println("[DEBRIEF] AP '" + apSsid + "' up at " + apIP.toString());

    // We drive DNS + HTTP inline and watch for BACK. Drain any stale/held/
    // bouncing BACK from the preceding attack or prompt for ~800 ms so it can't
    // tear the AP down on frame 1 of the watch loop below.
    uint32_t drainEnd = millis() + 800;
    while (millis() < drainEnd) {
        check(EscPress); // consume and ignore
        delay(20);
    }
    uint32_t lastDiag = millis();
    int lastStations = -1;
    while (!check(EscPress)) {
        s_dnsServer.processNextRequest();
        int st = WiFi.softAPgetStationNum();
        if (st != lastStations || millis() - lastDiag > 3000) {
            Serial.printf("[DEBRIEF][diag] stations=%d heap=%u\n", st, (unsigned)ESP.getFreeHeap());
            lastStations = st;
            lastDiag = millis();
        }
        delay(5);
    }
    Serial.println("[DEBRIEF] serve loop exit (BACK)");

    webDebriefEnd(); // stop serving /debrief; shared server stays up for WebUI
    s_dnsServer.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
#ifdef HAS_SCREEN
    tft.fillScreen(bruceConfig.bgColor);
#endif
}

// ---- best-effort SD persistence -------------------------------------------

static void saveSessionToSd(const DebriefFacts &f) {
    if (!sdcardMounted) return;
    if (!SD.exists("/debrief")) SD.mkdir("/debrief");
    String name = "/debrief/" + String(millis()) + "_" + String((int)f.type) + ".html";
    File file = SD.open(name.c_str(), FILE_WRITE);
    if (!file) return;
    file.print(s_report);
    file.close();
    Serial.println("[DEBRIEF] saved " + name);
}

// ---- public entry ---------------------------------------------------------

// Facts armed by an attack, run later from the menu (shallow stack).
static DebriefFacts g_pending;
static bool g_pendingValid = false;

void debriefArmDeauthFlood(uint32_t frames, uint32_t durationS, int apCount) {
    g_pending = DebriefFacts{};
    g_pending.type = DEBRIEF_DEAUTH;
    g_pending.target = "All scanned APs (" + String(apCount) + ")";
    g_pending.channels = "per-AP";
    g_pending.frames = frames;
    g_pending.durationS = durationS;
    g_pendingValid = true;
}

void debriefArmBeacon(const String &mode, uint32_t durationS) {
    g_pending = DebriefFacts{};
    g_pending.type = DEBRIEF_BEACON;
    g_pending.target = mode.isEmpty() ? String("Beacon spam") : mode;
    g_pending.channels = "1-11 (hopping)";
    g_pending.durationS = durationS;
    g_pendingValid = true;
}

void debriefArmBadUSB(const String &scriptName, uint32_t durationS) {
    g_pending = DebriefFacts{};
    g_pending.type = DEBRIEF_BADUSB;
    // Show just the file name, not the full SD path.
    int slash = scriptName.lastIndexOf('/');
    g_pending.target = slash >= 0 ? scriptName.substring(slash + 1) : scriptName;
    if (g_pending.target.isEmpty()) g_pending.target = "BadUSB script";
    g_pending.durationS = durationS;
    g_pendingValid = true;
}

void debriefRunPending() {
    if (!g_pendingValid) return;
    g_pendingValid = false;
    runDebrief(g_pending);
}

void runDebrief(const DebriefFacts &facts) {
    // An attack may have exited by setting returnToMenu; clear it so the prompt
    // below actually renders instead of being unwound immediately.
    returnToMenu = false;

    // Auto-prompt: Debrief?  (BACK cancels via returnToMenu, leaving want=false)
    bool want = false;
    options = {
        {"Debrief this attack", [&]() { want = true; }},
        {"Skip", [&]() { want = false; }},
    };
    loopOptions(options);
    options.clear();
    if (!want) return;

    // ── Phase 1 (STA): fetch the AI narrative, if AI debrief is enabled ──────
    // Two-phase by design: STA for internet now, AP for serving later. Never
    // AP+STA at once. Any failure here leaves aiText empty → canned fallback.
    String aiText;
    if (bruceConfig.aiDebriefEnabled) {
        displayTextLine("Connecting WiFi for AI...");
        if (wifiConnecttoKnownNet()) {
            displayTextLine("Asking Gemini...");
            aiText = aiGenerateDebrief(facts);
        } else {
            Serial.println("[DEBRIEF] no known WiFi — offline debrief");
            displayTextLine("No internet - offline debrief");
            delay(800);
        }
        wifiDisconnect(); // tear STA fully down before the AP phase
        delay(300);
    }

    // ── Phase 2 (AP): build report (aiText optional) and serve over SoftAP ───
    s_report = buildReportHtml(facts, aiText);
    saveSessionToSd(facts);

    displayTextLine("Starting Debrief AP...");
    delay(200);
    serveReportLoop();
}
