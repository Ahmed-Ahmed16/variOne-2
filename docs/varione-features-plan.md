# VariOne S3 — Feature & Declutter Plan

**Date:** 2026-06-12
**Baseline:** clean upstream Bruce + `varione-s3` board profile.
**Branch:** `og-bruce-s3` (off `main`). Known-good frozen at tag `og-bruce-s3-working` (commit `9ef06de`).
**Local backup of working firmware:** `backups/og-bruce-s3-working/`.

> Status: APPROVED for spec. This file is the handoff plan — revise freely or hand to another agent.

---

## 0. Operating rules (read first — do not break the working build)

- All feature work happens on a **new branch off the frozen tag**: `git checkout -b varione-features og-bruce-s3-working`.
- The known-good tag `og-bruce-s3-working` and `backups/og-bruce-s3-working/` are the revert points. Never force-push or delete them.
- **Declutter = hide from menus only** (reversible). Do NOT delete module source in this pass. Source-strip is a deliberate later pass, only after features are stable.
- Build: `pio run -e varione-s3`. Flash firmware: `pio run -e varione-s3 -t upload`. Flash filesystem: `pio run -e varione-s3 -t uploadfs`.
- The operator flashes manually unless they ask for upload. Monitor at 115200.
- One change at a time, rebuild + verify on hardware before stacking the next. The project has a history of "add one thing → everything breaks"; move in small reversible steps.
- Mascot/VariOne overlay is intentionally NOT present on this branch. This is stock Bruce UI. Do not re-introduce the overlay.

---

## 1. Priority order

| # | Workstream | Why this order |
|---|---|---|
| **0** | **Fix SoftAP broadcast** | Hard blocker. AI Debrief, WebUI, Evil Portal all depend on the ESP32 raising its own AP. Operator reports the AP "says open" but never broadcasts a visible SSID. |
| 1 | **AI Debrief** (centerpiece) | Primary ask. Depends on #0. |
| 2 | **WiFi fixes** — beacon channel hop, WebUI | Beacon already has hop code (bug, not feature). WebUI likely just needs `uploadfs`. |
| 3 | **RF rolling-code test** | Standalone, independent. |
| 4 | **Declutter** (hide menus) | Independent, low risk, can be done in parallel/last. |
| 5 | **BadUSB demo scripts on SD** | Later / nice-to-have. |

---

## 0. Priority #0 — SoftAP does not broadcast

**Symptom:** "Start WiFi AP" reports open, but no SSID appears on phones; QR/WebUI never connects.

**Prime hypothesis (concrete):** `src/core/wifi/wifi_common.cpp:115-116`
```cpp
WiFi.softAPConfig(AP_GATEWAY, AP_GATEWAY, IPAddress(255,255,255,0)); // called BEFORE softAP
WiFi.softAP(bruceConfig.wifiAp.ssid, bruceConfig.wifiAp.pwd, 6, 0, 4, false);
```
On current ESP32 Arduino/IDF (this build uses Arduino 3.3.6 / IDF 5.5), calling `softAPConfig()` **before** `softAP()` can leave the AP non-broadcasting. **Fix candidate:** call `WiFi.softAP(...)` first, then `WiFi.softAPConfig(...)`, then read `softAPIP()`. Re-test SSID visibility.

**Diagnosis steps (on hardware, serial @115200):**
1. Reproduce "Start WiFi AP", capture serial: confirm `WiFi.mode` is `WIFI_AP`/`WIFI_AP_STA`, `softAP()` return value, and the reported `softAPIP()`.
2. Apply the reorder fix. Rebuild, flash, re-scan for SSID on a phone.
3. If still failing, check: SSID non-empty (`bruceConfig.wifiAp.ssid`), channel valid (1–13; currently 6), and that a prior promiscuous/monitor mode (sniffer/deauth) isn't leaving the radio in a bad state — add an explicit `WiFi.mode(WIFI_OFF); delay; WiFi.mode(WIFI_AP);` reset before `softAP()`.
4. Verify TX power / brownout is not the cause (S3 clone power) — check for brownout resets in serial.

**Acceptance:** Phone sees the AP SSID, joins it, and reaches `http://192.168.4.1`. This unblocks #1 and #2.

---

## 1. AI Debrief (centerpiece)

**Goal:** After a WiFi attack, operator gets a "Debrief?" prompt → device serves an educational report over its own AP → phone reads it via a one-scan QR.

**Decisions (locked):**
- Report **hosted on the ESP32 itself** (own SoftAP + captive portal). No internet, no API.
- Wired for **deauth, beacon spam, evil portal** (generic engine, easy to extend).
- Report = **real session data + canned per-attack lesson** (what / why it works / mitigations / how to replicate for testing).
- **Canned only now**; AI/API is a later drop-in. `SessionFacts` JSON is the natural seam — no abstraction layer built now, but keep the report renderer reading from a single `SessionFacts` struct so a future API renderer slots in.
- Trigger: **auto-prompt on attack stop, AND saved to SD**, re-openable from a Debrief menu.
- Delivery: **open AP "VariOne-Debrief" + captive-portal auto-open** + **WiFi-join QR** on TFT (scan = join + report pops) + plain URL fallback shown.

**New module:** `src/modules/varione/debrief/`
- `debrief_session.h/.cpp` — `SessionFacts` struct + builders. Fields: `attackType` (enum: DEAUTH, BEACON, EVIL_PORTAL), `targetSsid`, `targetBssid`, `channels[]`, `framesSent`, `clientsHit`, `credsCaptured` (+ values for portal), `startTs`, `endTs`, `durationMs`.
- `debrief_report.h/.cpp` — renders `SessionFacts` → HTML. Holds the 3 canned lesson templates with `{{placeholders}}` for live data.
- `debrief_portal.cpp` — starts open SoftAP "VariOne-Debrief", DNS catch-all (captive), AsyncWebServer routes (`/`, `/debrief`, `/debrief/<id>`). **Reuse the proven pattern in `src/modules/wifi/evil_portal.cpp`** (it already does SoftAP + captive DNS + AsyncWebServer); do not invent a new server stack.
- Persistence: write `SessionFacts` to SD `/debrief/<ts>_<type>.json`. Debrief menu lists these, re-opens any.

**Hook points (where attacks populate SessionFacts on stop):**
- Deauth: `src/modules/wifi/deauther.cpp` (and `wifi_atks.cpp`).
- Beacon spam: `src/modules/wifi/wifi_atks.cpp` (`beaconAttack()`).
- Evil portal: `src/modules/wifi/evil_portal.cpp`.

**QR:** reuse `src/modules/others/qrcode_menu.cpp` to render a WiFi-join QR (`WIFI:T:nopass;S:VariOne-Debrief;;`).

**Acceptance:**
1. Run deauth → stop → "Debrief?" prompt → OK.
2. TFT shows QR + URL; "VariOne-Debrief" AP is visible and joinable.
3. Phone joins → report auto-opens (captive) showing real target/channel/frames/duration + the deauth lesson (what/why/mitigations/replicate).
4. Session saved to SD; re-openable from Debrief menu.
5. Same for beacon spam and evil portal.

**Future (not now):** swap canned renderer for an API renderer fed by the same `SessionFacts` JSON; optionally also generate the prompt string and save it to SD for inspection.

---

## 2. WiFi fixes

### 2a. Beacon spam channel hop
- **Finding:** hop code already exists — `src/modules/wifi/wifi_atks.cpp`: `channels[]={1..11}`, `nextChannel()` cycles + `esp_wifi_set_channel()`. The bug is in *when/whether* the radio channel actually advances during the beacon TX loop (packets may all go out on one channel even though the packet's channel byte at offset 82 changes).
- **Fix:** in `beaconAttack()`, ensure the radio channel is set (`esp_wifi_set_channel`) in lockstep with the packet's channel byte each burst, and that hop timing is slow enough for client devices to catch beacons on each channel. Verify on a phone WiFi list that random SSIDs appear across multiple channels.
- **Acceptance:** beacon-spam SSIDs visible on nearby devices regardless of the channel the victim is parked on.

### 2b. WebUI not working
- **Finding:** WebUI assets are served from LittleFS `/BruceWebUI/` (`src/core/wifi/webInterface.cpp`). Prime suspect: the **filesystem image was never flashed** (only firmware was). Fix: `pio run -e varione-s3 -t uploadfs`, confirm assets present.
- Also depends on #0 (AP must broadcast for AP-mode WebUI).
- **Acceptance:** "WebUi screen" → AP mode → phone joins → WebUI loads (not 404/blank).

---

## 3. RF rolling-code test (new)

- **New RF menu entry:** "Rolling Code Test".
- Flow: capture fob press #1 (CC1101 OOK raw), capture press #2, compare payloads.
- **Verdict on TFT:** `FIXED CODE — identical, insecure` vs `ROLLING CODE — changed each press, secure`, plus both hex payloads and match %.
- Reuse existing Bruce CC1101 capture path (`src/modules/rf/`, `Record RAW` / `Scan/copy` internals).
- Standalone, no QR/report in this pass. (Engine can later feed a debrief like WiFi.)
- **Acceptance:** two presses of a fixed-code remote read as FIXED; two presses of a rolling-code car key read as ROLLING.

---

## 4. Declutter — hide from menus (final decisions)

Edit the relevant `src/core/menu_items/*.cpp` to not register the hidden entries (or add them to `disabledMenus` defaults). Source stays; UI hides. Verify each menu still builds + opens after trimming.

### Top-level menus to REMOVE
GPS, LoRa, Ethernet, Clock, Connect, Apps (Applications), Scripts (JS interpreter), **FM radio**.
Files: `GpsMenu`, `LoRaMenu`, `EthernetMenu`, `ClockMenu`, `ConnectMenu`, `AppsMenu`, `ScriptsMenu`, `FMMenu` — unregister from the main menu list (`src/core/main_menu.cpp` / wherever `MainMenu` items are added).

### Top-level menus to KEEP
WiFi, Bluetooth (reduced), RF, RFID, IR, Files, Others (reduced), Config, NRF24 (kept for later use).

### WiFi (`WifiMenu.cpp`) — REMOVE the whole pivot cluster
Karma, Listen TCP, Client TCP, SOCKS4 Proxy, TelNET, SSH, ReverseShell, Responder, Wireguard, Brucegotchi, WiFi Pass Recovery, Change MAC.
**KEEP:** Start WiFi AP (fixed), Wifi Atks, Evil Portal, Sniffer, Connect to Wifi, AP info, basic config, Turn Off WiFi. (Scan Hosts: REMOVE.)

### Bluetooth (`BleMenu.cpp`) — KEEP BLE Scan + BLE Spam only
REMOVE: iBeacon, Bad BLE, BLE Keyboard, BLE Suite, Media Cmds, Ninebot.

### RF (`RFMenu.cpp`)
**KEEP:** Jammer Full, Listen, Record RAW, Scan/copy, Bruteforce, Custom SubGhz, **Spectrum** (the one graph), settings (RF Frequency/Module/RX/TX pins, Config). **+ NEW Rolling Code Test.**
**REMOVE:** Jammer Itmt, RSSI Spectrum, SquareWave Spec, Spectogram.

### RFID (`RFIDMenu.cpp`)
**KEEP:** Read tag, Read EMV, Scan tags, Add MIF Key, Load file, Erase data, config.
**REMOVE:** Read 125kHz, Amiibolink, Chameleon, PN532 BLE, PN532 UART, SRIX Tool, Write NDEF.

### IR — KEEP all
TV-B-Gone, IR jammer, universal remote / record / send.

### Others (`OthersMenu.cpp`)
**KEEP:** QRCodes, Megalodon, BadUSB (+ BadUSB & HID).
**REMOVE:** iButton, Microphone, Record (audio), Spectrum (audio FFT), USB Clicker, USB Keyboard, USB U2F.

### NRF24
KEEP as-is. Operator uses CC1101 and nRF24 **separately** (hardware conflict on shared SPI); nRF24 work resumes later once CC1101 demos are filmed and the CC1101 module is unplugged. Do not attempt simultaneous CC1101 + nRF24 in this pass.

---

## 5. BadUSB demo scripts (later)

- Add a few demo DuckyScript payloads to SD (`/BadUSB/` or Bruce's expected path) so BadUSB has content to run for the demo.
- Keep payloads benign/authorized-lab only (e.g., open a notepad and type an awareness message). Plan-only for now; implement after #0–#3.

---

## 6. Non-goals / do-NOT-do this pass

- No source deletion (declutter is menu-hide only).
- No mascot/VariOne overlay re-introduction.
- No simultaneous CC1101 + nRF24.
- No live API/network calls for the debrief (canned only).
- No partition/board-profile changes beyond what #0–#3 strictly require.
- Do not touch the `og-bruce-s3-working` tag or the `backups/` dir.

---

## 7. Suggested execution order (small, verifiable steps)

1. Branch `varione-features` off `og-bruce-s3-working`.
2. **#0 SoftAP fix** → verify SSID visible + `192.168.4.1` reachable. (Foundation.)
3. **#2b WebUI** (`uploadfs`) → verify WebUI loads. (Confirms AP path end-to-end.)
4. **#1 AI Debrief** deauth path end-to-end → then beacon + evil portal.
5. **#2a beacon channel hop** fix.
6. **#3 RF rolling-code test.**
7. **#4 declutter** (menu hides), verify each menu builds/opens.
8. **#5 BadUSB demo scripts** (optional).
9. Tag a new known-good (`varione-features-v1`) + back up firmware bin when stable.

Rebuild + hardware-verify after each numbered step. Commit per step with a clear message.
