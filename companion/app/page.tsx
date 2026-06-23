import { asset } from "../lib/asset";
import { getDeviceStats } from "../lib/sampleData";
import { buildProfile } from "../lib/gamification";
import VemoHero from "../components/VemoHero";
import SkillGrid from "../components/SkillGrid";
import BadgeShelf from "../components/BadgeShelf";
import OpsTimeline from "../components/OpsTimeline";

export default async function Home() {
  const stats = await getDeviceStats();
  const p = buildProfile(stats);

  return (
    <main className="shell">
      <header className="topbar">
        <div className="brand">
          <img src={asset("/vemo/logo.png")} alt="VariOne" width={92} height={38} />
          <div>
            <div className="wm">
              VAR<i>I</i>ONE
            </div>
            <div className="sub">
              {p.deviceName} · {p.firmware} · Companion
            </div>
          </div>
        </div>
        <div className="streak" title={`Best streak: ${p.bestStreak} days`}>
          🔥 <span className="n">{p.streakDays}</span> day streak
        </div>
      </header>

      <div className="grid">
        <div className="stack">
          <VemoHero p={p} />

          <div className="stats">
            <div className="stat">
              <div className="v">{p.counts.attack}</div>
              <div className="l">Attacks</div>
            </div>
            <div className="stat">
              <div className="v">{p.counts.capture}</div>
              <div className="l">Captures</div>
            </div>
            <div className="stat">
              <div className="v">{p.counts.debrief}</div>
              <div className="l">Debriefs</div>
            </div>
            <div className="stat">
              <div className="v">{p.totalXp.toLocaleString()}</div>
              <div className="l">Total XP</div>
            </div>
          </div>

          <OpsTimeline events={p.recent} />
        </div>

        <div className="stack">
          <SkillGrid skills={p.skills} />
          <BadgeShelf badges={p.badges} />
        </div>
      </div>

      <p className="foot">
        Vemo reacts to what you do on the device — run ops, read debriefs, keep your
        streak alive, and watch your level climb.
        <br />
        <span className="seam">
          Demo data shown · the device link feeds real aggregate stats (counts only —
          never captured data).
        </span>
      </p>
    </main>
  );
}
