# VariOne — Demo Runbook

Two timed slots: **13-min** (graded, low-risk) and **17-min** (deep-dive, live).
Pair with `docs/DEMO_HARDWARE_GUIDE.md`.

---

## Pre-flight (do once, before walking in)

- [ ] Flashed the current build (`pio run -e varione-s3 -t upload`).
- [ ] **Forget the old "VariOne" network** on every test/judge phone (clears stale WPA2).
- [ ] Phone hotspot up: **Maximize Compatibility ON (2.4 GHz)**, **mobile data ON**.
- [ ] Target router (`Vari_Huawei`) up; pick least-busy of ch 1/6/11 if congested.
- [ ] Device auto-joined the strongest known net (boot, watch serial `STA connected`).
- [ ] **Cap test** passed (D1): NRF + RF TX, no resets/glitch.
- [ ] **Powerbank test** passed (D3): 10+ min idle, no cut.
- [ ] **USB mirror** (E): laptop → `tools/usb-serial-mirror/varione-mirror.html` →
      Connect → Fullscreen → HDMI → projector. Captions + screen showing.
- [ ] All six buttons respond (LEFT 39 · UP 47 · RIGHT 41 · DOWN 42 · OK/SEL 45 · BACK 46).
- [ ] Own/issued **test EMV cards** in pocket (full PAN shows on screen — own cards only).

---

## AI-debrief: who connects to what (the part that confuses people)

Two **distinct** WiFi roles, back to back:

- **Phase 1 — DEVICE is the client.** Device joins your phone hotspot as a **STA** to
  reach the internet and call the AI. Watch serial `[AI][smoke] STA connected: <ip>`.
- **Phase 2 — DEVICE is the access point.** Device switches to the **OPEN `VariOne`** AP
  and serves the report. A **judge's phone joins the device** and scans the QR → captive
  report pops (`stations 0 → 1`, phone gets an IP).

**Stability matrix (verify on HW):**

| # | Check |
|---|---|
| T1 | Cold boot → device joins hotspot (STA) < ~15 s. |
| T2 | After STA→AP switch, judge phone gets a DHCP **lease** (re-seat fix) — not "couldn't connect". |
| T3 | Full cycle ×5 (attack → debrief → phone joins → BACK), **no reboot** — catches teardown leaks. |
| T4 | Heap holds before TLS (no OOM); canned fallback reachable on AI error. |
| T5 | Hotspot OFF → graceful **offline canned** debrief, no hang; device-AP + report still come up. |
| T6 | 2–3 judge phones join the OPEN report AP at once (cap 10), all get leases. |

---

## 13-minute slot (graded — keep it safe)

**5 min video** (with bloopers) · **3 min** intro + recap · **5 min member-led feature tour.**

Mirror via the USB screen mirror throughout (features show live on the projector while
members narrate; the `>>` captions reinforce each step).

**Team members give a brief walk-through of the remaining features** — overview, *not*
deep live attacks (those are the 17-min slot):

1. **BadUSB** — HID payload demo (VariOne / Vemo ducky art): what it types + the risk.
2. **IR** — capture + replay (`>> IR: captured …`); TV / AC remote angle.
3. **NFC / RFID** — tag read; EMV card shows vendor / full PAN / expiry / **Status**
   (own cards only; serial/mirror shows masked last-4).
4. **NRF24** — 2.4 GHz spectrum + jam modes (`>> NRF: jamming …`).
5. **Site simulator** — the in-browser VariOne device simulator on **varione.ai**.
6. **Vemo Academy** — the awareness / learning page.

> **Debrief here = CANNED** (no live cloud risk in the graded slot).

Then the **presenter + friend take over for the 17-min mega** (live attacks + live AI debrief).

---

## 17-minute slot (discussion / deep-dive — live network risk is free)

- **Live AI debrief** — real cloud call via the phone hotspot (Phase 1 → Phase 2 above).
- Judges' laptops on the deployed **varione.ai** site / companion alongside the mirror.
- Deeper attacks, BLE/NRF kill demos (pick the mode by the target — see hardware guide /
  jammer notes), Q&A.

---

## If something slips

- **Mirror flaky** → WebUI spectator (zero build), but **no live WiFi attacks** while it
  holds the AP. Show attacks in a separate non-mirrored beat.
- **Cloud debrief errors / hotspot down** → offline **canned** debrief (T5) — still pops
  the report AP + QR.
- **Phone won't join VariOne** → it's a stale WPA2 cache: **forget network**, rejoin (AP
  is OPEN now).
