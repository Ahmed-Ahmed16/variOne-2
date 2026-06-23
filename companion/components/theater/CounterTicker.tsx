"use client";

// A big glowing counter that rolls smoothly to its target. The live "number go
// up" beat of every act — bursts, fps, frames, clients. Pure presentation; the
// value comes straight from `SceneState` so mock and live tick identically.

import { useEffect, useRef, useState } from "react";

type Accent = "cy" | "good" | "warn" | "crit" | "gold";

/** Tween an integer toward `target` with an ease-out cubic over `ms`. */
function useTween(target: number, ms = 450): number {
  const [v, setV] = useState(target);
  const vRef = useRef(target);
  useEffect(() => {
    vRef.current = v;
  }, [v]);
  useEffect(() => {
    const from = vRef.current;
    if (from === target) return;
    let raf = 0;
    let cancelled = false;
    const start = performance.now();
    const step = (now: number) => {
      const t = Math.min(1, (now - start) / ms);
      const eased = 1 - Math.pow(1 - t, 3);
      setV(Math.round(from + (target - from) * eased));
      if (t < 1 && !cancelled) raf = requestAnimationFrame(step);
    };
    raf = requestAnimationFrame(step);
    return () => {
      cancelled = true;
      cancelAnimationFrame(raf);
    };
  }, [target, ms]);
  return v;
}

export default function CounterTicker({
  value,
  label,
  sub,
  accent = "cy",
  big,
}: {
  value: number;
  label: string;
  sub?: string;
  accent?: Accent;
  /** Hero counter (oversized) vs. a secondary metric. */
  big?: boolean;
}) {
  const shown = useTween(value);
  return (
    <div className={`tk ${accent} ${big ? "big" : ""}`}>
      <div className="tk-v">{shown.toLocaleString()}</div>
      <div className="tk-l">{label}</div>
      {sub ? <div className="tk-sub">{sub}</div> : null}
    </div>
  );
}
