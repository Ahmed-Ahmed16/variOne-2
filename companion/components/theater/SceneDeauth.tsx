"use client";

// Targeted deauth — act 2, the "check your WiFi again" beat. The committee was
// invited onto the presenter's OWN router at session start; this knocks every
// client off it (venue WiFi untouched). Frames + fps climb live; when it's
// firing, a big call-out tells the room to look at their phones. Target MAC is
// redacted by default (R reveals — it's the operator's own gear).

import SceneFrame from "./SceneFrame";
import CounterTicker from "./CounterTicker";
import MechanismDiagram from "./MechanismDiagram";
import { redactMac } from "../../lib/theater/redact";
import type { SceneState } from "../../lib/theater/types";

export default function SceneDeauth({
  deauth,
  redacted,
}: {
  deauth: SceneState["deauth"];
  redacted: boolean;
}) {
  const fired = deauth.framesTotal > 0;
  return (
    <SceneFrame
      act="deauth"
      running={deauth.running}
      runLabel="deauthing"
      extra={<MechanismDiagram act="deauth" />}
    >
      <CounterTicker value={deauth.framesTotal} label="frames sent" accent="crit" big />
      <CounterTicker value={deauth.fps} label="frames / sec" accent="warn" />
      <CounterTicker
        value={deauth.channel}
        label="channel"
        accent="cy"
        sub={deauth.targetMac ? `target ${redactMac(deauth.targetMac, redacted)}` : "acquiring target…"}
      />
      {deauth.running ? (
        <div className="callout">📣 Check your phones — check your WiFi again.</div>
      ) : fired ? (
        <div className="callout off">— and they all dropped. Only my router was targeted.</div>
      ) : null}
    </SceneFrame>
  );
}
