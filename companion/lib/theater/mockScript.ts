// The curated full-show event stream — the bulletproof fallback's content.
//
// Each act is a list of timed steps (delay BEFORE the step, relative to the
// previous step in the same act). Numbers + cadence mirror what the real
// firmware prints so `?mode=demo` is indistinguishable from `?mode=serial`:
//   - BLE: bursts climb then "spam stopped" (ble_spam prints ~4×/s).
//   - DEAUTH: ~1 line/s, fps 120-160, frames accumulate, then "stopped".
//   - CLONE: creds + the optional "clients connected" counter trickle in.
//   - RECON: device/SSID counts creep up (the quiet opener).
// badUSB is NOT here — it is rendered client-side by duckyScroll against a real
// `badusb_payloads/*.txt`, identical in mock and live.

import type { Act, AttackEvent } from "./types";

export interface ScriptStep {
  /** ms to wait after the previous step in this act before emitting. */
  delayMs: number;
  event: AttackEvent;
}

const DEMO_MAC = "A4:C1:38:7E:2B:0F"; // plausible vendor OUI, clearly demo
const DEMO_SSID = "CIC_vari";

/**
 * Per-act scripted streams. The presenter advances acts; entering an act plays
 * its steps. Re-entering replays from the top (rehearsable, deterministic).
 */
export const MOCK_SCRIPT: Record<Act, ScriptStep[]> = {
  // ~10s quiet inhale: devices + leaked SSIDs creep up.
  recon: [
    { delayMs: 300, event: { act: "recon", devicesSeen: 2, ssidsLeaked: 1 } },
    { delayMs: 900, event: { act: "recon", devicesSeen: 5, ssidsLeaked: 3 } },
    { delayMs: 1100, event: { act: "recon", devicesSeen: 9, ssidsLeaked: 5 } },
    { delayMs: 1200, event: { act: "recon", devicesSeen: 12, ssidsLeaked: 6 } },
    { delayMs: 1300, event: { act: "recon", devicesSeen: 16, ssidsLeaked: 8 } },
  ],

  // BLE blitz: burst ticker climbs fast, then stops.
  ble: [
    { delayMs: 200, event: { act: "ble", bursts: 8, running: true } },
    { delayMs: 250, event: { act: "ble", bursts: 23, running: true } },
    { delayMs: 250, event: { act: "ble", bursts: 41, running: true } },
    { delayMs: 250, event: { act: "ble", bursts: 62, running: true } },
    { delayMs: 250, event: { act: "ble", bursts: 86, running: true } },
    { delayMs: 250, event: { act: "ble", bursts: 109, running: true } },
    { delayMs: 250, event: { act: "ble", bursts: 131, running: true } },
    { delayMs: 250, event: { act: "ble", bursts: 148, running: true } },
    { delayMs: 600, event: { act: "ble", bursts: null, running: false } },
  ],

  // Targeted deauth on the presenter's own router. fps live, frames accumulate.
  deauth: [
    {
      delayMs: 300,
      event: { act: "deauth", targetMac: DEMO_MAC, channel: 6, fps: 138, framesTotal: 138, running: true },
    },
    {
      delayMs: 1000,
      event: { act: "deauth", targetMac: DEMO_MAC, channel: 6, fps: 154, framesTotal: 292, running: true },
    },
    {
      delayMs: 1000,
      event: { act: "deauth", targetMac: DEMO_MAC, channel: 6, fps: 149, framesTotal: 441, running: true },
    },
    {
      delayMs: 1000,
      event: { act: "deauth", targetMac: DEMO_MAC, channel: 6, fps: 157, framesTotal: 598, running: true },
    },
    {
      delayMs: 800,
      event: { act: "deauth", targetMac: null, channel: null, fps: 0, framesTotal: 598, running: false },
    },
  ],

  // Clone rejoin + creds — counts ONLY, never raw values.
  clone: [
    { delayMs: 600, event: { act: "clone", clients: 1, credCount: null } },
    { delayMs: 900, event: { act: "clone", clients: null, credCount: 1 } },
    { delayMs: 700, event: { act: "clone", clients: 2, credCount: null } },
    { delayMs: 1100, event: { act: "clone", clients: null, credCount: 2 } },
    { delayMs: 800, event: { act: "clone", clients: 3, credCount: null } },
  ],

  // badUSB is driven by duckyScroll (client-side); no scripted events here.
  badusb: [],

  // debrief is a presenter marker; the recap reads accumulated scene state.
  debrief: [{ delayMs: 100, event: { act: "debrief" } }],
};

/** The default SSID/MAC the deauth+clone mock targets (for labels/redaction). */
export const MOCK_TARGET = { mac: DEMO_MAC, ssid: DEMO_SSID };
