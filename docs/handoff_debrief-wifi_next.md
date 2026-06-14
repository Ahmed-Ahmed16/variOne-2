# Handoff — AI Debrief + WiFi (next session)

**Branch:** `varione-features` (off frozen tag `og-bruce-s3-working` = clean Bruce + board profile).
**Plan of record:** `docs/varione-features-plan.md`. **Backup:** `backups/og-bruce-s3-working/`.
**Build:** `pio run -e varione-s3`  ·  **Flash:** `... -t upload`  ·  **Erase:** `... -t erase`  ·  **Monitor:** `pio device monitor -e varione-s3` (115200).

## ⚠️ STATE AS OF END OF NIGHT (2026-06-12) — read this first
**Confirmed working** on the current build: **Start WiFi AP broadcasts**, phone connects,
**WebUI works**. Radio + firmware are good. (Firmware id check: serial prints `[AP][diag]`
and `[AP] SSID: BruceNet IP: 192.168.4.1` — old/wrong builds print only `IP: 172.0.0.1`.)

**THE ONE REMAINING BUG: a deauth attack breaks the AP afterward.** After running deauth,
Start WiFi AP / WebUI / debrief stop broadcasting (or connect fails). This still happens
*with* `WiFi.persistent(false)` — so it is NOT purely NVS persistence. Two sub-questions to
answer FIRST next session, on the CONFIRMED-correct firmware (not a stale flash):
1. After a deauth breaks it, does a **plain reboot** restore the AP, or only `-t erase`?
   - reboot restores → RAM driver state → fix = full `esp_wifi_stop()/deinit()` + reinit after attacks.
   - only erase restores → still some NVS write slipping through → find/stop that write.
2. Does **deauth actually disconnect a real device** (not just "frames sent" in serial)?
   Confirms TX genuinely works (operator was on 5GHz, could not test yet).

Also: BruceNet has a **password** (shown as `pwd:` on the Start-WiFi-AP screen) — "connection
failed" earlier was an auth mismatch. The **debrief AP is now OPEN** (commit, see below) so the
QR one-scan join can't fail on auth — test the debrief once the post-attack AP bug is fixed.

## Debrief flow — ready to test once AP survives attacks
`debrief` module builds the report + serves it on an OPEN AP (`bruceConfig.wifiAp.pwd` blanked
in-memory around `_setupAP`), QR = open join. Runs from `wifi_atk_menu` after the attack
unwinds (off the attack stack). Blocked only by the post-attack AP bug above.

## The WiFi saga — root cause (took most of the session)
Symptom: SoftAP reports up (`mode=2, country=EG, 20dBm`) but **never broadcasts / stations stays 0**;
survives reboot, only a full chip erase fixes it; an attack re-breaks it.
**Root cause: NVS WiFi-config corruption.** The WiFi lib persisted AP/STA config to NVS; the attack
code reconfigures WiFi repeatedly and a write corrupted during a deauth TX power-sag poisons the NVS
WiFi entry → every later SoftAP fails. Fix = `WiFi.persistent(false)` at boot (`src/main.cpp` setup)
so nothing is ever written to WiFi NVS. Needs ONE erase to clear the already-poisoned NVS.
(Also recorded in memory `softap-nvs-rootcause.md`.)

## Done this session (all build green)
- `ee9d5a3` SoftAP fix (softAP-before-config, pwd/ssid guards) · `da49d93` country=EG/MANUAL + `[AP][diag]`.
- `822f2b2` AI Debrief module `src/modules/varione/debrief/` (deauth+beacon) · `9dd024d`/`925df34`/`5f565ea` debrief AP via proven `_setupAP()` on BruceNet.
- `fa45dd8` debrief runs OFF the attack stack (arm + `debriefRunPending()` in `wifi_atk_menu`) — fixed a stack-overflow crash.
- `5fa07ff` **InputHandler task stack 2048→8192** in `boards/varione-s3/varione-s3.ini` — fixed deauth `Stack canary` Guru Meditation (latent Bruce board-config bug, not the debrief).
- `6bb316f` beacon hop reverted to per-pass (per-SSID hop was too fast to catch).
- `5816b79` `WiFi.persistent(false)` — the root NVS fix (pending hw verify).

## Known-good facts
- Deauth crash FIXED (stack bump). Targeted "Deauth" runs without panic.
- SoftAP visibility needs: clean NVS (erase) + country=EG + persistent(false). All in place.
- WebUI works via **Files → WebUI → AP mode** (NOT "Start WiFi AP", which is a bare AP with no server).

## Next tasks (in order)
1. **Verify the persistent(false) fix** (above). If good, the debrief end-to-end should finally work.
2. **Targeted deauth has no Debrief** — only *Deauth Flood* + *Beacon SPAM* are wired. Add `debriefArm*` to the targeted-deauth exit if wanted.
3. **Move WebUI menu entry** from Files (`FileMenu.cpp:12`) to the WiFi menu (operator requested).
4. Finish AI Debrief: **Evil Portal** debrief + a **Debrief menu** to re-open saved `/debrief/*.html`.
5. **RF rolling-code test** (plan §3) · **Declutter** (plan §4) · **BadUSB demo scripts** (plan §5).
6. **Beacon spam visibility** — still unconfirmed across channels; revisit after WiFi is stable.

## If AP STILL fails after erase + persistent(false)
- Get the `[DEBRIEF][diag] stations=` line: 0 = phone never associates; →1 = associated (then it's captive/DHCP, not the AP).
- Try a full driver reset before the debrief AP: `esp_wifi_stop(); esp_wifi_deinit();` then re-init (Bruce's `wifi_complete_cleanup()` deliberately skips deinit).
- Consider making the debrief AP **open** (no password) to kill any QR-credential/saved-profile mismatch.

## Do NOT do without reading first
- Don't touch tag `og-bruce-s3-working` or `backups/`. Don't re-add the mascot/VariOne overlay (this branch is stock Bruce UI).
- Don't run CC1101 + nRF24 together. Declutter = menu-hide only, no source deletion.
- Debrief MUST run from a shallow stack (menu), never nested inside an attack loop (caused the earlier crash).
