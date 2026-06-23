"use client";

// Full-bleed control-room shell: brand + source badge up top, the act rail down
// the left (click or key to jump), the active scene filling the stage, and a
// keyboard-hint bar along the bottom. Pure chrome — all data lives in the scene
// passed as `children`; this only frames it and shows where we are in the show.

import type { ActMeta } from "../../lib/theater/show";
import type { AttackSource } from "../../lib/theater/types";
import { asset } from "../../lib/asset";
import SourceBadge from "./SourceBadge";

export default function ControlRoomStage({
  acts,
  currentIdx,
  onSelect,
  sourceKind,
  status,
  onConnect,
  muted,
  redacted,
  children,
}: {
  acts: ActMeta[];
  currentIdx: number;
  onSelect: (i: number) => void;
  sourceKind: AttackSource["kind"];
  status: AttackSource["status"];
  onConnect: () => void;
  muted: boolean;
  redacted: boolean;
  children: React.ReactNode;
}) {
  const total = acts.length;
  return (
    <div className="theater">
      <header className="t-top">
        <div className="t-brand">
          <img src={asset("/vemo/logo.png")} alt="VariOne" width={84} height={34} />
          <div className="t-brandtext">
            <b>
              VAR<i>I</i>ONE
            </b>
            <span>Attack Control Room</span>
          </div>
        </div>
        <div className="t-topright">
          <span className="t-pos">
            {String(currentIdx + 1).padStart(2, "0")} / {String(total).padStart(2, "0")}
          </span>
          <SourceBadge kind={sourceKind} status={status} onConnect={onConnect} />
        </div>
      </header>

      <div className="t-body">
        <nav className="t-rail" aria-label="Acts">
          {acts.map((a, i) => (
            <button
              key={a.id}
              className={`t-railitem ${i === currentIdx ? "on" : ""} ${
                i < currentIdx ? "done" : ""
              }`}
              onClick={() => onSelect(i)}
            >
              <span className="t-railkick">{a.kicker}</span>
              <span className="t-railtitle">{a.title}</span>
              {a.optional ? <span className="t-railopt">optional</span> : null}
            </button>
          ))}
        </nav>

        <main className="t-stage">{children}</main>
      </div>

      <footer className="t-keys">
        <span>
          <kbd>→</kbd>/<kbd>Space</kbd> next
        </span>
        <span>
          <kbd>←</kbd> back
        </span>
        <span>
          <kbd>R</kbd> {redacted ? "reveal" : "redact"}
        </span>
        <span>
          <kbd>V</kbd> {muted ? "unmute" : "mute"}
        </span>
        <span>
          <kbd>0</kbd> reset
        </span>
      </footer>
    </div>
  );
}
