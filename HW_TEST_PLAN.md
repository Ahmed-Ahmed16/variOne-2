# VariOne Revamp — Hardware Test Plan (owner)

One section per batch. Flash `Bruce-varione-s3.bin`, then run the checks for each
completed batch. Builds are all green locally; this verifies real-device behavior.

> Flash: `pio run -e varione-s3 -t upload` (or the merged `Bruce-varione-s3.bin` at 0x0).
> The Vemo theme lives on SD at `/themes/VariOne_Vemo` (or flash the rebuilt
> `sd_files/themes/VariOne_Vemo.zip`).

---

## Batch 1 — Critical BACK fix + logo reverts

**What changed:** `ble_ninebot.cpp` scan made cancellable (non-blocking NimBLE scan
polled every ~20 ms; "No scooter" wait now Esc-pollable). Theme reverted: `ir`, `ble`,
`nrf`, `config` icon keys removed + PNGs deleted + zip rebuilt → those fall back to stock
hardcoded icons.

1. **BACK during BLE/Ninebot scan:** open BLE → Ninebot. While "Scanning BLE" shows,
   tap BACK once → must exit to the BLE menu **immediately** (within ~½ s), NOT rescan.
   Repeat 5×, including a tap right at scan start and mid-scan.
2. **BACK during "No scooter" wait:** with no scooter nearby, let a scan finish → while
   "No scooter" shows, tap BACK → must exit immediately, not loop into another scan.
3. **Icons after revert:** main menu — `IR`, `BLE`, `NRF`, `Config` show the **stock**
   built-in icons; `WiFi` and `RFID` (and `RF`) still show the custom Vemo icons.
4. **No decoder crash:** scrolling the whole main menu does not reboot/crash (confirms the
   removed PNG keys fall back cleanly, no missing-file decode).

**Pass:** BACK exits scans instantly every time; icons as described; no crash.

_Note (not a code change this batch):_ other `while(!check(EscPress))` loops
(rf_scan, keyfob_inspect, debrief, nrf_spectrum, ble_common BLE Send) were audited —
they poll every iteration with no long blocking call between checks, so BACK already
works. If any one feels sticky on HW, report it and I'll apply the same cancellable
pattern.

---

## Batch 2 — Menu declutter + Mount Storage

**What changed:** main-menu items `JS Interpreter`, `Apps`, `Clock`, `Connect` hidden via
`isBoardHiddenMenu()` (gated by existing `VARIONE_HIDE_UNSUPPORTED_MENUS`). Submenu
`Audio Config` (Config menu) and `Ninebot` (BLE menu) hidden behind new board flags
`VARIONE_HIDE_AUDIO_CONFIG` / `VARIONE_HIDE_NINEBOT`. `Mass Storage` label → `Mount Storage`
(menu entry + in-screen title).

1. **Main menu:** scroll the whole main menu — `JS Interpreter`, `Apps`, `Clock`, `Connect`
   are **gone**. WiFi/BLE/RF/RFID/IR/Files/NRF/Others still present.
2. **Hide/Show Apps:** Settings → (Display/System) → Hide/Show Apps list — the same 4 are
   absent there too (can't be re-shown by accident).
3. **Config menu:** open Config → `Audio Config` is **gone**; `Display & UI`, `System
   Config`, `Power` remain.
4. **BLE menu:** open BLE → `Ninebot` is **gone**; `BLE Scan`, `BLE Spam`, `BLE Suite`
   remain.
5. **Mount Storage:** Files menu shows `Mount Storage` (not "Mass Storage"); entering it
   shows title "Mount Storage" and the SD still mounts as a USB drive on a PC.

**Pass:** all 6 items hidden/renamed as above; nothing else missing; storage still works.

> **Icon follow-up (owner request, commit a05d36f):** RF menu icon also reverted to stock
> (in addition to Batch 1's ir/ble/nrf/config). So RF/NRF/CONFIG/BLE/IR all show stock
> icons; WiFi/RFID/Files/Others keep custom Vemo icons. Re-check after flashing the
> rebuilt `VariOne_Vemo.zip`.

---

## Batch 10 — Rebrand Bruce → VariOne + Vemo boot/About

**What changed:** visible-string rebrand (no code-symbol or capture-format changes). New
`VARIONE_VERSION="1.0"` board macro drives displayed version; ifdef-guarded so the stock
board still builds.

1. **Boot splash:** power on → splash shows **"VariOne v1.0"** (+ Vemo boot art), not "Bruce".
2. **Status bar:** top/status bar reads **"VariOne 1.0"**, not "BRUCE dev".
3. **About:** main menu → Config → About (or Device Info) → title **"ABOUT VariOne"**;
   first line **"VariOne v1.0"**; then **"Graduation Project - CIC New Cairo"** and
   **"Supervised by Dr. Ahmed Gaber"**; diagnostics still listed below.
4. **AP SSID/pwd:** start an AP feature (Evil Portal / WebUI / Connect-AP) → SSID broadcasts
   as **"VariOne"**; connect with password **"varione1"** (8 chars — must NOT fall back to
   an open AP). WebUI login = **admin / vari**; WebUI page title says VariOne.
5. **BLE name:** run a BLE advertise feature → a phone BLE scanner sees **"VariOne-XXXX"**
   (and Vari-* for BLE Suite attacks), not "Bruce-XXXX".
6. **Capture-tool compat (regression guard):** capture an IR/PCAP/wardriving file → confirm
   it still lands in **/BruceIR /BrucePCAP /BruceWardriving** with the original
   "Filetype: Bruce IR File" / brand=Bruce headers (Flipper/Bruce parsers depend on these —
   they were intentionally NOT rebranded).

**Pass:** all displayed Bruce→VariOne; AP connects with varione1; BLE shows VariOne-…;
capture files keep Bruce paths/headers.

---

## Batch 11 — BadUSB verify + demo scripts

**What changed:** firmware was already wired (`-DUSB_as_HID=1`, native USBHIDKeyboard,
Others → BadUSB & HID → BadUSB). No source touched. Added two **safe** awareness scripts +
a README to `sd_files/BadUSB and BlueDucky/` so they flash to the SD card:
`VariOne_demo_hello.txt`, `VariOne_demo_url.txt`, `VariOne_BadUSB_README.md`.

Full owner verify checklist lives in that README. Quick version:

1. **Enumerate:** plug VariOne into a Windows PC → Device Manager shows it under Keyboards
   (or `lsusb` on Linux lists the HID).
2. **hello demo:** Others → BadUSB & HID → BadUSB → SD Card → `BadUSB and BlueDucky/` →
   `VariOne_demo_hello.txt` → press OK → Notepad opens and the awareness message is typed.
3. **url demo:** run `VariOne_demo_url.txt` → Run dialog opens the awareness URL.
4. **BACK cancels** mid-script; **re-run** works without re-plugging.

**Pass:** PC sees a USB keyboard; both scripts run end-to-end; BACK cancels; re-run OK.

> Note: these are SD-card data files — no firmware rebuild needed. The picker browses from
> card root, so navigate into `BadUSB and BlueDucky/`. Scripts are Windows (`GUI r`); add a
> Linux/macOS launcher line if the demo PC needs it.

---

## Batches 4 / 5 / 6 / 8 — WiFi / BLE / NRF / RFID declutter (commit 1ecc44d)

All reversible compile-flag hides. Flip the flag off in `boards/varione-s3/varione-s3.ini`
to bring any group back.

### Batch 4 — WiFi menu
1. Open **WiFi**. These are **gone**: Listen TCP, Client TCP, SOCKS4 Proxy, TelNET, SSH,
   Wireguard, Responder, VariGotchi.
2. Still present: **Wifi Atks**, **Evil Portal**, **Sniffer**, **Scan Hosts**,
   **WiFi Pass Recovery**, Connect/Start AP, Config.
3. **Capture→crack regression:** Wifi Atks → Target → Capture Handshake still works;
   WiFi Pass Recovery still opens (offline crack). This is the headline demo chain — must
   survive.

### Batch 5 — BLE menu
1. Open **BLE**. Gone: Media Cmds, iBeacon, Bad BLE, BLE Keyboard, Ninebot.
2. Present + renamed: **BLE Scan**, **BLE Popup Spam** (was "BLE Spam"),
   **BLE Attack Suite** (was "BLE Suite").

### Batch 6 — NRF24 menu + jammer
1. Open **NRF24**. **MouseJack** entry gone; Information, Spectrum, NRF Jammer present.
2. Open **NRF Jammer** submenu. Modes shown: **Full Spectrum**, **WiFi 2.4 (Degrade)**,
   **BLE Link Kill**, **BLE Discovery Kill**, **Bluetooth Kill**, **Wireless Mouse/KB Kill**.
   Gone: Video/FPV, RC Controllers, Zigbee, Drone FHSS, Single CH, CH Hopper.
3. Sanity: pick **BLE Link Kill** (or Full Spectrum) → jammer runs; **BACK** cancels.
   (WiFi label honestly says "Degrade" — it weakens, not a hard block.)

### Batch 8 — RFID menu
1. Open **RFID**. Gone: Amiibolink, Chameleon, PN532 BLE, PN532 UART.
2. **Present (must still work on the I2C PN532):** Read tag, Read EMV, Read 125kHz,
   Scan tags, Load file, Erase data, Write NDEF, SRIX Tool (I2C mode), Config.
   → Read a real NFC tag to confirm the core reader is intact (we deliberately did NOT set
   REMOVE_RFID_HW_INTERFACE, which would have removed these).

**Pass (all):** listed items hidden/renamed; kept features (esp. WiFi capture→crack and the
RFID I2C reads) still function.
