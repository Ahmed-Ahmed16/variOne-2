# Attack Control Room — presenter runbook

The Theater is a keyboard-driven, presenter-paced glow-wall (`companion/app/theater/`).
Source is swappable; the UI is identical on mock and live.

## Launch

```bash
cd companion
npm run dev            # http://localhost:3000/theater?mode=demo
# or the serverless export on the projector laptop (Chromium):
npm run build && open out/theater/index.html
```

URL modes (the wall can't tell which is which):

| `?mode=` | Source | When |
|----------|--------|------|
| `demo` (default) | in-page mock, fully scripted | rehearsal + bulletproof fallback |
| `serial` | Web Serial → plugged-in device | live (Chromium only; click **connect device** to grant the port) |
| `replay` | (shares the mock spine for now) | recorded run / non-Chromium |

## Keys

| Key | Action |
|-----|--------|
| `→` / `Space` / `Enter` | next act (arms its source) |
| `←` | previous act |
| `R` | redact ⇄ reveal (MACs / SSIDs) — default **redacted ON** |
| `V` | mute ⇄ unmute Vemo voice-over |
| `0` | reset counters + jump to act 1 |
| click a rail item | jump straight to that act |

## Running order + how to arm each act on the device (live mode)

Mock plays all of these automatically; the paths below are only for `?mode=serial`.

| # | Act | Device menu path | Notes |
|---|-----|------------------|-------|
| 00 | Recon opener *(optional)* | WiFi → Phone Probes *(hidden)* | needs the optional `>> VARIPROBE:` firmware line, else mock/TFT only |
| 01 | BLE blitz | Bluetooth → BLE Popup Spam → **iBeacon ("VariOne")** | needs the **iBeacon menu re-enable** (firmware) to be pickable |
| 02 | Targeted deauth | WiFi → Wifi Atks → Target Atks → **your own router** → Deauth | targets ONLY your AP; say "check your WiFi again" when frames climb |
| 02b | Clone & rejoin | WiFi → Wifi Atks → Target Atks → your AP → **Deauth+Clone** | optional `+1` firmware line gives the live "clients rejoined" count |
| 03 | BadUSB | BadUSB → SD/LittleFS → pick payload → start | wall scroll is client-side (`duckyScroll`), synced to playback timing |
| 04 | Debrief | — | aggregate-only recap; the awareness payload |

## Reliability / degradation

- **Spine = BLE blitz + BadUSB** (controllable, repeatable). If any other act misfires
  live, just press `→` and move on — or stay on `?mode=demo`, which is indistinguishable.
- The BadUSB scroll falls back to an embedded copy of the payload if the file fetch
  fails, so it never depends on a path or the network.
- iOS BLE-popup patches mean room reach varies → the **burst ticker + Vemo carry the beat**
  even if few phones actually pop.

## Safety (baked in)

- Deauth hits **only the presenter's own router** (committee pre-joined it); venue WiFi,
  projector, and the offline-local site stay up.
- BadUSB targets **your own / consenting stage laptop**, never the committee's.
- Credentials are **count + event only** on the wall — raw values never leave the device
  and never render. Redaction is default-ON for any MAC/SSID. Everything is local; nothing
  is transmitted off the laptop.

## Voice-over

Record one short MP3 per act into `public/vemo/vo/<act>.mp3` (see that dir's README).
Missing clips simply run silent — the show never depends on audio. `V` mutes live.
