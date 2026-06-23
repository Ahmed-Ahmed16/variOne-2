"use client";

// BLE room blitz: the burst ticker climbs while a wall of phones pops into the
// "got a popup" state in time with the count — the visual proxy for real phones
// lighting up in the room (iOS patches mean reach varies, so the ticker + the
// pop wall carry the beat even on a hardened room). The mechanism reveal (how a
// BLE advert spoofs a proximity pairing) is added in MechanismDiagram (Batch 6).

import SceneFrame from "./SceneFrame";
import CounterTicker from "./CounterTicker";
import MechanismDiagram from "./MechanismDiagram";
import type { SceneState } from "../../lib/theater/types";

const PHONES = 14;

export default function SceneBLE({ ble }: { ble: SceneState["ble"] }) {
  // map burst count → how many phones have "popped" (saturates the wall).
  const lit = Math.min(PHONES, Math.round(ble.bursts / 11));

  return (
    <SceneFrame
      act="ble"
      running={ble.running}
      runLabel="advertising"
      extra={<MechanismDiagram act="ble" />}
    >
      <CounterTicker value={ble.bursts} label="bursts broadcast" accent="cy" big />
      <CounterTicker value={ble.peakBursts} label="peak reached" accent="warn" />
      <div className="phonewall" aria-hidden>
        {Array.from({ length: PHONES }).map((_, i) => (
          <span key={i} className={`ph ${i < lit ? "pop" : ""}`}>
            📱
          </span>
        ))}
      </div>
    </SceneFrame>
  );
}
