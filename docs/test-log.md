# VariOne Test Log

## 2026-04-28 - Sub-GHz classifier alignment fix for fan remotes

- Scope: Sub-GHz fixed/rolling classifier (`src/main.cpp`).
- Problem reported: fan STOP button produced two healthy 315 MHz captures, but classifier reported `ROLLING?` at 44% because press 1 and press 2 started on different GDO0 levels and different repeated-frame offsets (`start_prev=1`, `start_curr=0`).
- Root cause: old classifier compared `edges_us[i]` directly against previous `edges_us[i]`. That is too naive for fixed-code fan/appliance remotes because capture can begin mid-frame or on the opposite edge polarity.
- Change: added aligned comparison over nearby offsets (`compareSubGhzAligned`) and removed edge-count difference as a rolling-code trigger. Verdict now uses best aligned similarity plus average delta, and serial logs `offset` / `compared` for diagnosis.
- Hardware tests needed: run UP classify mode again on the fan STOP button. Expected result is `FIXED` or at least higher similarity; if replay still fails after a fixed verdict, focus on TX timing/power/orientation instead of rolling code.

## 2026-04-28 - SD Sub-GHz sequence filenames for offline sorting

- Scope: Sub-GHz SD persistence (`src/main.cpp`), governed by PRD §9.7 / §11.
- Problem reported: FAT timestamps show `1979-12-31` on a PC because the ESP32 has no RTC, making old/new captures hard to identify after laptop-free field tests.
- Change: added `/captures/sequence.txt`, a monotonic SD sequence counter, and sequence-prefixed Sub-GHz capture/replay filenames such as `0000507264_446042_433920kHz.json`.
- Existing `captured_at: "uptime-ms-..."` remains unchanged per PRD §11; new JSON files also include `"seq": <number>` for easy sorting.
- Added serial target helper `5=fixed_code_fan` so fan tests can be labeled separately from car/gate tests when serial is available.
- Hardware tests needed: flash, capture two fan remote presses without laptop, pull SD, confirm newest Sub-GHz files sort by leading sequence number and include `target_class` / `seq`.

## 2026-04-28 - CC1101 fan remote capture recovery patch

- Scope: Sub-GHz CC1101 scanner/capture/replay path (`src/main.cpp`).
- Problem reported: fan remote stop signal previously captured but no longer produced scanner spikes, could not be stored, and therefore could not be replayed.
- Root cause suspected in code: primary CC1101 GDO0 was forced to carrier-sense (`IOCFG0=0x0E`) after init, while raw OOK capture/replay needs async serial mode (`setCCMode(0)`, GDO0 raw data). Canceling an armed capture also left `sgListening=false`, so later arms could silently never capture.
- Changes: added `configureSubGhzRawRx()`, restored raw OOK async RX after init/scan/replay/jammer stop, made `armSubGhzCapture()` always re-enter RX/listening, kept listening alive after cancel, widened scan RX bandwidth, extended arm window to 8 s, extended raw capture to 180 ms with quiet-end detection, increased edge buffer to 512, and expanded SD reload buffer for longer captures.
- Scanner now checks 300 / 303.875 / 315 / 330 / 345 / 390 / 433.92 / 868.35 / 915 MHz and logs both RSSI peak and GDO edge activity.
- Hardware tests needed: flash, open Sub-GHz, use serial `f`, hold/press fan STOP through the full scan, set the highlighted frequency with OK, arm capture, verify `edges>18`, save file, reload file, replay near the fan.

## 2026-04-29 - Rolling/fixed code verdict + physical button replay

- Scope: Sub-GHz input handler + drawSubGhz (`src/main.cpp`).
- Problem reported: fan remote replay failed. Root cause unknown — likely frequency mismatch (many fans use 315 MHz, not 433.92 MHz). Run freq scanner first.
- Added **classify mode**: UP button ('w') when no capture → arms 2-press sequence, computes similarity, prints `CODE TYPE: FIXED / UNCERTAIN / ROLLING?` to serial with replay advice.
- Added **UP button as direct replay**: UP ('w') when capture is ready and not awaiting result → replays immediately. Physical button, no serial needed. 
- Verdict thresholds: ≥85% sim = FIXED CODE (fan/gate/garage — replay expected to work), 50–84% = UNCERTAIN, <50% = ROLLING? (car key — replay likely rejected, use RollJam).
- OLED hints updated: `ok:arm up:classify dn:load bk:exit` (no capture) | `ok/up:replay dn:clear bk:exit` (capture ready).
- Serial verdict line added after every 2-press compare, e.g.: `[CC1101] CODE TYPE: FIXED (sim=92%) — replay should work. If it didn't, check frequency with serial 'f'`
- Hardware tests needed: fan classify verdict (expect FIXED >85%); fan replay after freq confirm; physical UP button replay without laptop.

## 2026-04-28 - Two-CC1101 RollJam implemented

- Scope: Sub-GHz module (`src/main.cpp`). Algorithm: independent impl of Samy Kamkar RollJam concept. No code copied.
- Hardware required: spare CC1101 wired to VSPI (SCK=18, MISO=19, MOSI=23) with CS=GPIO2.
- Jammer: 433.80 MHz (-120kHz from fob freq). Car RX BW ≈ ±150kHz → jammed. VariOne RX BW set to ±58kHz → jammer outside passband → VariOne not jammed.
- KeeLoq window: 16-press normal accept, ~1000-press resync. Single uncaptured press = counter diff 1 → accepted.
- State machine: RJ_JAMMING_WAIT_P1 → capture P1 → stop jammer → replay P1 (car opens) → RJ_WAIT_P2 → capture P2 → RJ_COMPLETE. Press OK to replay P2 (second unlock).
- Serial 'j' in Sub-GHz state starts RollJam. Serial 'q' aborts.
- No-op if GPIO2 CC1101 not wired (initCC1101Jammer prints warning, cc1101JamOk=false).
- Hardware tests needed: verify narrow BW registers suppress jammer leakage; verify P1 replay timing; verify P2 held correctly.

## 2026-04-28 - Rolling code study: .sub export + 4-press session

- Added `saveSubGhzFlipperSub()`: every capture now saves a matching `*kHz.sub` file (Flipper RAW format) alongside the JSON. Load into qFlipper or any text editor for offline edge analysis.
- Added 4-press session (serial `4`): arms 4 consecutive captures with same `pair_id`, auto-arms next press after each, prints study tips on session complete. Pull SD and compare `edges_us` arrays across 4 files — constant edges = clock/preamble structure, variable edges = rolling counter payload.
- Hardware tests needed: verify .sub file opens in qFlipper; verify 4 JSON files share pair_id.

## 2026-04-28 - Sub-GHz Flipper-inspired features: freq scanner + protocol decode

- Scope: Sub-GHz module (`src/main.cpp`).
- Flipper Zero reference: algorithm behavior from docs.flipper.net/zero/sub-ghz and .sub file-format spec. No source copied.
- Added: frequency scanner (`STATE_SUBGHZ_FREQSCAN`) — scans 300/315/345/433.92/868.35/915 MHz, shows RSSI bars, highlights peak, OK to set active freq.
- Added: `sgActiveFreqMHz` global replaces all hardcoded 433.92 — capture, replay, and file naming are now frequency-aware.
- Added: `tryDecodePrinceton()`, `tryDecodeCAME()`, `tryDecodeNiceFlo()` — TE-based decoders; Princeton PT2262 (24-bit), CAME (12-bit), Nice Flo (12-bit). Result shown on OLED + saved in capture JSON as `decoded_protocol`/`decoded_bits`.
- File naming: `<millis>_<freq>kHz.json` (e.g. `114442_433920kHz.json` unchanged at default; `114442_315000kHz.json` at 315 MHz). SD picker updated to accept any `*kHz.json`.
- Serial `f` key in Sub-GHz state enters freq scanner.
- Hardware tests needed: verify decoder output on Princeton remote; verify 315/868 capture + replay; verify freq scan RSSI bars update live.

## 2026-04-28 - Sub-GHz press-1 direct replay path added

- Scope: Sub-GHz input handler (`src/main.cpp` STATE_SUBGHZ).
- Bug: after capturing press 1 (rolling code flow), `e`/OK always armed press 2 — no path to replay press 1 directly.
- Fix: added serial `r` key = replay current capture immediately, clears `sgNeedSecondCapture`.
- OLED hint updated: when press-1 captured and press-2 not yet done, shows "OK=p2 r=replay1" instead of misleading "OK replay".
- Use case: press fob while car is out of range (code not consumed), move into range, press `r` to test if receiver accepts uncounsumed rolling token.
- Hardware test still needed: confirm `r` triggers RF TX and saves replay JSON correctly.

## 2026-04-27 - Wi-Fi Revamp Build Check

- Scope: F4/F5/F6 Wi-Fi revamp code path in `src/main.cpp`.
- Marauder reference use: behavior only, not source. Adopted same-channel SoftAP + raw deauth coexistence pattern from the PRD-described ESP32 Marauder attack loop.
- Result: local PlatformIO build passed. Hardware flash/test not run; operator flashes manually.
- Hardware tests still needed: AP scan count, station discovery, single/all-discovered/broadcast deauth, VariPortal captive redirect, SD theme render, same-channel deauth + VariPortal coexistence.
