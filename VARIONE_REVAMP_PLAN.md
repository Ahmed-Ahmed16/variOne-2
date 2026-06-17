# VariOne Revamp Plan — Curation, Rebrand & Mascot (authoritative)

> Next-agent brief. Work **only** from this file. Supersedes ad-hoc notes.
> Branch: **`ui-revamp`** only. **Never touch `varione-stable`** (MVP-done branch).

---

## How to use this plan (workflow)

Execute **one batch at a time**. Do NOT batch-bomb everything in one pass — the owner
flashes and HW-tests each batch before the next.

For each batch:

1. **Read only the files that batch names.** Don't re-explore the whole tree — this plan
   already has the file:line entry points. Saves context/tokens.
2. **Implement** the batch's changes.
3. **Build:** `pio run -e varione-s3` (must SUCCESS, within OTA) **and**
   `pio run -e esp32-s3-devkitc-1` (stock — proves flags don't break other boards).
4. **Update the Progress Tracker** below (status + commit hash + build result).
5. **Commit locally** (one commit per batch, clear message). **Do NOT push** unless the
   owner says so.
6. **Hand to owner for HW test.** Mark HW column when they confirm.
7. Only then start the next batch.

### Token / context discipline

- Use **TodoWrite** at the start of each batch: one todo per concrete step in that batch.
  Mark in-progress/completed as you go so progress survives a context reset.
- Keep the Progress Tracker in THIS file current — it is the durable state if the session
  is summarized or a new agent takes over.
- Don't re-derive facts already in this file (root causes, file:line, decisions). Trust it;
  verify a symbol only if an edit fails.
- Don't read whole large files when a `grep -n` for the entry point suffices.

### Conventions (apply to every batch)

- **Hide, never delete.** All feature removals are reversible compile flags (code stays
  in-tree). See "Hide mechanism".
- **Naming: Vemo** (one e) everywhere; mascot art = Vemo the panda-bot.
- **Never rename code symbols** `bruceConfig`, `bruceConfigPins`, `BRUCE_VERSION` (macro
  name) — only their *displayed* output. Renaming symbols breaks the build.
- Keep the **minimal blue/black theme** — owner loves it. No heavy UI.
- Theme PNGs must be **≤160 px wide** (decoder line-buffer limit) or the device crashes.
- Commits local, unpushed until owner says push.

---

## Progress Tracker (update this as you work)

| # | Batch | Status | Commit | Build (vari/stock) | HW |
|---|-------|--------|--------|--------------------|----|
| 1 | Critical BACK fix + logo reverts | ✅ done+built | 3b94c03 | ✅/✅ | — |
| 2 | Menu declutter (hides) + Mount Storage | ✅ done+built | 9f67bde | ✅/✅ | — |
| 3 | IR jammer fix | ✅ done+built | 37230f9 | ✅/✅ | — |
| 4 | WiFi hides + keep capture→crack | ✅ done+built | 1ecc44d | ✅/✅ | — |
| 5 | BLE hides + rename | ✅ done+built | 1ecc44d | ✅/✅ | — |
| 6 | NRF jammer rename + hide | ✅ done+built | 1ecc44d | ✅/✅ | — |
| 7 | RF cleanup + all-band scanner | ✅ done+built | ce07635 | ✅/✅ | — |
| 8 | RFID cleanup | ✅ done+built | 1ecc44d | ✅/✅ | — |
| 9 | Idle Vemo sleep screen + art (mood-swap deferred) | ✅ done+built | 2aaa7f2 | ✅/✅ | — |
| 10 | Rebrand Bruce→VariOne + Vemo boot/About | ✅ done+built | 4b2eedd | ✅/✅ | — |
| 11 | BadUSB verify + demo scripts | ✅ done (SD-data) | 7313592 | n/c (no src) | — |

Status legend: ☐ todo · ◐ in-progress · ✅ done+built · 🔬 owner-HW-verified · ⏸ blocked.

Suggested order: 1 → 2 → 10 (rebrand) → 11 (BadUSB) → 4 → 5 → 6 → 7 → 8 → 3 → 9.
Batches are independent; 9 is blocked on owner art. Do 1 first (critical bug).

---

## Context

First on-device test of the `ui-revamp` Vemo UI (flashed commit `929a402`) surfaced one
real bug + a large curation pass from the owner. Device = graduation demo (CIC New Cairo,
Dr. Ahmed Gaber). Goal: a clean, honest, demo-focused menu — hide features that need gear/
servers the owner lacks or have no demo value, rename ambiguous items, fix what's broken,
rebrand to VariOne, and bring the Vemo mascot to life **without losing the minimal
blue/black theme**.

## Decisions locked (2026-06-17)

- **HIDE, never delete** (reversible). Owner may re-enable later.
- **Others menu: KEEP** (QRCodes + BadUSB&HID + Megalodon). Hide JS Interpreter + Apps.
- **Clock, Connect, Audio Config: HIDE.**
- **Ninebot: HIDE.**
- **Mascot: animate WITHIN the current minimal theme** — no heavy full-screen overlay.
- **IR jammer: FIX** (continuous carrier + sweep); remove only if HW still dead.
- **Logos:** keep new wifi + rfid; revert ir, ble, nrf, config to stock.
- **Mass Storage → "Mount Storage".**
- **RSSI bars: KEEP.**
- **Rebrand Bruce → Vari/VariOne, mascot Vemo** (visible strings only). Boot shows Vemo.
- **Capture→crack chain: KEEP** Pass Recovery + Capture Handshake; hide BruceGotchi.
- **BLE Suite: SHOWN.** **Zigbee jammer: HIDDEN.**

## Hide mechanism (reversible, board-safe)

- **Main-menu items** (JS Interpreter / Apps / Clock / Connect): add exact `getName()`
  strings to `isBoardHiddenMenu()` in [src/core/main_menu.cpp](src/core/main_menu.cpp)
  (gated by `VARIONE_HIDE_UNSUPPORTED_MENUS`). Also hides them from Settings → Hide/Show
  Apps. Names: `"JS Interpreter"`, `"Apps"`, `"Clock"`, `"Connect"`.
- **Submenu items** (WiFi/BLE/RF/NRF options, Audio Config, Ninebot): wrap the
  `{label, lambda}` entry in a VariOne compile flag (`-DVARIONE_HIDE_<x>` in
  [boards/varione-s3/varione-s3.ini](boards/varione-s3/varione-s3.ini), like existing
  `DISABLE_IBUTTON_MENU`). Entry vanishes; code stays compiled-out but in-tree.

---

## Batch 1 — Critical fix + logo reverts (no feature loss)

1. **BACK-button escape bug** (root-caused): `check(EscPress)`
   ([include/globals.h:222](include/globals.h)) auto-clears `EscPress` ~75 ms after a tap
   (input task, [src/main.cpp:64](src/main.cpp)). Ninebot's loop runs a blocking 5 s scan
   then checks Esc — a mid-scan tap is gone before the check → rescans forever; the
   `delay(2000)` "No scooter" wait has the same hole.
   - Fix in [src/modules/ble/ble_ninebot.cpp](src/modules/ble/ble_ninebot.cpp): replace
     `delay(UI_READ_DELAY)` with a cancellable poll; make the scan cancellable (poll Esc
     every ~50 ms; fallback lower `SCAN_TIME`). Audit every VariOne
     `while(!check(EscPress))` around a blocking call ("some screens" the owner hit).
   - Pre-existing bug, not from the Phase-3 flicker change.
2. **Logo reverts:** in [sd_files/themes/VariOne_Vemo/theme.json](sd_files/themes/VariOne_Vemo/theme.json)
   remove keys `ir`, `ble`, `nrf`, `config`; delete those 4 PNGs; re-zip (`.zip` == folder).
   Missing key → `MenuItemInterface::draw()` falls back to stock hardcoded `drawIcon()`.
   Keep `wifi.png`, `rfid.png`, the rest.

**Verify:** BACK exits BLE/Ninebot scan instantly; ir/ble/nrf/config show stock icons,
wifi/rfid custom; no decoder crash.

## Batch 2 — Menu declutter (hides)

- Main menu: hide `"JS Interpreter"`, `"Apps"`, `"Clock"`, `"Connect"`.
- **Audio Config:** flag-hide the entry at
  [src/core/menu_items/ConfigMenu.cpp:31](src/core/menu_items/ConfigMenu.cpp) (no audio HW).
- **Ninebot:** flag-hide the BLE→Ninebot entry in
  [src/core/menu_items/BleMenu.cpp](src/core/menu_items/BleMenu.cpp).
- **Mass Storage → "Mount Storage":** rename the menu label (entry feeding
  [src/core/massStorage.cpp](src/core/massStorage.cpp)).

**Verify:** the 4 main items + Audio Config + Ninebot gone from menu and Hide/Show Apps;
Others intact; storage entry reads "Mount Storage".

## Batch 3 — IR jammer fix

Rewrite [src/modules/ir/ir_jammer.cpp](src/modules/ir/ir_jammer.cpp): current failure =
fixed 50 % duty + 12 µs pulses + no sweep → receivers ignore it. Emit a continuous 38 kHz
LEDC carrier (high duty) + a 30–56 kHz sweep mode. Keep BACK cancellable.

**Verify (HW):** under IR Read, confirm it blocks a real remote. If still dead at the IR
LED's power → flag-hide it.

## Batch 4 — WiFi hides + cleanup

HIDE (flag) in [src/core/menu_items/WifiMenu.cpp](src/core/menu_items/WifiMenu.cpp):
`Listen TCP`, `Client TCP`, `SOCKS4 Proxy`, `TelNET`, `SSH`, `Wireguard`, `Responder`,
`Brucegotchi` (redundant with targeted capture).

**KEEP:** Sniffer, Scan Hosts, Evil Portal, Wifi Atks (Target/Karma/Beacon/Deauth),
Connect/AP.

**KEEP — capture→crack chain:** `WiFi Pass Recovery` (offline WPA handshake crack vs
wordlist) + Target Atks → `Capture Handshake`. Demo: capture handshake → crack → reveal a
*weak* WiFi password (slow ~13 pw/s; strong passwords won't fall).

## Batch 5 — BLE hides + rename for clarity

- HIDE: `Media Cmds`, `BLE Keyboard`, `Ninebot` (also Batch 2), `iBeacon`, `Bad BLE`.
- KEEP: `BLE Scan`, `BLE Spam`, `BLE Suite` (hijack/FastPair-spam + DoS — shown).
- **Rename to say what they DO** in
  [src/core/menu_items/BleMenu.cpp](src/core/menu_items/BleMenu.cpp): BLE Spam →
  "BLE Popup Spam", etc. (kept items only).

## Batch 6 — NRF jammer rename + hide

In [src/modules/NRF24/nrf_jammer.cpp](src/modules/NRF24/nrf_jammer.cpp) +
[src/core/menu_items/NRF24Menu.cpp](src/core/menu_items/NRF24Menu.cpp):

- **Rename modes (plain language):** BLE data → **"BLE Link Kill"**, BLE advertising →
  **"BLE Discovery Kill"**, BT classic → **"Bluetooth Kill"**, USB dongle →
  **"Wireless Mouse/KB Kill"**, keep **WiFi 2.4** (label honest: *degrades, not a hard
  block*).
- **HIDE:** Zigbee, Drone FHSS, RC, single-channel, channel-hopper, MouseJack.
- NRF keeps **Jammer + Spectrum** only.

## Batch 7 — RF cleanup

In [src/core/menu_items/RFMenu.cpp](src/core/menu_items/RFMenu.cpp): keep **Spectrum** and
**Waterfall/Spectogram**; **add/verify an all-band frequency scanner** that sweeps the
CC1101 sub-bands (~300–348 / 387–464 / 779–928 MHz) and reports the strongest active
frequency — not a single manual freq (building blocks: existing Keyfob Inspect
auto-classify + waterfall range sweep). HIDE **Bruteforce** and **Jammer Itmt** (keep
**Jammer Full**).

## Batch 8 — RFID cleanup

Enable `-DREMOVE_RFID_HW_INTERFACE` for varione-s3 (already gates PN532-BLE, PN532-UART,
SRIX); also flag-hide Amiibolink + Chameleon. KEEP I2C PN532: Read tag, Read EMV, Read
125 kHz, Scan, Load, Write NDEF, Config. Verify flag coverage during execution.

## Batch 9 — Mascot animation (keep the minimal theme) — BLOCKED on owner art

**Constraint:** do NOT lose the blue/black minimal UI. Stay in the current
`showVemoStatus` layout (head + text + footer); animate the head **in place** — NOT the
heavy full-screen `vemoReaction` overlay from the other branch.

- Borrow the **mood concept** from the `jiggly-avalanche` worktree
  (`.claude/worktrees/nifty-driscoll-083bb3/src/modules/varione/varione_mascot.h`, 10-mood
  enum) + art tools (`tools/crop_poses.py`, `tools/png_to_rgb565.py`).
- Render **small per-mood head frames** from owner art (palette Navy `#0B2E63`, Cyan
  `#29C7F6`, Sky `#67E9FF`, Soft White `#F7FCFF`), ≤64–80 px (≤160 wide). Frames:
  idle+blink, working (scan), success (wave), fail (sad), sleeping (Zzz).
- Timer-driven frame-swap in
  [src/modules/varione/ui/vemo_status.cpp](src/modules/varione/ui/vemo_status.cpp) reusing
  the Phase-3 small-region repaint (`repaintScanText`): blink ~3 s, mood per state, Zzz on
  idle. No full-screen takeover; graceful fallback intact.
- State map: WiFi/BLE scan→working, connect/read OK→success(wave), failure→sad,
  idle→sleeping, boot→wave.

**Blocked on:** owner's full mood art. Engine/frame-swap can land first with 1–2 frames;
art swap is data-only after.

## Batch 10 — Rebrand Bruce → Vari/VariOne (+ Vemo boot/About)

**Visible strings only.** NEVER touch symbols `bruceConfig`, `bruceConfigPins`, or the
`BRUCE_VERSION` macro *name* — only displayed output.

- **Version:** add board macro `-DVARIONE_VERSION='"1.0"'`
  ([boards/varione-s3/varione-s3.ini](boards/varione-s3/varione-s3.ini)) for displayed
  version; leave `BRUCE_VERSION` (`"dev"`, platformio.ini:122) for internal use.
- **Boot screen:** [src/main.cpp:236](src/main.cpp) splash "Bruce" → "VariOne v1.0" + Vemo
  mascot face/logo (reuse theme `boot.png` Vemo art). Status bar
  [src/core/display.cpp:827](src/core/display.cpp) `"BRUCE " + BRUCE_VERSION` →
  `"VariOne " + VARIONE_VERSION`.
- **About / Device Info** ([src/core/utils.cpp:112](src/core/utils.cpp) `showDeviceInfo()`):
  title → "ABOUT VariOne"; first line → **"VariOne v1.0"**; add two lines:
  **"Graduation Project — CIC New Cairo"** and **"Supervised by Dr. Ahmed Gaber"**. Keep
  diagnostics below.
- **AP defaults** ([src/core/config.h:64](src/core/config.h)): SSID `BruceNet` →
  **`VariOne`**, pwd `brucenet` → **`varione1`** (must be **≥8 chars** or `_setupAP()`
  drops to an open AP — [src/core/wifi/wifi_common.cpp:122](src/core/wifi/wifi_common.cpp)).
  `"BruceAP"` fallbacks ([debrief.cpp:230](src/modules/varione/debrief/debrief.cpp)) →
  `"VariOne"`.
- **WebUI** ([src/core/config.h:62](src/core/config.h)): creds `{"admin","bruce"}` →
  `{"admin","vari"}`; rebrand page title/header in
  [src/core/wifi/webInterface.cpp](src/core/wifi/webInterface.cpp).
- **BLE advertised names:** `Bruce-XXXX` → `VariOne-XXXX`
  ([ble_common.cpp:176,205](src/modules/ble/ble_common.cpp)); `Bruce-App`,
  `Bruce-Attack/Exploit/Flooder/Spammer/PINBrute`
  ([BLE_Suite.cpp](src/modules/ble/BLE_Suite.cpp),
  [ble_js.cpp](src/modules/bjs_interpreter/ble_js.cpp)), `ble_api` init
  ([ble_api.cpp:23](src/modules/ble_api/ble_api.cpp)) → `Vari-*`.
- **pwngrid identity** ([pwngrid.cpp:108](src/modules/pwnagotchi/pwngrid.cpp)) + device_js
  fallback + responder default "Bruce" → "VariOne".
- **KEEP for tool compat (do NOT rebrand):** on-disk capture folders `/BruceIR`,
  `/BrucePCAP`, `/BruceWardriving` + file-format headers (`Filetype: Bruce IR File`,
  wardriving `brand=Bruce`) — Flipper/Bruce parsers read these.
- Sweep: `grep -rn '"[^"]*Bruce' src/`, rebrand each *displayed* string, rebuild.

**Verify:** boot shows VariOne v1.0 + Vemo; AP SSID "VariOne" connects with `varione1`;
BLE scanners see "VariOne-…"; About shows grad-project + supervisor; build green.

## Batch 11 — BadUSB verify + demo scripts

BadUSB = USB HID keystroke injection (Ducky Script). **Already wired:** `-DUSB_as_HID=1`
([boards/varione-s3/varione-s3.ini](boards/varione-s3/varione-s3.ini)); native-USB
`USBHIDKeyboard` + `USB.begin()`; entry **Others → BadUSB & HID → BadUSB**
([src/core/menu_items/OthersMenu.cpp](src/core/menu_items/OthersMenu.cpp) `badUsbHidMenu`).
`ducky_setup()` → `loopSD()` picks a `.txt` script → `key_input()` runs it line-by-line
([src/modules/badusb_ble/ducky_typer.cpp:228,327](src/modules/badusb_ble/ducky_typer.cpp)).
Existing payloads in `badusb_payloads/` + `data/badusb`.

**Tasks:**

1. **Verify the feature** on HW (USB OTG path works on the S3 native USB): plug the device
   into a PC as a USB keyboard, run a script, confirm keystrokes land. Check the picker
   reads scripts from the right FS (SD vs LittleFS) and that the chooser lists them.
2. **Ship 2 basic, SAFE awareness scripts** on SD (copy into `sd_files/` so they flash to
   the card; keep them harmless — graduation demo, not malware). Example Ducky content:

   `sd_files/badusb/demo_hello.txt` (Windows — opens Notepad, types an awareness message):

   ```
   REM VariOne BadUSB awareness demo
   DELAY 1000
   GUI r
   DELAY 500
   STRING notepad
   ENTER
   DELAY 1500
   STRING This PC was just controlled by a VariOne BadUSB demo.
   ENTER
   STRING Lesson: never plug in or trust unknown USB devices.
   ```

   `sd_files/badusb/demo_url.txt` (Windows — opens a browser to an awareness page):

   ```
   REM VariOne BadUSB awareness demo - open URL
   DELAY 1000
   GUI r
   DELAY 500
   STRING https://example.com/varione-awareness
   ENTER
   ```

   (Cross-platform note: `GUI r` is Windows; add a Linux/macOS variant only if the demo PC
   needs it. Verify the device's Ducky dialect supports `REM/DELAY/GUI/STRING/ENTER`.)

3. **Document demo + test/verify** (put in the README near the scripts):
   - **Demo:** plug VariOne into a volunteer laptop → run `demo_hello.txt` → it auto-types
     the awareness message in Notepad → teaches "USB devices can act as a keyboard; never
     trust unknown USB."
   - **Verify:** (a) device enumerates as a USB keyboard (Device Manager / `lsusb`);
     (b) `demo_hello.txt` types the exact message; (c) `demo_url.txt` opens the URL;
     (d) BACK cancels mid-script; (e) re-run works without re-plug.

**Verify (build + HW):** builds green; on HW both scripts run end-to-end on a Windows PC.

- `pio run -e varione-s3` SUCCESS, within OTA partition.
- `pio run -e esp32-s3-devkitc-1` (stock) SUCCESS — flags don't break other boards.
- Owner HW pass per batch. Naming stays **Vemo**. Never touch `varione-stable`. Unpushed.

## Feature reference (demo framing)

- **Responder** = LLMNR/NBNS poisoning → Windows NTLMv2 hash capture (needs Windows client).
- **BruceGotchi** = passive pwnagotchi (deauth+handshake capture; does NOT join nets).
- **iBeacon** = fake Apple beacon broadcast (location-spoof demo).
- **BadBLE** = BLE keystroke injection (needs pairing).
- **BLE Suite** = FastPair popup-spam / HID hijack / BLE DoS (needs target).
- **NRF WiFi-jam** = degrades, not a hard block.
- **RF spectrum/waterfall** = RSSI views; owner wants an all-band freq finder.
- **IR jammer** = fixable via continuous carrier + sweep.
- **Mount Storage** = SD appears as a USB drive on a PC.
- **Capture→crack** = WPA handshake captured over the air → offline wordlist crack →
  reveal weak WiFi password. Strongest WiFi demo chain.
