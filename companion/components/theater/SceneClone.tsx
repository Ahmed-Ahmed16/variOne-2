"use client";

// Clone & rejoin — the "they came back to ME" beat right after the deauth. Once
// knocked off their AP, phones hunt for the same SSID and auto-join our clone.
// Counters: clients rejoined (good) + credentials captured as a COUNT ONLY —
// raw values never leave the device, so the wall shows a number, never a string.

import SceneFrame from "./SceneFrame";
import CounterTicker from "./CounterTicker";
import MechanismDiagram from "./MechanismDiagram";
import type { SceneState } from "../../lib/theater/types";

const PHONES = 6;

export default function SceneClone({ clone }: { clone: SceneState["clone"] }) {
  const moved = Math.min(PHONES, clone.clients);
  return (
    <SceneFrame act="clone" extra={<MechanismDiagram act="clone" />}>
      <CounterTicker value={clone.clients} label="rejoined the clone" accent="good" big />
      <CounterTicker
        value={clone.credCount}
        label="credentials captured"
        accent="crit"
        sub="count only — values stay on device"
      />
      <div className="migrate" aria-hidden>
        <span className="mg-end off">📶 their AP</span>
        <span className="mg-arrow">➜</span>
        <span className="mg-end clone">🪤 our clone</span>
        <div className="mg-phones">
          {Array.from({ length: PHONES }).map((_, i) => (
            <span key={i} className={`mg-ph ${i < moved ? "moved" : ""}`}>
              📱
            </span>
          ))}
        </div>
      </div>
    </SceneFrame>
  );
}
