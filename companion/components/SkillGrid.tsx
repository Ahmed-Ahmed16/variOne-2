import type { SkillInfo } from "../lib/types";
import { pct } from "../lib/format";

/** Per-domain skill levels with progress bars (the "skill tree" axes). */
export default function SkillGrid({ skills }: { skills: SkillInfo[] }) {
  return (
    <div className="card">
      <h2>🧠 Skills</h2>
      {skills.map((s) => (
        <div className="skill" key={s.category}>
          <div className="top">
            <span className="name">
              <span className="ic">{s.icon}</span>
              {s.name}
            </span>
            <span className="lv">
              Lv <b>{s.level}</b> · {s.count} ops
            </span>
          </div>
          <div className="bar">
            <span style={{ width: `${pct(s.xpInLevel, s.xpForLevel)}%` }} />
          </div>
        </div>
      ))}
    </div>
  );
}
