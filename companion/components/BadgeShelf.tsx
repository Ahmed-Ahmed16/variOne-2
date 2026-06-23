import type { Badge } from "../lib/types";

/** Achievement shelf — earned badges glow gold, locked ones are dimmed. */
export default function BadgeShelf({ badges }: { badges: Badge[] }) {
  const earned = badges.filter((b) => b.earned).length;
  return (
    <div className="card">
      <h2>
        🏆 Badges <span style={{ marginLeft: "auto", color: "var(--gold)" }}>{earned}/{badges.length}</span>
      </h2>
      <div className="badges">
        {badges.map((b) => (
          <div className={`badge ${b.earned ? "on" : ""}`} key={b.id} title={b.desc}>
            <div className="em">{b.icon}</div>
            <div className="bn">{b.name}</div>
            <div className="bd">{b.desc}</div>
          </div>
        ))}
      </div>
    </div>
  );
}
