# VariOne S3 — Session Handoff (2026-06-16, session 2)

## State
- Branch `ui-revamp`, tip `bf0ba9c`. **UNPUSHED** — local is ahead of
  origin/ui-revamp by 6 commits. Push NOT done (user holding).
- Build: BOTH green this session —
  - `pio run -e varione-s3` **SUCCESS** (RAM 37.0%, Flash 79.6% / 4,071,411 B).
  - `pio run -e esp32-s3-devkitc-1` **SUCCESS** (stock regression check).
- HW-confirmed: **none** — desk/build only. Owner tests on-device 2026-06-16 AM.

## What shipped (this session, oldest->newest)
| commit | what | HW status |
|--------|------|-----------|
| `4752ae3` | fix(hal): `tft_display::initBus()` no-op instead of re-exporting private `TFT_eSPI::initBus`. | not tested |
| `bf0ba9c` | feat(ui): Phase 3 scan chrome + Ninebot flicker fix (WiFi+BLE). | not tested |

## Phase 3 — DONE (WiFi + BLE only)
- **Ninebot flicker fix** ([ble_ninebot.cpp](src/modules/ble/ble_ninebot.cpp)):
  the loop drew `redrawMainBorder()` + full `showVemoStatus()` (fillScreen + head)
  every iteration → blink between scans. Now: `beginVemoScan("Scanning BLE")` draws
  the head ONCE; `updateVemoScanText()` repaints only the text band between scans;
  head is redrawn after `loopOptions()` takes the full screen (`needFullDraw` flag).
  The old `redrawMainBorder()` in the scan path was dropped — `fillScreen` wiped it
  anyway, so the border never actually showed.
- **vemo_status API** ([vemo_status.h](src/modules/varione/ui/vemo_status.h)):
  added `beginVemoScan(msg, footer)`, `updateVemoScanText(msg)`, `rssiBars(dBm)`.
  Graceful fallback to the stock Bruce band is preserved on every path (no head art
  / missing/corrupt `.bin` → `displayTextLine`).
- **Tier-1 chrome:**
  - `rssiBars()` → `[||..]` 0–4 bar glyphs, shown in WiFi + BLE result rows
    instead of raw dBm ([wifi_common.cpp](src/core/wifi/wifi_common.cpp),
    [ble_common.cpp](src/modules/ble/ble_common.cpp), ninebot device names).
  - Footer hint on the scan screen: `<> Nav   OK Sel   BACK Esc` (mapped to the real
    6-button layout: LEFT/RIGHT nav, OK select, BACK esc — see
    [boards/varione-s3/interface.cpp](boards/varione-s3/interface.cpp)).
  - Live "Scanning… ch n/13" ticking **NOT done** — `WiFi.scanNetworks()` blocks; a
    real per-channel counter needs async scan (plan itself flags this). v2.
  - Page `n/N` on result lists **NOT done** — that lives in the shared
    `loopOptions()` renderer; touching it risks every other board (Tier-3 custom
    widget territory, deferred). Deliberately skipped.
- **Layout note:** scan text uses **FP** (small) font. The 64×64 `vemo_head.png`
  starts text at x≈80, leaving ~80 px width; FM (12 px/char) clips "Scanning …".
  Ninebot retry text shortened to "No scooter" to fit the band.
- **NOT touched (v2, hands-off):** RF / NRF / RFID / IR. wifi_atks deauth/attack
  scans already used `showVemoStatus` from changeset A and now inherit the footer.

## Phase 4 — DONE / verified
- [main_menu.cpp](src/core/main_menu.cpp) `isBoardHiddenMenu` hides
  Ethernet/GPS/LoRa/**FM** in BOTH the main menu (`begin`, line 57) AND Settings →
  Hide/Show Apps (`hideAppsMenu`, line 96) — verified in code.
- Board flags set in [varione-s3.ini](boards/varione-s3/varione-s3.ini):
  `VARIONE_HIDE_UNSUPPORTED_MENUS=1`, `DISABLE_IBUTTON_MENU=1`. FM also never
  compiles in (no `FM_SI4713`). iButton hidden under Others.
- Non-VariOne boards unaffected: `isBoardHiddenMenu` returns false without the flag.

## Critical finding (fixed, but flag for the owner)
- Changeset B (`d6ea6bc`) added `using TFT_eSPI::initBus;` to
  [tftespi.h](lib/HAL/display/tftespi.h) — **illegal** (private base member) and it
  **broke every `USE_TFT_ESPI` board**. varione-s3 uses the ardgfx HAL, which never
  compiles that block, so the breakage was invisible until a stock build. `initBus()`
  is dead code everywhere (no callers; ardgfx/m5gfx already define it no-op), so the
  fix is a no-op `void initBus() {}`. Zero behavior change, zero varione-s3 impact.
  If you'd rather keep changeset B "pure", this one-liner can be folded into it.

## Open / not done
- **Nothing HW-tested.** On-device pass still owed (per session-1 handoff): select
  Vemo theme, menu icons render without clipping, boot centers, WiFi+BLE scans show
  the Vemo head then return to results, FM/GPS/LoRa/Ethernet absent from menu +
  Hide/Show Apps, iButton absent under Others.
- `vemo_head.png` still placeholder — owner replaces with final art.
- Live scan counter + page `n/N` deferred (see Phase 3 notes) → v2 / Tier-3.
- Phase 5 (feature-removal policy / impact matrix) not started.
- 6 commits unpushed.

## Next session
On-device verification of Phase 3 + 4. Then Phase 5 (removal policy/impact matrix).
RF/NRF/RFID/IR scan integration = v2.
Authoritative plan: `UI_REVAMP_PLAN.md`.
Relevant memory: [[ui-plan-status]], [[subghz-littlefs]], [[varione-stable-branch]].
