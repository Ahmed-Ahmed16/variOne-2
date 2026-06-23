import type { ActivityEvent } from "../lib/types";
import { XP } from "../lib/gamification";
import { timeAgo } from "../lib/format";

const KIND_ICON: Record<string, string> = {
  attack: "💥",
  capture: "📥",
  debrief: "🎓",
  session: "🟢",
};

/** Reverse-chronological feed of recent device operations. */
export default function OpsTimeline({ events }: { events: ActivityEvent[] }) {
  return (
    <div className="card">
      <h2>📜 Recent operations</h2>
      <div className="timeline">
        {events.map((e) => (
          <div className="row" key={e.id}>
            <div className={`dot ${e.kind}`}>{KIND_ICON[e.kind] ?? "•"}</div>
            <div className="meta">
              <div className="lab">{e.label}</div>
              {e.detail && <div className="det">{e.detail}</div>}
            </div>
            <div className="xp">+{XP[e.kind]}</div>
            <div className="ago">{timeAgo(e.ts)}</div>
          </div>
        ))}
      </div>
    </div>
  );
}
