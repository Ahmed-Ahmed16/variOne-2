"use client";

// The debrief grin + the awareness payload. AGGREGATE-ONLY: every number here
// is a count from SceneState (bursts, frames, clients, cred COUNT, keystrokes) —
// never a raw credential, MAC or capture. One card per attack that actually
// fired, each pairing what it did with how it dies. This is the teaching beat
// that a chatbot can't perform and a paper won't show live.

import { debriefSummary } from "../../lib/theater/aggregate";
import { actMeta } from "../../lib/theater/show";
import type { Act, SceneState } from "../../lib/theater/types";

interface Card {
  act: Act;
  fired: boolean;
  metric: string;
}

export default function DebriefPanel({ scene }: { scene: SceneState }) {
  const d = debriefSummary(scene);
  const m = actMeta("debrief");

  const cards: Card[] = [
    { act: "ble", fired: d.bleBursts > 0, metric: `${d.bleBursts.toLocaleString()} bursts broadcast` },
    { act: "deauth", fired: d.deauthFrames > 0, metric: `${d.deauthFrames.toLocaleString()} frames · ${d.deauthPeakFps}/s peak` },
    {
      act: "clone",
      fired: d.cloneClients > 0 || d.credCount > 0,
      metric: `${d.cloneClients} rejoined · ${d.credCount} creds (count only)`,
    },
    { act: "badusb", fired: d.badusbLines > 0, metric: `${d.badusbLines} keystrokes, no clicks` },
  ];

  return (
    <section className="scene" key="debrief">
      <div className="scene-kick">{m.kicker}</div>
      <h1 className="scene-title">
        {d.actsFired} attacks · under five minutes · here’s how each one dies
      </h1>

      <div className="debrief-grid">
        {cards.map((c) => {
          const meta = actMeta(c.act);
          return (
            <div key={c.act} className={`dcard ${c.fired ? "fired" : "skipped"}`}>
              <div className="dcard-top">
                <span className="dcard-title">{meta.title}</span>
                <span className="dcard-state">{c.fired ? "✓ landed" : "— not run"}</span>
              </div>
              <div className="dcard-metric">{c.metric}</div>
              <div className="dcard-defend">
                <span className="shield">🛡️</span>
                <span>{meta.defend}</span>
              </div>
            </div>
          );
        })}
      </div>

      <p className="debrief-foot">
        Counts only — no captured value ever left the device. Awareness is the patch;
        you just watched all four.
      </p>
    </section>
  );
}
