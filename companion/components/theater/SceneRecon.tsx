"use client";

// Recon opener — the quiet inhale before the punches. "I already know you."
// Devices + leaked-SSID counts creep up while a cloud of *remembered networks*
// fills in — the names phones broadcast in probe requests. The names shown are
// illustrative + redacted by default (real names would come from the optional
// VARIPROBE feed and never leave the laptop). Optional act; degrades gracefully.

import SceneFrame from "./SceneFrame";
import CounterTicker from "./CounterTicker";
import { redactSsid } from "../../lib/theater/redact";
import type { SceneState } from "../../lib/theater/types";

// Illustrative remembered-network names (NOT real captures). Revealed as the
// leaked-SSID count climbs; redacted to spotlight-only unless R is pressed.
const REMEMBERED = [
  "CIC-Student",
  "Starbucks WiFi",
  "iPhone (Ahmed)",
  "Home_5G",
  "TEData_2.4G",
  "Galaxy A52",
  "Airport_Free",
  "Vodafone-7F2C",
];

export default function SceneRecon({
  recon,
  redacted,
}: {
  recon: SceneState["recon"];
  redacted: boolean;
}) {
  const shown = Math.min(REMEMBERED.length, recon.ssidsLeaked);
  return (
    <SceneFrame act="recon">
      <CounterTicker value={recon.devicesSeen} label="devices in the room" accent="cy" big />
      <CounterTicker value={recon.ssidsLeaked} label="networks they remember" accent="warn" />
      <div className="ssid-cloud" aria-hidden>
        {REMEMBERED.slice(0, shown).map((name) => (
          <span key={name} className="ssid-chip">
            📶 {redactSsid(name, redacted)}
          </span>
        ))}
      </div>
    </SceneFrame>
  );
}
