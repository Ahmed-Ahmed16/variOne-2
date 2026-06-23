"use client";

// The honest LIVE / DEMO / REPLAY pill. It reports what the pipeline is ACTUALLY
// wired to — it never fakes "live". In serial mode before a port is granted it
// offers a Connect button (Web Serial needs a user gesture); a real, connected
// device link is the only thing that lights the green LIVE state.

import type { AttackSource } from "../../lib/theater/types";

export default function SourceBadge({
  kind,
  status,
  onConnect,
}: {
  kind: AttackSource["kind"];
  status: AttackSource["status"];
  onConnect: () => void;
}) {
  if (kind === "serial") {
    const connected = status === "connected";
    return (
      <div className={`srcbadge ${connected ? "live" : "warn"}`}>
        <span className="dot" />
        {connected ? (
          <span>LIVE · device</span>
        ) : (
          <button className="connectbtn" onClick={onConnect}>
            {status === "connecting"
              ? "connecting…"
              : status === "error"
                ? "retry connect"
                : "connect device"}
          </button>
        )}
      </div>
    );
  }

  if (kind === "replay") {
    return (
      <div className="srcbadge cy">
        <span className="dot" />
        <span>REPLAY</span>
      </div>
    );
  }

  return (
    <div className="srcbadge demo">
      <span className="dot" />
      <span>DEMO · rehearsal</span>
    </div>
  );
}
