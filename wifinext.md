# WiFi Next Handoff

## Completed this session
- [x] Reviewed `wifinext.md` + `wi-1NEXT.md`; used newer `wifinext.md` as active order where it superseded the older hardware gate.
- [x] Patched `test/test_deauth.cpp` Tier 1 so command `t` runs all parity paths:
  - `WIFI_STA` + `en_sys_seq=false` legacy raw TX smoke.
  - `WIFI_STA` + `en_sys_seq=true` firmware standalone path.
  - `WIFI_AP_STA` + `WIFI_IF_AP` + `en_sys_seq=true`, with STA fallback, matching VariPortal co-op `sendMgmtFrame()`.
- [x] Fixed `startPromiscuous()` in `src/main.cpp` so active VariPortal keeps `WIFI_AP_STA` instead of being dropped to `WIFI_STA` during client scan.
- [x] Added `STATE_DEAUTH_CONFIRM` hold gate before deauth TX. Client/mode selection now lands on confirm; physical RIGHT/OK must be held for 1.2 s before `startDeauthAttack()`.
- [x] Verified builds:
  - `pio run -e deauth_test` PASS. RAM 13.5%, Flash 24.2%.
  - `pio run` PASS. RAM 18.1%, Flash 31.2%.

## Handoff context for next task
- No hardware flash/upload was run this session.
- `test/test_deauth.cpp` is now the immediate hardware gate. Run `s`, select AP `0-9`, choose `b` or `c AA:BB:CC:DD:EE:FF`, then `t`.
- Tier 1 PASS now means all three TX profiles report 100/100 deauth, 100/100 disassoc, and `chan mismatch : 0`.
- If AP_STA profile fails but STA true passes, suspect `WIFI_IF_AP` raw TX behavior under SoftAP and check the STA fallback log path in `txOne()`.
- If all Tier 1 profiles pass, continue to Tier 2 sniffer and Tier 3 phone-disconnect proof, then log results in `docs/test-log.md`.

## Remaining todo list
- [ ] Flash + run `deauth_test` on hardware: `pio run -e deauth_test -t upload && pio device monitor -e deauth_test -b 115200`.
- [ ] Tier 1: `s` -> pick AP -> `b` or `c ...` -> `t`; capture serial summary for all three TX profiles.
- [ ] Tier 2: verify frames over the air with laptop monitor mode / airodump-ng or a second ESP32.
- [ ] Tier 3: verify authorized lab phone disconnects from target AP.
- [ ] Write Tier 1/2/3 result to `docs/test-log.md`.
- [ ] Only after Tier 2+ pass: implement `loadSdPortalThemes()` `theme.json` parsing per PRD §11.8.
- [ ] After manifest parsing: start P1 Wi-Fi state collapse (`STATE_DEAUTH_*` + `STATE_ET_*` -> `WIFI_SCAN -> WIFI_SELECT_THEME -> WIFI_ATTACK_RUN -> WIFI_CRED_VIEW`).

## Do not do next
- Do not mark F5/F6 hardware-ready until Tier 2+ is logged.
- Do not add new libraries for `theme.json` parsing without flagging first.
- Do not touch PN532 SPI/I2C wiring or old `v0.1.cpp`/`v0.2.cpp`.
