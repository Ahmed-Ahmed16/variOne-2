# VariOne — Demo Hardware Guide

Pre-demo hardware checklist and reference. Firmware-independent (no code here) —
pair with `docs/DEMO_RUNBOOK.md` for the show flow.

> Pin map reflects the current board: **UP=47, RIGHT=41** (changed from 40/2).

---

## D1 · NRF24 + CC1101 bulk caps — verify

1000 µF bulk caps were added across the NRF24 and CC1101 supply. Confirm they
actually killed the brown-out glitches:

- **Cap test (run once after flashing):** run NRF spectrum + jammer, then CC1101/RF
  TX, back-to-back ~2 min each. **Pass = no resets, no "NRF not found", no screen
  flicker** on TX bursts.
- If still glitchy: add a **100 nF ceramic in parallel** at the module supply pins
  (the bulk cap doesn't cover HF decoupling).
- **Polarity:** stripe = −. A backwards electrolytic runs hot — check before power-on.
- Reference pins (this S3 board): NRF **CE=17, CSN=15**; shared SPI **SCK=12 / MOSI=11
  / MISO=13**; both modules on **3.3 V** (CC1101 is 3.3 V only — never 5 V).

---

## D2 · Router + network

Two **separate** 2.4 GHz networks — don't conflate them:

| Network | Role |
|---|---|
| **Huawei WiFi4** (`Vari_Huawei` class) | No-internet target: deauth target, VariPortal clone source, STA-reconnect stability target. |
| **Phone hotspot** (`Vari_hotspot` class) | The **only** internet path for the AI cloud call. **Maximize Compatibility ON (2.4 GHz)**, **mobile data ON**. |

- WiFi creds are seeded from `src/modules/varione/varione_secrets.h` (gitignored).
  The device **auto-joins the strongest known network** — no manual selection.
- If the room is congested: pin the target router to the **least-busy of ch 1/6/11**
  (check with the device's own WiFi scan at the venue) so deauth/clone aren't fighting
  the room.

> The **VariOne AP** the phones join for the report is now **OPEN** (no password) —
> same SSID everywhere (debrief, Start-WiFi-AP, WebUI), so phones stop flip-flopping
> between WPA2 and open. **Forget any old "VariOne" network** on each test phone once
> before the demo to clear a stale WPA2 entry.

---

## D3 · Powerbank (Type-C ↔ Type-C)

- **Risk: low-current auto-shutoff.** ESP32-S3 idle (~80–120 mA) can fall below the
  powerbank's keep-on threshold → it cuts power → device reboots.
- **Test:** leave the device idle on the powerbank **10+ min**; confirm it stays up.
  If it cuts: keep a feature running, use a powerbank without auto-off, or add a small
  keep-alive load.
- Use a **data+power** capable cable (the same one feeds the USB screen mirror).

---

## D4 · (Deferred) 2N2222 IR driver — guide only, build on a SPARE

**Do not solder to the demo unit before the demo.** For a spare unit only:

- IR TX = **GPIO18** (LEDC 38 kHz, 50% duty).
- GPIO18 → ~1 kΩ → 2N2222 **base**; IR LED anode → 5 V (powerbank VBUS) → 10 Ω limit →
  LED → **collector**; **emitter** → GND. (100 Ω works as a base-limit alternative.)

---

## D5 · Button pin map + functions

Active-**LOW**, `INPUT_PULLUP`. Six logical inputs; **OK and SEL share GPIO45**.
Long-press supported.

| Button | GPIO | Function |
|---|---|---|
| LEFT  | 39 | navigate left / previous / value − |
| UP    | **47** | navigate up / scroll up |
| RIGHT | **41** | navigate right / next / value + |
| DOWN  | 42 | navigate down / scroll down |
| OK / SEL | 45 | select / confirm / enter (same physical line) |
| BACK  | 46 | back / cancel (short = ESC); long-press = exit feature |

> Defined in `boards/varione-s3/pins_arduino.h` **and** `boards/varione-s3/varione-s3.ini`
> (`-DUP_BTN`/`-DR_BTN`) — both must agree. If a button feels dead at the venue it's
> almost always a stuck-LOW / bridged solder joint on these exact pins, not firmware.

**Wiring a 4-leg tactile button (the common gotcha):** the 4 legs are **two
internally-shorted pairs** (same-side legs are permanently connected; pressing bridges
the two sides).

- Each button needs **2 wires**: one **GPIO** + one **GND**.
- Wire to legs on **opposite sides** (diagonal across the body). **Never** two legs from
  the same side — reads "pressed" forever.
- No resistor needed (`INPUT_PULLUP`, active-LOW): idle = HIGH, press = LOW.
- Continuity check: multimeter beeps across your two chosen legs **only when pressed**.
  If it beeps un-pressed, you grabbed a same-side pair — move one wire to the diagonal.
- Mapping: GPIO leg → 39 / 47 / 41 / 42 / 45 / 46; GND leg → common ground rail.

---

## D6 · USB screen mirror (Part E)

`tools/usb-serial-mirror/varione-mirror.html` — open in **Chrome/Edge desktop**,
Connect, pick the VariOne USB port (the one you monitor at 115200), Fullscreen →
HDMI → projector. Works **during live attacks** (rides USB serial, no WiFi).

- Pre-demo: confirm the mirror shows menus + the `>>` narration captions, and that it
  survives a deauth/VariPortal run without dropping the port.
- Fallback if it misbehaves: WebUI **spectator** (zero build) — but it holds the device
  AP, so no live WiFi attacks while mirroring.
- See `tools/usb-serial-mirror/README.md` for known limits (draw-log dedup ghosting,
  images skipped over USB).
