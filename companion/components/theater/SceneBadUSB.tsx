"use client";

// badUSB act: scroll the REAL DuckyScript line-by-line in sync with its
// estimated execution time, so the audience watches "a keyboard that types
// faster than you can" actually type. Each tick pushes a badusb event into the
// shared scene state (so the debrief total is right), and the current line
// auto-scrolls into view. Bulletproof: loadPayload falls back to an embedded
// copy if the file fetch fails.

import { useEffect, useRef, useState } from "react";
import SceneFrame from "./SceneFrame";
import CounterTicker from "./CounterTicker";
import MechanismDiagram from "./MechanismDiagram";
import { asset } from "../../lib/asset";
import { loadPayload, playDucky, type DuckyScript } from "../../lib/theater/duckyScroll";
import type { AttackEvent } from "../../lib/theater/types";

const PAYLOAD_URL = "/payloads/demo_exfil_sim.txt";

export default function SceneBadUSB({ inject }: { inject: (e: AttackEvent) => void }) {
  const [script, setScript] = useState<DuckyScript | null>(null);
  const [cur, setCur] = useState(-1);
  const [done, setDone] = useState(false);
  const termRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    let stop = () => {};
    let cancelled = false;
    loadPayload(asset(PAYLOAD_URL)).then((s) => {
      if (cancelled) return;
      setScript(s);
      inject({ act: "badusb", scriptName: s.name, lineIndex: 0, lineTotal: s.lines.length });
      stop = playDucky(s, {
        onTick: (i) => {
          setCur(i);
          inject({ act: "badusb", scriptName: s.name, lineIndex: i + 1, lineTotal: s.lines.length });
        },
        onDone: () => setDone(true),
      });
    });
    return () => {
      cancelled = true;
      stop();
    };
  }, [inject]);

  // keep the line being "typed" centered in the terminal
  useEffect(() => {
    const el = termRef.current?.querySelector<HTMLElement>(".dl.on");
    el?.scrollIntoView({ block: "center", behavior: "smooth" });
  }, [cur]);

  const total = script?.lines.length ?? 0;

  return (
    <SceneFrame act="badusb" extra={<MechanismDiagram act="badusb" />}>
      <div className="ducky-wrap">
        <div className="ducky-head">
          <span className="fname">⌨ {script?.name ?? "loading payload…"}</span>
          <span className="ducky-prog">
            {Math.max(0, cur + 1)}/{total} lines
          </span>
        </div>
        <div className="ducky" ref={termRef}>
          {script?.lines.map((l, i) => (
            <div
              key={i}
              className={`dl k-${l.kind} ${i === cur ? "on" : ""} ${i < cur ? "past" : ""}`}
            >
              <span className="dl-i">{String(i + 1).padStart(2, "0")}</span>
              <code>{l.raw || " "}</code>
            </div>
          ))}
        </div>
        {done ? (
          <div className="ducky-done">
            ✓ payload delivered — {total} lines typed in seconds, no clicks
          </div>
        ) : null}
      </div>
      <CounterTicker value={Math.max(0, cur + 1)} label="keystrokes scripted" accent="gold" />
    </SceneFrame>
  );
}
