# Debrief Wi-Fi "phone can't connect" — root-cause investigation (overnight)

**Date:** 2026-06-19 (late). Branch `finale-fixes`, HEAD `fe0f202`.
**Asked:** find the commit where the debrief Wi-Fi "was fine" and roll back to it.
**Answer: there is no such commit.** The debrief AP-raise code is byte-identical
across every version, including the "confirmed-working" `58580a4`. The bug is a
latent Wi-Fi/netif lifecycle problem, not a regression. Details + fix plan below.

---

## What the hardware logs proved

```
[AP] SSID: VariOne  IP: 192.168.4.1
[AP][diag] mode=2 ch=1 maxtxpow=80(20dBm) country=EG stations=0   <- AP healthy
[DEBRIEF][diag] stations=1 ...   <- phone ASSOCIATES (Layer-2 OK)
...phone never gets an IP, drops...
E (118916) wifi_init_default: netstack cb reg failed with 12308   <- the tell
```

- The AP raises perfectly: correct mode, 20 dBm TX, country EG, IP 192.168.4.1.
- The phone **associates** (`stations=1`) — so beacon + auth + association (L2) work.
- The phone **never gets an IP** — so the DHCP server (L3) is dead. It associates,
  waits for DHCP, times out, drops. The ESP keeps a stale `stations=1` for a while
  after the phone has already given up (association timeout lag) — that's why the
  serial looked like it "held a session."

## Root cause

`esp_netif_create_default_wifi_ap()` (Arduino calls it on every `WiFi.mode(WIFI_AP)`)
registers the **network-stack input callback** that wires the DHCP server to the
radio. When a prior Wi-Fi teardown was not clean, that registration fails —
`netstack cb reg failed with 12308` — and you get an AP that **beacons but has no
working DHCP/IP layer**.

Two framework facts make this happen specifically after an attack:

1. `WiFi.mode(WIFI_OFF)` does **not** destroy the AP netif. It calls `espWiFiStop()`
   and returns (`WiFiGeneric.cpp` mode(): the `cm && !m` branch). The netif is kept
   and **reused** on the next AP raise (only recreated when the pointer is `NULL`,
   `WiFiGeneric.cpp:297`).
2. The deauth/beacon attack path (`wifi_atks.cpp` `wifi_complete_cleanup`,
   `wifi_atk_setWifi`) mixes **direct `esp_wifi_stop()`**, **raw 802.11 injection**
   (`esp_wifi_80211_tx`), and **promiscuous mode** with Arduino's `WiFi.mode()`
   tracking. That desyncs Arduino's cached netif/mode state from the real
   esp_wifi/esp_netif state. The subsequent AP raise then reuses or recreates the
   AP netif in a state where the netstack callback can't (re)register → DHCP dead.

A **clean** "Start WiFi AP" (no attack first) reuses a healthy netif, so it works —
which is exactly what we saw (Start-WiFi-AP eventually connected, debrief never did).

## Why "roll back to the good commit" won't help

- `_setupAP()` (wifi_common.cpp) — **byte-identical** 58580a4 vs HEAD.
- `serveReportLoop()` AP-raise sequence — **identical** 58580a4 vs HEAD.
- `wifi_complete_cleanup` / `wifi_atk_setWifi` / `wifi_atk_unsetWifi` — identical
  except cosmetic Vemo status messages.

The old memory note "debrief AP holds a full stations=1 session" recorded the **L2
association in serial** — it did not confirm a phone actually got an IP and loaded
the report. So this has very likely been broken end-to-end the whole time; it was
just never noticed because `stations=1` looked like success.

---

## Fix plan — try on hardware in this order (one at a time, reflash each)

The fragile part is the esp_netif lifecycle, so each candidate must be HW-tested,
not committed blind (that's what bit us twice already: the DHCP-flush in `_setupAP`
and the dhcps re-seat both made things worse and were reverted).

**Candidate 1 — stop mixing direct esp_wifi calls in the attack teardown (root fix).**
In `wifi_atks.cpp` `wifi_complete_cleanup()`, drop the direct `esp_wifi_stop()` and
let Arduino own the transition (`WiFi.disconnect(true,true)` + `WiFi.mode(WIFI_OFF)`).
Rationale: the direct stop is the main desync source. Lowest-risk, most likely root.

**Candidate 2 — force a full netif rebuild before the debrief AP.**
In `serveReportLoop()` before `_setupAP()`, force Arduino to actually destroy +
recreate the netif. Pure-Arduino, no raw esp_netif handle juggling (which crashed
when tried): cycle `WiFi.mode(WIFI_OFF) → delay → WiFi.mode(WIFI_STA) → delay →
WiFi.mode(WIFI_OFF) → delay` so the cached AP-netif state is forced through a STA
transition and resynced, then let `_setupAP` raise AP. (Toggling through STA is the
safe lever; do NOT call `esp_netif_destroy_default_wifi()` on the handle directly —
Arduino keeps a cached pointer and will use-after-free → crash.)

**Candidate 3 — confirm the hypothesis with a diagnostic first.**
Add a one-line Serial print of the AP netif handle pointer in `serveReportLoop`
right after `_setupAP()` (`esp_netif_get_handle_from_ifkey("WIFI_AP_DEF")`), and of
`esp_netif_dhcps_get_state()`. If the DHCP state is not `ESP_NETIF_DHCP_STARTED`,
that confirms the dead-DHCP theory precisely and tells us if a plain restart helps.

**Candidate 4 — demo fallback if the netif fix proves too fragile.**
Serve the debrief report over USB-serial / the on-screen scroll only (already
exists), and drop the AP-join path for the live demo. Less flashy but reliable.

**Do NOT** reintroduce: `esp_netif_dhcps_stop/start` in `_setupAP` (broke every AP),
the dhcps re-seat with `set_ip_info` (broke association → `stations=0`), or
`softAPConfig()` (kills the beacon on this S3 — see memory).

---

## Other HW results this session (for the record)

- **EMV (Batch 7)** — CONFIRMED WORKING on hardware (card shows all details). Done.
- **Branding / menus / VariPortal label / boot splash** — built green, cosmetic,
  low risk; visually verify on device.
- **Band scanner (Batch 5)** — real root cause found: `setMHZ()` never re-strobes RX
  so the radio never retuned (that's the "frozen 300" / "sees nothing"). Fix shipped
  (`3d75de6`): `ELECHOUSE_cc1101.SetRx()` after each tune + wide RX BW. **Re-test
  with the car key** — this one I'm fairly confident in.
- **NRF mouse/kb jam** — hidden in the demo build (can't disrupt a real HID dongle).
- **IR replay (Batch 4)** — works, blank-screen fixed; replay is on UP which is dead
  on this unit. Easy remap to DOWN when wanted.
- **Targeted-deauth debrief (Batch 3)** — code is in, but it depends on the same
  broken debrief-AP path, so it can't be verified until the Wi-Fi root cause is fixed.

## Branch state
- All finale batches committed on `finale-fixes`. Two bad AP attempts reverted
  (`c4325fd` DHCP flush, `e4b25f0` dhcps re-seat → revert `fe0f202`).
- **Not pushed** to origin (Batch 9 still gated — debrief AP not verified).
- Tree builds green at `fe0f202`.
