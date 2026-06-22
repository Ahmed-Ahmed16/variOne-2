/*
 * VariOne AI Debrief — on-device educational debrief for WiFi attacks.
 * Implements PRD §17 (VariLearn) demo-ready path: after an attack, the device
 * raises its own SoftAP + captive portal and serves a report (real session data
 * + canned lesson) reachable via a one-scan WiFi-join QR. No internet, no API.
 * The canned renderer is the seam where a future cloud/AI renderer drops in.
 */
#ifndef VARIONE_DEBRIEF_H
#define VARIONE_DEBRIEF_H

#include <Arduino.h>

enum DebriefType { DEBRIEF_DEAUTH, DEBRIEF_BEACON, DEBRIEF_EVIL_PORTAL, DEBRIEF_BADUSB };

// Plain facts captured by an attack. Each attack fills this at its exit and
// passes it to runDebrief(); no globals, no hot-loop hooks.
struct DebriefFacts {
    DebriefType type = DEBRIEF_DEAUTH;
    String target = "";    // SSID, or "All scanned APs"
    String bssid = "";     // optional target MAC
    String channels = "";  // e.g. "per-AP" / "1-11 hopping" / "6"
    uint32_t frames = 0;   // frames/packets sent
    uint32_t clients = 0;  // clients connected/affected
    uint32_t creds = 0;    // credentials captured (portal)
    uint32_t durationS = 0;
};

// Show a "Debrief?" prompt. If accepted, raise the SoftAP + captive portal,
// draw the join QR on the TFT, and serve the report until BACK. Tears the AP
// down on exit. MUST be called from a shallow stack (NOT nested inside an
// attack's deep call chain) — building the report + web server needs headroom.
void runDebrief(const DebriefFacts &facts);

// Lightweight "arm" calls for attacks to record what just happened WITHOUT
// touching the heavy debrief machinery on their (stack-tight) frames. Facts are
// stored in static storage; debriefRunPending() then runs the debrief later,
// from the menu, once the attack's stack has unwound.
void debriefArmDeauthFlood(uint32_t frames, uint32_t durationS, int apCount);
// Targeted single-AP deauth: record the specific SSID/BSSID/channel hit so the
// debrief names the real target instead of "All scanned APs".
void debriefArmDeauthTarget(
    const String &ssid, const String &bssid, uint8_t channel, uint32_t frames, uint32_t durationS
);
void debriefArmBeacon(const String &mode, uint32_t durationS);
// BadUSB/HID injection: scriptName is the file that ran; durationS how long it
// typed. Unified across every script — the lesson is about HID trust, not the
// specific payload.
void debriefArmBadUSB(const String &scriptName, uint32_t durationS);
// Evil/Vari portal: clone-AP + captive-portal harvest. clients connected, creds
// captured, the channel cloned on, whether deauth was layered, and whether the
// portal verified the typed password against the real AP (verifyMode).
void debriefArmEvilPortal(
    const String &apName, uint32_t clients, uint32_t creds, uint8_t channel, uint32_t durationS,
    bool deauthEnabled, bool verifyMode
);
void debriefRunPending();

#endif // VARIONE_DEBRIEF_H
