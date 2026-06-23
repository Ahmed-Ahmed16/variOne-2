"use client";

import { useState } from "react";
import type { Mood, Profile } from "../lib/types";
import { MOOD_ORDER, moodInfo } from "../lib/gamification";
import { pct } from "../lib/format";
import { asset } from "../lib/asset";

/**
 * Hero panel: Vemo is the star. The mascot shows the *live* mood derived from
 * recent device activity, with a distinct illustrated expression per mood.
 * Tap Vemo (or a mood dot) to cycle expressions — this is the seam that, on the
 * device link, becomes "Vemo reacts in real time to what you just did".
 */
export default function VemoHero({ p }: { p: Profile }) {
  const liveMood = p.mood.mood;
  const [shown, setShown] = useState<Mood>(liveMood);
  const isLive = shown === liveMood;
  const m = moodInfo(shown);
  const progress = pct(p.xpInLevel, p.xpForLevel);

  const cycle = () => {
    const i = MOOD_ORDER.indexOf(shown);
    setShown(MOOD_ORDER[(i + 1) % MOOD_ORDER.length]);
  };

  return (
    <div className="card hero">
      <div className="stage">
        <div className="vemo-col">
          <button
            className="vemo-wrap"
            onClick={cycle}
            title="Tap Vemo to change mood"
            aria-label={`Vemo is ${m.title}. Tap to change mood.`}
          >
            <div className="vemo-glow" />
            {/* key forces a fresh fade-in when the expression changes */}
            <img
              key={m.mood}
              className="vemo-art pop"
              src={asset(m.art)}
              alt={`Vemo — ${m.title}`}
              width={240}
              height={240}
            />
          </button>
          <div className="moodgallery" role="tablist" aria-label="Vemo moods">
            {MOOD_ORDER.map((mood) => (
              <button
                key={mood}
                className={`mdot ${mood === shown ? "on" : ""}`}
                onClick={() => setShown(mood)}
                title={moodInfo(mood).title}
                aria-label={moodInfo(mood).title}
                aria-selected={mood === shown}
                role="tab"
              >
                {moodInfo(mood).emoji}
              </button>
            ))}
          </div>
        </div>

        <div className="info">
          <span className={`moodchip ${isLive ? "live" : "preview"}`}>
            <span>{m.emoji}</span> {m.title}
            <em>{isLive ? "live" : "preview"}</em>
          </span>
          <p className="speech">
            <span className="q">&ldquo;</span>
            {m.line}
            <span className="q">&rdquo;</span>
          </p>
          {!isLive && (
            <button className="livebtn" onClick={() => setShown(liveMood)}>
              ↺ back to live mood
            </button>
          )}
          <div className="levelrow">
            <div className="levelbadge">
              <span>
                <small>LVL</small>
                <b>{p.level}</b>
              </span>
            </div>
            <div className="levelmeta">
              <div>
                <b>{p.totalXp.toLocaleString()}</b> XP total
              </div>
              <div>
                {p.xpInLevel}/{p.xpForLevel} to level {p.level + 1}
              </div>
            </div>
          </div>
          <div className="bar">
            <span style={{ width: `${progress}%` }} />
          </div>
        </div>
      </div>
    </div>
  );
}
