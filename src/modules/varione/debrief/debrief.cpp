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
        case DEBRIEF_EVIL_PORTAL: return "VariPortal (Cloned AP + Captive Portal)";
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
// The AI narrative is now styled HTML (headings/paragraphs/lists) emitted by the
// model, so we render it as-is rather than escaping it. Source is Gemini over a
// trusted local AP, but we still neutralize <script>/<style> as defense in depth
// and strip the markdown code fences models sometimes wrap output in.
static String sanitizeAiHtml(String s) {
    s.trim();
    if (s.startsWith("```")) {               // drop opening ```html / ``` fence line
        int nl = s.indexOf('\n');
        if (nl >= 0) s = s.substring(nl + 1);
    }
    if (s.endsWith("```")) s = s.substring(0, s.length() - 3);
    s.trim();
    s.replace("<script", "&lt;script");
    s.replace("</script", "&lt;/script");
    s.replace("<style", "&lt;style");
    s.replace("</style", "&lt;/style");
    return s;
}

// Severity badge derived deterministically from the attack type + real facts.
// label -> human word; cls -> CSS class that colors the pill (sev-crit/high/med).
static void severityOf(const DebriefFacts &f, String &label, String &cls) {
    switch (f.type) {
        case DEBRIEF_DEAUTH:
            label = "Disruption";
            cls = "sev-high";
            break;
        case DEBRIEF_BEACON:
            label = "Noise / Spoofing";
            cls = "sev-med";
            break;
        case DEBRIEF_EVIL_PORTAL:
            label = f.creds > 0 ? "Critical" : "High";
            cls = f.creds > 0 ? "sev-crit" : "sev-high";
            break;
        case DEBRIEF_BADUSB:
            label = "Critical";
            cls = "sev-crit";
            break;
        default:
            label = "Info";
            cls = "sev-med";
            break;
    }
}

// Inline Vemo mascot head as SVG — matches the on-device vector identity (white
// rounded head, navy bandit mask, cyan eyes + "V" muzzle). Self-contained so the
// captive-portal report needs no external asset or internet. `s` = px size.
static String vemoSvg(int s) {
    String w = String(s);
    return "<svg width='" + w + "' height='" + w + "' viewBox='0 0 100 100' "
           "xmlns='http://www.w3.org/2000/svg' aria-hidden='true'>"
           "<circle cx='22' cy='24' r='13' fill='#192a3a'/><circle cx='22' cy='24' r='6' fill='#fff'/>"
           "<circle cx='78' cy='24' r='13' fill='#192a3a'/><circle cx='78' cy='24' r='6' fill='#fff'/>"
           "<rect x='14' y='18' width='72' height='66' rx='20' fill='#fff' stroke='#192a3a' stroke-width='3'/>"
           "<rect x='20' y='38' width='60' height='22' rx='11' fill='#192a3a'/>"
           "<circle cx='37' cy='49' r='6' fill='#5fd0ff'/><circle cx='35' cy='47' r='2' fill='#fff'/>"
           "<circle cx='63' cy='49' r='6' fill='#5fd0ff'/><circle cx='61' cy='47' r='2' fill='#fff'/>"
           "<path d='M44 66 L50 74 L56 66' fill='none' stroke='#5fd0ff' stroke-width='3' "
           "stroke-linecap='round' stroke-linejoin='round'/></svg>";
}

// One session stat as a card: big value on top, small label under it.
static String statCard(const String &value, const String &label) {
    return "<div class='stat'><div class='sv'>" + value + "</div><div class='sl'>" + label +
           "</div></div>";
}

// aiText is optional: when non-empty an "AI analysis" section is rendered. The
// canned What/Why/Mitigations stays as the reliable backbone + offline fallback.
static String buildReportHtml(const DebriefFacts &f, const String &aiText = "") {
    String title = attackTitle(f.type);
    String sevLabel, sevCls;
    severityOf(f, sevLabel, sevCls);

    String h;
    h.reserve(8192);
    h += "<!doctype html><html lang='en'><head><meta charset='utf-8'>";
    h += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    h += "<title>VariOne Debrief</title><style>";
    // --- brand tokens ---
    h += ":root{--bg:#070b10;--panel:#101a25;--panel2:#0c141d;--bd:#1b2a3a;"
         "--tx:#e9f1f8;--mut:#8597aa;--cy:#5fd0ff;--cy2:#2bb7e6;--navy:#192a3a}";
    h += "*{box-sizing:border-box}";
    h += "body{font-family:-apple-system,Segoe UI,Roboto,Helvetica,sans-serif;margin:0;"
         "background:radial-gradient(1200px 600px at 50% -10%,#13202e 0%,var(--bg) 60%);"
         "color:var(--tx);-webkit-font-smoothing:antialiased}";
    h += ".wrap{max-width:720px;margin:0 auto;padding:18px 16px 40px}";
    // header / hero
    h += ".brand{display:flex;align-items:center;gap:6px;font-weight:700;letter-spacing:.5px;"
         "font-size:13px;color:var(--mut);text-transform:uppercase}";
    h += ".brand b{color:var(--cy)}";
    h += ".hero{display:flex;align-items:center;gap:14px;margin:14px 0 6px}";
    h += ".hero .vemo{flex:0 0 auto;filter:drop-shadow(0 4px 14px rgba(95,208,255,.25))}";
    h += "h1{font-size:22px;line-height:1.2;margin:0;font-weight:800}";
    h += ".pill{display:inline-block;margin-top:8px;padding:3px 10px;border-radius:999px;"
         "font-size:12px;font-weight:700;letter-spacing:.4px}";
    h += ".sev-crit{background:#3a0f17;color:#ff7a90;border:1px solid #5e1b29}";
    h += ".sev-high{background:#3a2410;color:#ffb15e;border:1px solid #5e3c1b}";
    h += ".sev-med{background:#11323a;color:#5fe0ff;border:1px solid #1b525e}";
    // stat grid
    h += ".stats{display:grid;grid-template-columns:repeat(auto-fit,minmax(110px,1fr));"
         "gap:10px;margin:16px 0}";
    h += ".stat{background:var(--panel);border:1px solid var(--bd);border-radius:12px;"
         "padding:12px 14px}";
    h += ".stat .sv{font-size:clamp(15px,4.4vw,20px);font-weight:800;color:var(--cy);"
         "line-height:1.2;overflow-wrap:break-word;hyphens:auto}";
    h += ".stat .sl{font-size:11px;color:var(--mut);text-transform:uppercase;letter-spacing:.5px;"
         "margin-top:3px}";
    // section cards
    h += ".card{background:var(--panel);border:1px solid var(--bd);border-radius:14px;"
         "padding:14px 16px;margin:12px 0}";
    h += ".card.accent{border-left:4px solid var(--cy)}";
    h += ".card.insight{background:linear-gradient(180deg,#10222e,var(--panel2));"
         "border-left:4px solid var(--cy)}";
    h += ".card.ai{border-left:4px solid #b78cff}";
    h += ".sec{display:flex;align-items:center;gap:8px;font-size:13px;font-weight:700;"
         "text-transform:uppercase;letter-spacing:.6px;color:var(--mut);margin:0 0 8px}";
    h += ".sec .ic{font-size:16px}";
    h += "p{font-size:15px;line-height:1.55;margin:0}";
    h += ".card .ai p,.card.ai *{font-size:15px;line-height:1.55}";
    // mitigation checklist
    h += "ul.checks{list-style:none;margin:0;padding:0}";
    h += "ul.checks li{position:relative;padding:7px 0 7px 28px;font-size:15px;line-height:1.5;"
         "border-bottom:1px solid var(--bd)}";
    h += "ul.checks li:last-child{border-bottom:0}";
    h += "ul.checks li:before{content:'\\2713';position:absolute;left:2px;top:7px;color:var(--cy);"
         "font-weight:800}";
    // replicate block
    h += ".repl{background:var(--panel2);border:1px dashed var(--bd);border-radius:10px;"
         "padding:12px 14px;font-size:14px;color:#cfe;line-height:1.5}";
    h += ".foot{margin-top:22px;font-size:12px;color:#6b7280;line-height:1.5;text-align:center}";
    h += "</style></head><body><div class='wrap'>";

    // --- hero header ---
    h += "<div class='brand'><b>VAR<span style='color:var(--cy2)'>I</span>ONE</b>"
         "&nbsp;&middot;&nbsp;Awareness Debrief</div>";
    h += "<div class='hero'><div class='vemo'>" + vemoSvg(64) + "</div><div>";
    h += "<h1>" + title + "</h1>";
    h += "<span class='pill " + sevCls + "'>" + sevLabel + "</span></div></div>";

    // --- session stat cards (real data) ---
    h += "<div class='stats'>";
    h += statCard(f.target.isEmpty() ? String("&mdash;") : f.target, "Target");
    if (!f.channels.isEmpty()) h += statCard(f.channels, "Channel(s)");
    if (f.type == DEBRIEF_EVIL_PORTAL) {
        h += statCard(String(f.clients), "Clients");
        h += statCard(String(f.creds), "Credentials");
    } else if (f.type == DEBRIEF_DEAUTH) {
        h += statCard(String(f.frames), "Frames sent");
    } else if (f.type == DEBRIEF_BADUSB) {
        h += statCard("USB HID", "Vector");
    }
    h += statCard(String(f.durationS) + "s", "Duration");
    if (!f.bssid.isEmpty()) h += statCard(f.bssid, "BSSID");
    h += "</div>";

    // --- session insight (fact-driven) ---
    h += "<div class='card insight'><div class='sec'><span class='ic'>&#9889;</span>"
         "Session insight</div><p>" + dynamicInsight(f) + "</p></div>";

    // --- AI analysis (optional) ---
    if (!aiText.isEmpty()) {
        h += "<div class='card ai'><div class='sec'><span class='ic'>&#129302;</span>"
             "AI analysis</div><div>" + sanitizeAiHtml(aiText) + "</div></div>";
    }

    // --- lesson backbone ---
    h += "<div class='card accent'><div class='sec'><span class='ic'>&#128225;</span>"
         "What happened</div><p>" + lessonWhat(f.type) + "</p></div>";
    h += "<div class='card accent'><div class='sec'><span class='ic'>&#128275;</span>"
         "Why it works</div><p>" + lessonWhy(f.type) + "</p></div>";
    h += "<div class='card'><div class='sec'><span class='ic'>&#128737;</span>"
         "Mitigations</div><ul class='checks'>" + lessonMitigations(f.type) + "</ul></div>";
    h += "<div class='card'><div class='sec'><span class='ic'>&#129514;</span>"
         "Replicate (authorized testing)</div><div class='repl'>" + lessonReplicate(f.type) +
         "</div></div>";

    // --- footer ---
    h += "<div class='foot'>Generated on-device by VariOne &middot; authorized lab/demo use only.<br>";
    if (!aiText.isEmpty())
        h += "AI analysis generated from aggregate session facts only &mdash; no captured data left "
             "the device. Lesson content is templated.";
    else
        h += "Lesson content is templated; the session insight is generated from your capture.";
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

void debriefArmDeauthTarget(
    const String &ssid, const String &bssid, uint8_t channel, uint32_t frames, uint32_t durationS
) {
    g_pending = DebriefFacts{};
    g_pending.type = DEBRIEF_DEAUTH;
    g_pending.target = ssid.isEmpty() ? String("(hidden AP)") : ssid;
    g_pending.bssid = bssid;
    g_pending.channels = String(channel);
    g_pending.frames = frames;
    g_pending.clients = 1;
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
            displayTextLine("Asking AI...");
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
