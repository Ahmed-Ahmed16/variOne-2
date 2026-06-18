/*
 * VariOne AI Debrief — implementation. See ai_debrief.h for responsibility and
 * the security constraint (aggregate facts only — never secrets).
 */
#include "ai_debrief.h"
#include "core/wifi/wifi_common.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <globals.h> // bruceConfig

// ---- Gemini cloud endpoint -------------------------------------------------
static const char *GEMINI_HOST = "generativelanguage.googleapis.com";
// Key is sent via the x-goog-api-key header (required by the new "auth keys");
// the legacy ?key= query param is not used.
static const char *GEMINI_PATH = "/v1beta/models/gemini-2.0-flash:generateContent";

// Shared system instruction — pins tone for every attack template.
static const char *SYSTEM_TONE =
    "You are a friendly security-awareness coach writing a short post-exercise "
    "debrief for a NON-TECHNICAL audience after an AUTHORIZED lab demo. Explain in "
    "plain, everyday language what just happened, why the trick worked, and what an "
    "ordinary person should do to stay safe. Avoid heavy jargon; when a technical "
    "term is unavoidable, explain it in one short clause. Format the answer as "
    "clean, simple HTML: a couple of <h3> section headings, <p> paragraphs, "
    "<strong> for key terms, and one <ul><li> list of practical safety tips. Do "
    "NOT wrap the output in markdown code fences or a <html>/<body> wrapper. Do not "
    "invent facts beyond those given.";

// ---- per-attack prompt templates -------------------------------------------
// Aggregate facts only. No credentials, captures or PANs are ever interpolated.

static String buildPrompt(const DebriefFacts &f) {
    String p = String(SYSTEM_TONE) + "\n\n";
    switch (f.type) {
        case DEBRIEF_DEAUTH:
            p += "Attack performed: Wi-Fi deauthentication flood (forged 802.11 "
                 "deauth management frames).\n";
            p += "Target: " + (f.target.isEmpty() ? String("nearby access points") : f.target) + "\n";
            if (!f.bssid.isEmpty()) p += "Target BSSID (router MAC): " + f.bssid + "\n";
            p += "Channel(s): " + (f.channels.isEmpty() ? String("target channel") : f.channels) + "\n";
            p += "Deauth frames sent: " + String(f.frames) + "\n";
            if (f.clients > 0) p += "Clients knocked off: " + String(f.clients) + "\n";
            p += "Duration: " + String(f.durationS) + " seconds\n";
            p += "Explain how forged deauth frames knock clients off, why management "
                 "frames are unauthenticated in WPA/WPA2, and how 802.11w/WPA3 and "
                 "WIDS defend against it.";
            break;
        case DEBRIEF_BEACON:
            p += "Attack performed: Wi-Fi beacon spam (flooding forged beacon frames "
                 "advertising fake SSIDs).\n";
            p += "Mode/label: " + (f.target.isEmpty() ? String("beacon spam") : f.target) + "\n";
            p += "Channel(s): " + (f.channels.isEmpty() ? String("multiple, hopping") : f.channels) + "\n";
            p += "Duration: " + String(f.durationS) + " seconds\n";
            p += "Explain why nearby devices list networks that do not exist, why beacon "
                 "frames are unauthenticated and sent before association, and why an SSID "
                 "name alone proves nothing about who runs a network.";
            break;
        case DEBRIEF_EVIL_PORTAL:
            p += "Attack performed: VariPortal (cloned open AP + captive login page).\n";
            p += "Cloned SSID: " + (f.target.isEmpty() ? String("a familiar network") : f.target) + "\n";
            if (!f.bssid.isEmpty()) p += "Rogue AP BSSID (MAC): " + f.bssid + "\n";
            p += "Clients connected: " + String(f.clients) + "\n";
            p += "Credentials captured (count only): " + String(f.creds) + "\n";
            p += "Duration: " + String(f.durationS) + " seconds\n";
            p += "Explain why users trust a familiar SSID and a convincing portal, why an "
                 "open AP needs no server authentication so the victim cannot verify it, and "
                 "how HTTPS with cert validation, 2FA and WPA2/WPA3-Enterprise defend against it. "
                 "Do not ask for or reference the captured credential contents — only the count is known.";
            break;
        case DEBRIEF_BADUSB:
            p += "Attack performed: USB HID injection (BadUSB) — the device emulated an "
                 "ordinary keyboard and auto-typed a scripted payload into the host at machine speed.\n";
            p += "Target host: " + (f.target.isEmpty() ? String("an unlocked test machine") : f.target) + "\n";
            p += "Duration: " + String(f.durationS) + " seconds\n";
            p += "Explain why computers trust any USB keyboard (HID input is unauthenticated), why "
                 "injection fires the instant it is plugged into an unlocked session and runs faster "
                 "than a human, and how USB device control / HID allowlisting, locking the screen, and "
                 "disabling unused ports defend against it. No software exploit was used — only typing.";
            break;
    }
    return p;
}

// ---- HTTPS POST to Gemini --------------------------------------------------
// Cloud-only this pass. The aiEndpoint provider seam is intentionally a stub:
// when set, the local-LAN POST path is DEFERRED (plan Part E) and we bail to the
// canned fallback rather than guessing a response shape.
// Tolerant extractor: pull the narrative from whatever the backend returned.
// Handles Gemini (candidates[0].content.parts[0].text), an Ollama /api/generate
// reply ("response"), a simple proxy ("text"), or a raw plain-text body.
static String parseAiText(const String &payload) {
    JsonDocument doc;
    if (deserializeJson(doc, payload) == DeserializationError::Ok) {
        const char *t = doc["candidates"][0]["content"]["parts"][0]["text"];
        if (t) return String(t);
        const char *r = doc["response"]; // Ollama
        if (r) return String(r);
        const char *x = doc["text"]; // generic proxy
        if (x) return String(x);
        Serial.println("[AI] JSON reply had no known text field");
        return "";
    }
    // Not JSON → treat the whole body as the narrative (proxy may return text/plain).
    String s = payload;
    s.trim();
    return s;
}

// ---- cloud (Gemini over HTTPS) --------------------------------------------
static String geminiCloudGenerate(const String &prompt) {
    if (bruceConfig.geminiApiKey.isEmpty()) {
        Serial.println("[AI] no Gemini API key configured");
        return "";
    }

    // Gemini request body: {"contents":[{"parts":[{"text":"<prompt>"}]}]}
    JsonDocument reqDoc;
    reqDoc["contents"][0]["parts"][0]["text"] = prompt;
    String body;
    serializeJson(reqDoc, body);

    // Reclaim the WiFi scan results left by wifiConnecttoKnownNet, then report
    // memory (TLS to Google needs large buffers — served from PSRAM when enabled).
    WiFi.scanDelete();
    Serial.printf(
        "[AI] mem before TLS: free=%u largest=%u internal=%u psram=%u\n",
        (unsigned)ESP.getFreeHeap(),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        (unsigned)ESP.getFreePsram()
    );

    String url = String("https://") + GEMINI_HOST + GEMINI_PATH;

    String payload;
    int status = -1;
    for (int attempt = 1; attempt <= 3 && status != 200; attempt++) {
        WiFiClientSecure client;
        client.setInsecure(); // same trust model as gps/wigle.cpp
        HTTPClient https;
        https.setTimeout(20000); // ms
        if (!https.begin(client, url)) {
            Serial.printf("[AI] attempt %d: https.begin() failed\n", attempt);
            delay(300);
            continue;
        }
        https.addHeader("Content-Type", "application/json");
        https.addHeader("x-goog-api-key", bruceConfig.geminiApiKey);
        status = https.POST(body);
        if (status == 200) {
            payload = https.getString();
        } else {
            Serial.printf("[AI] attempt %d: Gemini HTTP %d (heap=%u)\n", attempt, status, (unsigned)ESP.getFreeHeap());
            String err = https.getString();
            if (err.length()) Serial.println("[AI] body: " + err.substring(0, 200));
        }
        https.end();
        if (status != 200) delay(400);
    }
    if (status != 200) return "";
    return parseAiText(payload);
}

// ---- local LAN (plain HTTP to aiEndpoint) ---------------------------------
// No TLS → no mbedTLS buffers → works in the tight internal heap. The endpoint
// is a LAN box (a proxy that forwards to Gemini, or an Ollama-style server).
// Body sent: {"prompt":"<prompt>"}. Reply parsed by parseAiText (flexible).
static String localGenerate(const String &prompt) {
    String url = bruceConfig.aiEndpoint;
    // Normalize the scheme to lowercase — phone keyboards often capitalize it to
    // "Http://", which HTTPClient::begin() refuses to parse (begin() fails).
    int sep = url.indexOf("://");
    if (sep > 0) {
        String scheme = url.substring(0, sep);
        scheme.toLowerCase();
        url = scheme + url.substring(sep);
    }
    JsonDocument reqDoc;
    reqDoc["prompt"] = prompt;
    String body;
    serializeJson(reqDoc, body);

    Serial.println("[AI] local endpoint POST -> " + url);
    WiFiClient plain;
    WiFiClientSecure tls;
    HTTPClient http;
    http.setTimeout(30000); // local models can be slow on CPU
    bool ok;
    if (url.startsWith("https://")) {
        tls.setInsecure();
        ok = http.begin(tls, url);
    } else {
        ok = http.begin(plain, url);
    }
    if (!ok) {
        Serial.println("[AI] local http.begin() failed");
        return "";
    }
    http.addHeader("Content-Type", "application/json");
    int status = http.POST(body);
    if (status != 200) {
        Serial.printf("[AI] local endpoint HTTP %d\n", status);
        http.end();
        return "";
    }
    String payload = http.getString();
    http.end();
    return parseAiText(payload);
}

// ---- backend dispatch ------------------------------------------------------
// aiEndpoint set → local LAN (plain HTTP, no TLS); empty → cloud Gemini (HTTPS).
static String backendGenerate(const String &prompt) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[AI] STA not connected — cannot reach AI backend");
        return "";
    }
    if (!bruceConfig.aiEndpoint.isEmpty()) return localGenerate(prompt);
    return geminiCloudGenerate(prompt);
}

// ---- public API ------------------------------------------------------------

String aiGenerateDebrief(const DebriefFacts &f) {
    if (!bruceConfig.aiDebriefEnabled) {
        Serial.println("[AI] aiDebriefEnabled=0 — skipping AI, using canned report");
        return "";
    }
    String prompt = buildPrompt(f);
    Serial.println("[AI] requesting AI debrief (" + String(prompt.length()) + " char prompt)");
    String text = backendGenerate(prompt);
    if (text.isEmpty()) {
        Serial.println("[AI] empty result — caller falls back to canned report");
    } else {
        Serial.println("[AI] got " + String(text.length()) + " char narrative");
    }
    return text;
}

bool aiDebriefSmokeTest() {
    Serial.println("[AI][smoke] connecting to a known WiFi network...");
    if (WiFi.status() != WL_CONNECTED && !wifiConnecttoKnownNet()) {
        Serial.println("[AI][smoke] FAILED: no known network connected");
        return false;
    }
    Serial.println("[AI][smoke] STA connected: " + WiFi.localIP().toString());

    DebriefFacts f;
    f.type = DEBRIEF_DEAUTH;
    f.target = "Lab-AP";
    f.channels = "6";
    f.frames = 5000;
    f.durationS = 30;

    int savedEnabled = bruceConfig.aiDebriefEnabled;
    bruceConfig.aiDebriefEnabled = 1; // force on for the test only (no save)
    String text = aiGenerateDebrief(f);
    bruceConfig.aiDebriefEnabled = savedEnabled;

    if (text.isEmpty()) {
        Serial.println("[AI][smoke] FAILED: no narrative returned");
        wifiDisconnect();
        return false;
    }
    Serial.println("[AI][smoke] SUCCESS, narrative:\n" + text);
    wifiDisconnect();
    return true;
}
