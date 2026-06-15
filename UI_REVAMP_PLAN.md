# VariOne Veemo UI Revamp — Plan

Branch: `ui-revamp`. Supersedes `uiPLAN.md` (earlier draft). Every constraint here
is verified against the actual firmware, not assumed.

## Goal

Give VariOne S3 a distinct identity — black + neon-cyan palette, **Veemo** the
panda-bot mascot — while keeping Bruce's fast, simple menu. Veemo appears at boot,
during scans/waits, and on success/error/idle moments; he stays out of dense
result lists. A draft theme pack and ~80%-wired firmware hooks already exist, so
this is largely a *finish-and-refine* job, not a from-scratch build.

---

## Decisions (locked with user 2026-06-15)

1. **lib_deps (changeset B): keep as-is.** The 6 lines a prior agent added to the
   board `.ini` — 4 are ESP32 framework built-ins (no-op), only QRcode + PN532_SRIX
   are real external libs. Build is green with them; not worth the churn.
   *Caution: changing `lib_deps` requires a clean build of the env (stripping them
   once corrupted the `.pio` variant cache).*
2. **FM menu: remove.** Hide `FM` alongside GPS/LoRa/Ethernet, and drop `fm.png`
   from the theme (plus orphan `gps.png`, `veemo_pose.png`), then re-zip.
3. **Menu icon style: keep current** (framed line-art + label + "V-ONE" watermark).
4. **Scan text placement: Veemo above/beside the text — never behind the face.**
   Scan screens are *not* full-screen Veemo with an overlaid text band. Small Veemo
   + adjacent status text.
5. **v1 scan scope: WiFi + BLE only.** RF / NRF / RFID / IR deferred to v2.
6. **Boot sound: silent.** No `boot_sound` (ESP32-S3 has no DAC / no speaker).
7. **Veemo direction: small persistent Veemo head + shared chrome** on scan/list
   screens (Veemo always present). Full-screen Veemo reserved for boot / success /
   error / idle.
8. **Chrome scope: Tier 1 in v1** (page counter, RSSI signal bars, footer hint
   line, header wordmark + status dot — text/layout only). Tier 2 animations next.
   Tier 3 custom framed-panel widget only after MVP.

---

## Current status (verified 2026-06-15)

- **Build: PASS.** `pio run -e varione-s3` → SUCCESS. RAM 37.0% (121,156 B),
  Flash **79.6%** (4,069,287 / 5,111,808 B) — ~1 MB app headroom.
- **Menu-hide names: PASS.** `GpsMenu("GPS")`, `LoRaMenu("LoRa")`,
  `EthernetMenu("Ethernet")` match `isBoardHiddenMenu()`; `NRF24Menu("NRF24")` and
  `FMMenu("FM")` are not yet hidden (FM will be per Decision 2).
- **Theme pack: PASS.** `theme.json` valid; all 17 referenced images exist; every
  PNG ≤160 wide; `.zip` filename set == folder filename set.

---

## Working-tree state — three tangled changesets

The uncommitted diff mixes three unrelated efforts. Untangle into separate commits
so UI history is clean.

- **A — Veemo UI (this task):** `theme.{h,cpp}` (veemo_* keys), `veemo_status.{h,cpp}`,
  `main_menu.cpp` + `OthersMenu.cpp` + board `.ini` hide flags, scan hooks in
  `wifi_common.cpp` / `ble_common.cpp` / `ble_ninebot.cpp` / `wifi_atks.cpp`.
- **B — SPI/NRF leftovers (separate commit):** `initBus()` added to display HAL
  (`ardgfx`, `lovyan`, `m5gfx`, `tftespi`); lib_deps adds (Decision 1: keep).
- **C — RF SubGHz save fix (separate commit):** `rf_scan.cpp` forces `.sub` saves
  to LittleFS (SD collides with CC1101 on the shared bus). Correct; matches the
  existing project decision.

Untracked helper `varione/ui/paged_text.{cpp,h}` is unrelated — review separately.

Recommended commit order: **C → B → A**.

---

## Hard technical constraints (verified in code)

1. **Display runs landscape 160×128.** Board: `TFT_WIDTH=128`, `TFT_HEIGHT=160`,
   `TFT_ROTATION=1` → runtime `tftWidth=160`, `tftHeight=128`.
2. **PNG wider than 160 px crashes the decoder.** `display.cpp` uses
   `usPixels[MAX_IMAGE_WIDTH]` with `MAX_IMAGE_WIDTH = max(TFT_WIDTH,TFT_HEIGHT)=160`.
   Every theme PNG must be ≤160 wide. The contest **scifi theme is 320 px → will
   overflow.** Reference only; never load it.
3. **Themes pre-decode to `.bin` at load.** `openThemeFile()` → `preparePngBin()`
   writes `<dir>/tmp/<name>.bin`; runtime draws the cache. First select is slow,
   later draws cheap.
4. **`displayError/Success/TextLine` draw a centered text band, not a full clear**
   (all route to `displayRedStripe()`, a rounded rect at rows ~51–77). It does not
   `fillScreen`. Per Decision 4 we move away from overlaying this on Veemo's face.
5. **Theme select + boot already supported.** Picker: `setTheme()` in
   `settings.cpp`. Boot override: `main.cpp` (`boot_img == 5`).
6. **Menu hide is name-based** — relies on exact `getName()` strings (verified).

---

## Phases

### Phase 0 — Untangle the working tree
Confirm baseline build, then commit **C** (rf LittleFS) and **B** (initBus + kept
lib_deps) on their own. Leave **A** for the work below. No new code here.

### Phase 1 — Theme pack (`sd_files/themes/VariOne_Veemo/`)
Draft is on-brand and valid. Finishing work:
- Remove `fm.png`, `gps.png`, `veemo_pose.png`; re-zip so `.zip` == folder.
- Keep menu icons 160×72, full-screen art 160×128, all ≤160 wide.
- Per Decision 4/7: full-screen `veemo_scan.png` is repurposed as boot/idle art;
  scan screens use a small Veemo asset + text (see Phase 3).
- Pose → state mapping: boot = hero/point; success = thumbs-up + sparkles;
  error = surprised (`!`); idle = sleeping (Zzz); scan = small "thinking"/working.

### Phase 2 — Firmware theme support (`veemo_status.*`)
- Rework `showVeemoStatus()` for Decision 4/7: small Veemo + adjacent status text,
  not full-screen + overlaid band.
- Add a draw-once / text-only-repaint path so scan loops
  (`wifiConnectMenu`, `BLENinebot::loop`, `deauthFloodAttack`) don't redraw the
  full image every iteration (flicker).
- Keep the fallback: no `veemo_*` image → behave exactly like stock Bruce
  (`displayTextLine/Success/Error`). Tolerate a missing/corrupt `.bin` without
  crashing.

### Phase 3 — Scan/status integration v1 (WiFi + BLE only)
Already wired: WiFi connect/known-net/attack/deauth scans, BLE scan, BLE Ninebot.
- Verify each shows small Veemo while blocking, then returns to the normal results
  list. Do **not** touch RF/NRF/RFID/IR (v2).
- Tier-1 chrome on these screens: live "Scanning… ch n/13" text, page `n/N`, RSSI
  → 0–4 bar glyphs, footer hint mapped to the real 6-button layout.

### Phase 4 — Menu clutter cleanup
Hidden via reversible board flags (no deletion): GPS, LoRa, Ethernet
(`VARIONE_HIDE_UNSUPPORTED_MENUS`), iButton (`DISABLE_IBUTTON_MENU`), and now
**FM** (Decision 2). Verify hidden items also vanish from Settings → Hide/Show
Apps, kept menus (WiFi, BLE, RF, NRF24, RFID, IR, Files, Clock, Config, Connect,
Others) stay, and non-VariOne boards are unaffected.

### Phase 5 — Feature removal policy (later)
Phase 1 = hide only. Before any hard deletion, produce an impact matrix (menu, HW
dep, VariOne has it?, external-module possible?, files, risk, recommendation). If a
feature might work via an external module, default to hidden, not deleted.

---

## Cool Veemo UI & loading ideas (tiered by effort)

Driven by the user's AP-SCAN mockups (small Veemo head + framed panel + page
indicator + signal bars + footer). Reality check: mockups are CSS-grade; the ST7735
+ Bruce primitives approximate (flat fills, 1 px rounded rects, neon line-art PNGs)
but won't match pixel-for-pixel. Animations must repaint only a small region.

**Tier 1 — cheap, high impact (text/layout):**
- Live scan counter ("Scanning 2.4GHz… ch 7/13"). True live ticking needs
  passive/per-channel scan; otherwise band + spinner.
- Page indicator `n/N` on lists.
- RSSI → 0–4 signal-bar glyphs (friendlier than dBm for awareness demos).
- Footer hint line mapped to the real 6 buttons.

**Tier 2 — small Veemo animations (2–3 cached frames, small redraw):**
- Veemo "ping" scan loop (expanding wifi arcs).
- Veemo blink every ~3 s on wait screens.
- Sweep/progress bar for deterministic ops (theme decode, file read, BadUSB).
- Success pop (thumbs-up + sparkle) / error shake (surprised + 1-frame jitter).

**Tier 3 — richer, more core work (after MVP):**
- Persistent small-Veemo shell: reusable VariOne list/panel widget (header + Veemo
  head + page count + footer), scoped to a few screens so stock Bruce stays intact.
- Boot wake-up sequence: visor lights up → hero pose + VO logo → menu.
- Idle screensaver: Veemo lies down with Zzz, screen dimmed.
- Status LED dot (green idle / cyan busy / red attack); mirror to RGB LED if present.

---

## Test & verification plan

- **Static:** `theme.json` parses; all referenced images exist; every PNG ≤160 wide;
  `.zip` == folder.
- **Build:** `pio run -e varione-s3` passes within OTA partition; also build one
  stock env to prove hide flags + `veemo_status` include don't break other boards.
- **On-device:** select the theme; menu icons render without clipping; boot centers;
  WiFi + BLE scans show Veemo then return to results; GPS/LoRa/Ethernet/FM absent
  from menu and Hide/Show Apps; iButton absent under Others.
- **Regression:** stock theme without `veemo_*` keys → plain text band still works;
  theme disabled → scans work; missing/corrupt `veemo_*.png` → graceful fallback.

---

## Risks

- Loading the 320 px scifi PNGs crashes the decoder (constraint #2) — never load.
- Changing `lib_deps` needs a clean build (Decision 1 note).
- Loop flicker if the Phase 2 draw-once guard is skipped.
- Name-based menu hide is brittle (verify strings on any rename).
- Tier-3 custom rendering risks regressions on non-VariOne boards — keep it scoped.
