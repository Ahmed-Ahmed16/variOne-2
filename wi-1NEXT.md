# NEXT — VariOne Wi-Fi Module Handoff

> Superseded for the current Wi-Fi task order by `wifinext.md` (updated after smoke-test parity + SoftAP-preservation work). Keep this file as historical context for the original hardware gate and PRD notes.

## Done this session (non-hw blocker pass)
- **PRD §9.6.1** — added NAPT-not-possible bullet. Verified `framework-arduinoespressif32 .../sdk/esp32/dio_qspi/include/sdkconfig.h` does NOT define `CONFIG_LWIP_IPV4_NAPT`. `lwip_napt.h` ships but `IP_NAPT=0`, `ip_napt_enable()` no-op. VariPortal stays captive-only for MVP.
- **PRD §11.8** — new section, locked VariPortal `theme.json` schema 1. Optional manifest, 9 fields, full type/length table, hard rules on masking + version skipping.
- **NEXT.md** — rewritten (this file).
- **No `main.cpp` edits.** Build untouched.

## Carryover from prior session (still in-progress)
- `test/test_deauth.cpp` + `[env:deauth_test]` in `platformio.ini` (lines 57–60) built clean (RAM 13.5%, Flash 24.1%) — **never flashed to hw yet**. Tier 1/2/3 all unverified.
- Theme loader `loadSdPortalThemes()` at `src/main.cpp` lines 2829–2856 still ignores `theme.json` (only scans for `index.html`). Code edit to read manifest = deferred, **gated on Tier 2+ pass**.

## Next 3 tasks in order
1. Flash + run `deauth_test` on hw: `pio run -e deauth_test -t upload && pio device monitor -e deauth_test -b 115200`. Run Tier 1 (`s` → pick AP → `b` → `t`). PASS = 100/100 ESP_OK both frame types AND `chan mismatch : 0`.
2. Tier 2 sniffer (airodump-ng or 2nd ESP32) + Tier 3 phone-disconnect proof. Log to `docs/test-log.md`.
3. Only after Tier 2+ pass: extend `loadSdPortalThemes()` to parse `theme.json` per PRD §11.8, then start P1 state collapse (`STATE_DEAUTH_*` + `STATE_ET_*` → `WIFI_SCAN → WIFI_SELECT_THEME → WIFI_ATTACK_RUN → WIFI_CRED_VIEW`).

## Open decisions
- Operator phone-hotspot creds: `secrets.h` (gitignored) vs runtime menu. **Needs your call.** Recommend runtime menu.
- `maxSdPortalThemes = 5` in `main.cpp:559` vs prior note "max 10". Pick one before P3.

## Hard rules — read first next session
- **Do NOT touch `main.cpp` deauth path before P0.5 Tier 2+ pass.** Catalog in prior NEXT is reference, not refactor license.
- **Do NOT switch PN532 to SPI** — I²C working.
- **Do NOT add libraries** without flagging in response.
- **Read `variOne/CLAUDE.md` + PRD §9.6.1 + new §11.8 first.** PRD wins on conflict.
- If Tier 1 fails: suspects in order — country code, interface mode (`WIFI_IF_AP` after `WiFi.mode(WIFI_AP)` no-SSID), PMF on target AP, missing `esp_wifi_set_promiscuous(true)` before TX.
