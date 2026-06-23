"use client";

// Shared scaffold every act scene sits in: the kicker + big title up top, a
// left column for the act's live counters (passed as children), and a right
// column with the "how it works" mechanism reveal + the one-line defend tip —
// both pulled from the act's metadata so wording lives in one place (show.ts).
// The mechanism reveal is the differentiator: you DID it, and you explain WHY.

import type { Act } from "../../lib/theater/types";
import { actMeta } from "../../lib/theater/show";

export default function SceneFrame({
  act,
  running,
  runLabel,
  extra,
  children,
}: {
  act: Act;
  /** Show a live/idle running chip (e.g. deauth firing). */
  running?: boolean;
  runLabel?: string;
  /** Optional node rendered under the mechanism panel (e.g. a diagram). */
  extra?: React.ReactNode;
  /** The act's counter cluster. */
  children: React.ReactNode;
}) {
  const m = actMeta(act);
  return (
    <section className="scene" key={act}>
      <div className="scene-kick">{m.kicker}</div>
      <h1 className="scene-title">{m.title}</h1>

      <div className="scene-body">
        <div className="counters">
          {children}
          {running !== undefined ? (
            <div className={`runchip ${running ? "live" : "idle"}`}>
              <span className="dot" />
              {running ? (runLabel ?? "firing") : "armed / idle"}
            </div>
          ) : null}
        </div>

        <aside className="sidepanel">
          <div className="mech">
            <h3>How it works</h3>
            <p>{m.mechanism}</p>
          </div>
          {extra}
          <div className="defend">
            <span className="shield">🛡️</span>
            <span className="dtext">
              <b>Defend</b>
              <span>{m.defend}</span>
            </span>
          </div>
        </aside>
      </div>
    </section>
  );
}
