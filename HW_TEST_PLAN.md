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
