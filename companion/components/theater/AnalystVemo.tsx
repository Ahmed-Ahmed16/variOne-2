"use client";

// Vemo as the live narrator — the one thing neither rival can clone. Reuses the
// VemoHero idiom (illustrated mood + first-person speech) but as a persistent
// corner panel that reacts to the current act + scene via the director. Keying
// on mood/line re-fires the popin so each new beat lands with a little pop.

import { asset } from "../../lib/asset";
import { directVemo } from "../../lib/theater/vemoDirector";
import type { Act, SceneState } from "../../lib/theater/types";

export default function AnalystVemo({ act, scene }: { act: Act; scene: SceneState }) {
  const d = directVemo(act, scene);
  return (
    <aside className="analyst" aria-live="polite">
      <div className="analyst-bubble" key={d.line}>
        <span className="analyst-mood">
          {d.info.emoji} {d.info.title}
        </span>
        <p>{d.line}</p>
      </div>
      <div className="analyst-vemo">
        <div className="analyst-glow" />
        <img
          key={d.info.mood}
          className="pop"
          src={asset(d.info.art)}
          alt={`Vemo — ${d.info.title}`}
          width={150}
          height={150}
        />
      </div>
    </aside>
  );
}
