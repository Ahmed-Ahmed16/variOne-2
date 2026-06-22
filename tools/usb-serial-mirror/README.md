# VariOne USB-Serial Screen Mirror

Mirror the VariOne OLED onto a laptop **over USB**, then HDMI that laptop into
the projector/Casio. Works **during live WiFi attacks** (rides the USB CDC
serial — no WiFi, doesn't hold the device AP like the WebUI spectator does).

## How it works

The firmware already streams its draw-log over USB serial:

- `tft.startAsyncSerial()` spins a background task that emits every draw op as a
  `0xAA`-framed packet (same format the WebUI spectator's `renderTFT` parses).
- The plain-English narration lines (`>> DEAUTH: …`, `>> EMV: …`, etc., Part B3)
  go out the **same** serial port (`serialDevice` wraps `Serial`).

`varione-mirror.html` opens the port with the **Web Serial API**, demuxes the
stream — `0xAA` packets → canvas, `>>` lines → caption ticker — and upscales the
canvas to fullscreen.

**No firmware change, no install, no server.** One self-contained HTML file.

## Use it

1. Chrome or Edge **desktop** (Web Serial; Firefox/Safari won't work).
2. Plug the VariOne into the laptop by USB (the same port you monitor at 115200).
3. Open `varione-mirror.html` (double-click, or drag into the browser).
4. **Connect** → pick the VariOne port. It auto-sends `display start`.
5. **Fullscreen** the canvas → laptop HDMI → projector.

Toggle **Raw log** to see all serial text. Untick *auto "display start"* if you'd
rather start streaming manually (type `display start` in your own serial monitor).

## Known limits (verify on hardware before relying on it)

- The draw-log is **dedup'd** on-device, so a full-screen clear that's identical
  to an earlier one may not re-send → possible ghosting between very similar
  screens. Menus that redraw fully are fine; watch for it on rapid identical
  repaints.
- `DRAWIMAGE` ops (boot logo / Vemo art) need the device's HTTP `/file` endpoint,
  which isn't reachable over USB — images are skipped (vector + text render fine).
- Only one app can hold the serial port. Close other serial monitors first.

**Fallback** (if the mirror misbehaves at the venue): the WebUI **spectator**
(zero build) — but it holds the device AP, so no live WiFi attacks while mirroring.
