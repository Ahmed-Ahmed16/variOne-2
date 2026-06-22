# VariOne — Demo Hardening Test Checklist

Verifies every change made for the demo. Run top-to-bottom on the **real device**.
Follows `docs/DEMO_RUNBOOK.md`. `[ ]` = pass on hardware.

> ⚠️ **Rebuild required.** The EMV summary (B1), `UP_BTN 40→47`, and `R_BTN 2→41`
> landed **after** the last green build. Rebuild before flashing:
> ```
> pio run -e varione-s3        # must be GREEN
> pio run -e varione-s3 -t upload
> ```
> Monitor at **115200**. (A one-time `pio run -t erase` clears stale NVS — pre-demo only.)

---

## 0 · Build & boot
- [ ] `pio run -e varione-s3` compiles green (no warnings on the new EMV/pin code).
- [ ] Flashes; boots clean; SD mounts; auto-joins strongest known WiFi (`STA connected`).

## 1 · A1 — VariOne AP is OPEN everywhere (the real fix)
- [ ] **Forget** any old "VariOne" network on every test phone first.
- [ ] **Start WiFi AP** → SSID `VariOne` appears **< 10 s**, shows **OPEN** (no lock icon).
- [ ] Phone joins → gets an IP, serial `stations 0 → 1` (no "couldn't connect").
- [ ] **Debrief** AP is the **same OPEN `VariOne`** — switch debrief ↔ Start-WiFi-AP
      repeatedly: **no WPA2/open flip-flop**, phone keeps joining.
- [ ] QR "VariOne AP" scans → **one-tap join** to the open net (nopass QR).
- [ ] WebUI AP screen reads **"VariOne (open)"** (not "VariOne/varione1").

## 2 · Button pin remap (UP=47, RIGHT=41)
- [ ] **UP (GPIO47)** scrolls up. **RIGHT (GPIO41)** = next / value +.
- [ ] LEFT 39 · DOWN 42 · OK/SEL 45 · BACK 46 unchanged; BACK short = ESC, long = exit.
- [ ] No phantom/stuck presses on 47 or 41 (solder-joint check if a button reads held).

## 3 · B1 — EMV (full PAN on screen, last-4 on serial)
- [ ] **Readable own card:** screen shows Vendor, **full PAN**, expiry, **`Status: READ OK`**.
- [ ] Serial: `>> EMV: <vendor> card - PAN ****<last4>, exp MM/YY - READ OK`
      (**only last-4 on the wire** — full PAN never leaves over serial/USB mirror).
- [ ] **PDOL-gap / unreadable card:** screen `Unknown PAN` + `Status: PARTIAL (PDOL gap)`;
      serial `PAN ****----`.
- [ ] **Failed read:** `Status: UNKNOWN X`.
- [ ] **Save** still writes the scan file.
- [ ] (Policy) only own/issued test cards shown on camera/projector.

## 4 · B2 — VariPortal rename
- [ ] WebUI **WiFi-warning banner FIRES** on the VariPortal screen (the `index.js:904`
      fix — open WebUI navigator/spectator, enter VariPortal, banner appears).
- [ ] Serial prints `VariPortal output file:` (not "Evil Portal output file:").
- [ ] No user-facing "Evil Portal" text anywhere on screen.

## 5 · B3 — Plain-English serial narration (all `>>`, @115200)
- [ ] **Deauth:** `>> DEAUTH: target <mac> on chN - X fps, N frames total` (~1/s),
      then `>> DEAUTH stopped: N frames total`.
- [ ] **VariPortal:** `>> VARIPORTAL: creds captured (entry #N) on '<ap>'` —
      and **no captured username/password is printed** (event + count only).
- [ ] **NRF:** `>> NRF: jamming <mode> - ch N` on mode entry (not spamming per-hop).
- [ ] **BLE:** `>> BLE: iBeacon spam advertising - N bursts` … `>> BLE: spam stopped`.
- [ ] **IR:** `>> IR: captured <proto> addr=0x.. cmd=0x.. (N-bit)` on capture.

## 6 · B4 — NRF jammer menu trim
- [ ] Jam scroll list **hides**: Video/FPV, RC Ctrl, Zigbee, Drone.
- [ ] Still **visible & launchable**: Full Spectrum, WiFi Degrade, BLE Link Kill,
      BLE Discovery Kill, Bluetooth Kill, Mouse/KB Kill, **Single CH**, **CH Hopper**.
- [ ] Each visible mode launches the correct jammer (no wrong-mode / reindex bug).

## 7 · Phone Probes hidden
- [ ] WiFi menu **no longer lists "Phone Probes"**.
- [ ] Other WiFi items intact (Wifi Atks, VariPortal, Sniffer, Channel Graph, Scan Hosts…).

## 8 · E — USB-serial screen mirror
- [ ] Open `tools/usb-serial-mirror/varione-mirror.html` in **Chrome/Edge desktop**.
- [ ] **Connect** → pick VariOne port → status pill turns **green**, device screen mirrors.
- [ ] **Captions** show the `>>` narration lines under the screen as features run.
- [ ] **Fullscreen** (button / click canvas) fills; HDMI → projector shows it.
- [ ] **During a live deauth / VariPortal run** the mirror keeps updating (no port drop).
- [ ] Replug device → **Reconnect last** works in one click.
- [ ] Branding matches WebUI (cyan accent, VAR*I*ONE mark, navy surfaces).
- [ ] Fallback check: WebUI **spectator** still renders (zero-build safety net).

## 9 · AI-debrief stability matrix (T1–T6)
- [ ] T1 device→hotspot STA < ~15 s · T2 phone→device DHCP lease · T3 full cycle ×5 no reboot
- [ ] T4 heap holds pre-TLS · T5 hotspot-off → offline canned debrief, no hang · T6 2–3 phones at once

## 10 · Hardware (see DEMO_HARDWARE_GUIDE.md)
- [ ] **D1 cap test** — NRF + RF TX back-to-back, no reset/glitch.
- [ ] **D3 powerbank** — 10+ min idle, no auto-cutoff.

---

## 11 · Round-2 HW-test fixes (this batch — needs a fresh build)

- [ ] **Persisted config self-heals (no erase):** after flashing, the serial config dump shows
      `wifiAp.pwd:""` and QR `WIFI:T:nopass;S:VariOne;;` even though NVS previously had `varione1`.
- [ ] **No WDT flood:** browsing files in WebUI / running debrief no longer spams
      `task_wdt: ...(707): task not found`.
- [ ] **NRF jammer cycle:** changing modes on the live jammer screen cycles only the 6 visible
      modes — Video/RC/Zigbee/Drone never reappear.
- [ ] **Debrief persists:** after a debrief, serial shows
      `[DEBRIEF] saved /debrief/last_<type>.html (LittleFS)` — no SD fopen errors.
- [ ] **"Install App Store"** no longer in the Config menu.
- [ ] **USB mirror:** on connect shows "connected — navigate device" (not dead "waiting");
      navigating the device paints the screen; unplug/replug or a device reboot → mirror
      **auto-reconnects** and resumes with no manual re-Connect.
- [ ] **NRF first launch:** cold boot (power-cycle), open NRF24 **first** (before RF/RFID) → it
      initializes (CC1101 CS-deselect fix).
- [ ] **NRF mode picker:** entering any NRF feature shows only **SPI Mode / Main Menu** (no
      "SPI UART" / "SPI BOTH"); SPI Mode runs, Main Menu backs out.
- [ ] **Mirror quality:** text on the mirror is crisp (3× supersample), not blocky.
- [ ] **QR on mirror:** open a screen with a QR (AP-join / debrief report / a QR menu) → the
      QR appears on the mirror (white bg + black modules), scannable. Serial shows a `>>QR:` line.
- [ ] **CIC portal desktop:** on a laptop browser the login/header/students sit **left-aligned**
      (not centered), red bars full-bleed.

## Regression — DO NOT break (confirmed-good before this batch)
- [ ] WiFi AP/debrief working formula still holds (DHCP re-seat, promiscuous-off,
      country-EG-after-softAP, attack-teardown chokepoint, max_connection 10).
- [ ] SD still mounts; SubGHz `.sub` still saves to LittleFS; NRF+screen bus isolation intact.
