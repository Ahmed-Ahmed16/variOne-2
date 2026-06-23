// Demux raw `>>` serial lines into normalized `AttackEvent`s.
//
// STATELESS + PURE: one line in, zero-or-one event out. Shared verbatim by the
// Web Serial driver, the mock source, and the (optional) WS bridge so all three
// feed an identical pipeline. Keeping it stateless means it never needs to
// remember a prior value — fields a line does not carry are emitted as `null`
// and `aggregate.ts` keeps the prior value (see types.ts NULL SEMANTICS).
//
// Line formats are the firmware's own house style (verified against source):
//   >> BLE: iBeacon spam advertising - 120 bursts        (ble_spam.cpp:338)
//   >> BLE: spam stopped                                 (ble_spam.cpp:340)
//   >> DEAUTH: target AA:BB:CC:DD:EE:FF on ch6 - 150 fps, 600 frames total
//                                                        (deauther.cpp:261)
//   >> DEAUTH stopped: 600 frames total                  (deauther.cpp:277)
//   >> VARIPORTAL: creds captured (entry #1) on 'CIC_vari'   (evil_portal.cpp:734)
//   >> VARIPORTAL: 1 clients connected to 'CIC_vari' on ch6  (optional +1 line)
//   >> VARIPROBE: 3 devices, 2 ssids                     (optional +2 lines)

import type { AttackEvent } from "./types";

// --- BLE -------------------------------------------------------------------
const RE_BLE_ADV = /^>>\s*BLE:\s*iBeacon spam advertising\s*-\s*(\d+)\s*bursts/i;
const RE_BLE_STOP = /^>>\s*BLE:\s*spam stopped/i;

// --- DEAUTH ----------------------------------------------------------------
// target <MAC> on ch<N> - <fps> fps, <total> frames total
const RE_DEAUTH = /^>>\s*DEAUTH:\s*target\s+([0-9A-Fa-f:]+)\s+on\s+ch(\d+)\s*-\s*(\d+)\s*fps,\s*(\d+)\s*frames total/i;
const RE_DEAUTH_STOP = /^>>\s*DEAUTH stopped:\s*(\d+)\s*frames total/i;

// --- VARIPORTAL (clone) ----------------------------------------------------
const RE_PORTAL_CREDS = /^>>\s*VARIPORTAL:\s*creds captured\s*\(entry #(\d+)\)/i;
// optional +1 line: "<n> clients connected to '<ssid>' on ch<c>"
const RE_PORTAL_CLIENTS = /^>>\s*VARIPORTAL:\s*(\d+)\s*clients connected/i;

// --- VARIPROBE (recon, optional) -------------------------------------------
const RE_PROBE = /^>>\s*VARIPROBE:\s*(\d+)\s*devices,\s*(\d+)\s*ssids/i;

/**
 * Parse one serial/mock line into an `AttackEvent`, or `null` if the line is
 * not a recognized `>>` event (boot spam, blank lines, other firmware logs).
 */
export function parseLine(raw: string): AttackEvent | null {
  const line = raw.trim();
  if (!line.startsWith(">>")) return null;

  let m: RegExpMatchArray | null;

  if ((m = line.match(RE_BLE_ADV))) {
    return { act: "ble", bursts: Number(m[1]), running: true };
  }
  if (RE_BLE_STOP.test(line)) {
    return { act: "ble", bursts: null, running: false };
  }

  if ((m = line.match(RE_DEAUTH))) {
    return {
      act: "deauth",
      targetMac: m[1].toUpperCase(),
      channel: Number(m[2]),
      fps: Number(m[3]),
      framesTotal: Number(m[4]),
      running: true,
    };
  }
  if ((m = line.match(RE_DEAUTH_STOP))) {
    return {
      act: "deauth",
      targetMac: null,
      channel: null,
      fps: 0,
      framesTotal: Number(m[1]),
      running: false,
    };
  }

  if ((m = line.match(RE_PORTAL_CREDS))) {
    // entry #N is the running cred *count* — never the value.
    return { act: "clone", clients: null, credCount: Number(m[1]) };
  }
  if ((m = line.match(RE_PORTAL_CLIENTS))) {
    return { act: "clone", clients: Number(m[1]), credCount: null };
  }

  if ((m = line.match(RE_PROBE))) {
    return { act: "recon", devicesSeen: Number(m[1]), ssidsLeaked: Number(m[2]) };
  }

  return null;
}
