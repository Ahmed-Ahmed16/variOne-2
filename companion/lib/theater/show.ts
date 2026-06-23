// The show's running order + per-act display metadata.
//
// One source of truth for the act rail, scene headers, the mechanism reveal
// titles, and the one-line defend tip. The escalation order is locked in the
// wow-effect plan: quiet recon inhale → BLE blitz → targeted deauth → clone
// rejoin → badUSB owns a laptop → Vemo debrief grin.

import type { Act } from "./types";

export interface ActMeta {
  id: Act;
  /** Big scene title. */
  title: string;
  /** Short kicker above the title. */
  kicker: string;
  /** The "how it works" reveal headline (the part the audience doesn't know). */
  mechanism: string;
  /** One-line defence the debrief drives home. */
  defend: string;
  /** Whether this act is part of the optional/degradable set (not the spine). */
  optional?: boolean;
}

/** Full running order, including the optional recon opener + debrief closer. */
export const ACTS: ActMeta[] = [
  {
    id: "recon",
    kicker: "00 · opener",
    title: "I already know you",
    mechanism: "Phones shout probe requests for every network they remember.",
    defend: "Forget old networks; disable auto-join on public SSIDs.",
    optional: true,
  },
  {
    id: "ble",
    kicker: "01 · proximity",
    title: "BLE room blitz",
    mechanism: "A spoofed BLE advertisement fakes a nearby-device pairing popup.",
    defend: "Turn off “nearby device” / proximity sharing.",
  },
  {
    id: "deauth",
    kicker: "02 · knock-off",
    title: "Targeted deauth",
    mechanism: "802.11 deauth frames are unauthenticated — anyone can forge them.",
    defend: "WPA3 / 802.11w (PMF); verify the network you join.",
  },
  {
    id: "clone",
    kicker: "02b · rejoin us",
    title: "Clone & rejoin",
    mechanism: "Knocked off their AP, devices auto-join an identical-named clone.",
    defend: "Don’t auto-reconnect; check for the lock + the real cert.",
  },
  {
    id: "badusb",
    kicker: "03 · keystrokes",
    title: "BadUSB owns a laptop",
    mechanism: "It’s not a USB stick — it’s a keyboard that types faster than you.",
    defend: "Lock your screen; restrict USB HID; don’t trust found drives.",
  },
  {
    id: "debrief",
    kicker: "04 · debrief",
    title: "Here’s how each one dies",
    mechanism: "Four attacks, under five minutes — and every one has a defence.",
    defend: "Awareness is the patch. You just watched all four.",
  },
];

/** The acts in presenter-advance order. */
export const ACT_ORDER: Act[] = ACTS.map((a) => a.id);

/** Look up an act's metadata. */
export function actMeta(act: Act): ActMeta {
  return ACTS.find((a) => a.id === act) ?? ACTS[0];
}

/** The reliable spine (always rehearsable); the rest degrade gracefully. */
export const SPINE_ACTS: Act[] = ["ble", "badusb"];
