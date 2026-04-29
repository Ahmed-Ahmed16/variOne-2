# NEXT — VariOne handoff

## Done this session (NFC track, PRD §9.2)
- `src/main.cpp` — added `STATE_NFC_MENU/_ACCESS/_SAVED`, `NfcMifareCard` struct, 10-key Mifare dict, `nfcReadMifare/SaveMifare/LoadMifare/EmulateStep/WriteToMagicCard/LoadSavedList`, draw fns (`drawNfcMenu/Access/Saved`), input + loop wiring. Added Meeza AID + tags 9F36 (ATC) + 9F4D (tx log) parsing in `nfcParseEmvTlv`; record loop fixed (SFI 1–4, no early stop).
- `lib/PN532Custom/Adafruit_PN532.{h,cpp}` — forked from libdeps, added `AsTargetUID(uid3, sak)` that builds TgInitAsTarget (0x8C) with caller UID. Custom UID emulation works on PN532.
- `platformio.ini` — removed `adafruit/Adafruit PN532`, added explicit `adafruit/Adafruit BusIO@^1.16.1` (was transitive). Build clean: RAM 18.1%, Flash 31.1%.

## In progress
- NFC implementation **complete + compiles**. Not yet flashed/tested on hardware.

## Next 3 tasks (in order)
1. Write `test/test_pn532.cpp` per PRD §13.0 — print firmware version, scan card, dump SAK/ATQA/UID + try one default-key auth on sector 0. Build env already in `platformio.ini` (`pio run -e pn532_test`).
2. Flash main firmware, demo flow on hardware: bank card read → elevator tag dump → emulate at elevator → if reject, write to white magic card → test.
3. Log results in `docs/test-log.md`. Update PRD §9.2 schema for Mifare JSON if it diverged.

## Open decisions / blockers
- Emulation only uses uid[0..2] + computed BCC. If elevator/CIC readers strict-validate uid[3] → emulation fails → magic card path required. Decide after live test.
- Default-key dict won't unlock sector keys that were rotated. If any sector locked → that sector won't clone. Acceptable for demo.

## Warnings — read before changing
- **Do NOT switch PN532 back to SPI** — CLAUDE.md hard rule, I²C config working.
- **Do NOT re-add `adafruit/Adafruit PN532` to lib_deps** — it'll shadow the local fork in `lib/PN532Custom/` and emulation breaks.
- **Do NOT delete `lib/PN532Custom/`** without restoring the lib_deps line first.
- **Never write unmasked PAN** — all EMV display/save paths already mask. Don't bypass `nfcMaskPanDigits`.
- Magic card write irreversibly overwrites block 0 → confirm screen exists, do not auto-trigger.
