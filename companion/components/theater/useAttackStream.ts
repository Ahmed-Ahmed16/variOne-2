"use client";

// THE hook. Picks the event source by `?mode`, accumulates `SceneState`, and
// exposes presenter controls. The component tree never knows whether it is on
// live serial, the mock, or a replay — it just renders `scene`.
//
//   ?mode=demo    (default) in-page mock — the bulletproof spine
//   ?mode=serial  Web Serial against the plugged-in device (Day 2, gated)
//   ?mode=replay  recorded session played back (optional)
//
// `enterAct` drives the source when the presenter advances. `injectEvent` lets
// client-side acts (badUSB, via duckyScroll) push progress into the same single
// source of truth so the debrief totals stay correct.

import { useCallback, useEffect, useRef, useState } from "react";
import type {
  Act,
  AttackEvent,
  AttackSource,
  SceneState,
} from "../../lib/theater/types";
import { initialSceneState, reduceScene } from "../../lib/theater/aggregate";
import { createMockSource } from "../../lib/theater/mockSource";

export type Mode = "demo" | "serial" | "replay";

function readMode(): Mode {
  if (typeof window === "undefined") return "demo";
  const m = new URLSearchParams(window.location.search).get("mode");
  if (m === "serial" || m === "replay") return m;
  return "demo";
}

export interface AttackStream {
  scene: SceneState;
  /** Honest label of the source actually wired up (drives the LIVE/DEMO pill). */
  sourceKind: AttackSource["kind"];
  /** Presenter advanced to `act` — tells the source to play/arm it. */
  enterAct: (act: Act) => void;
  /** Push a client-side event (badUSB progress) into the shared scene state. */
  injectEvent: (e: AttackEvent) => void;
  /** Wipe counters back to zero (between rehearsals). */
  reset: () => void;
  /** Open the live device link (serial only; no-op for mock/replay). */
  connect: () => Promise<void>;
  /** Live link state for the badge (undefined when the source has none). */
  status: AttackSource["status"];
}

export function useAttackStream(): AttackStream {
  const [scene, setScene] = useState<SceneState>(initialSceneState);
  const [sourceKind, setSourceKind] = useState<AttackSource["kind"]>("demo");
  const [status, setStatus] = useState<AttackSource["status"]>(undefined);
  const sourceRef = useRef<AttackSource | null>(null);

  useEffect(() => {
    const mode = readMode();
    let cancelled = false;

    // Web Serial is lazy-loaded so the mock path never bundles it. Until
    // serialSource lands (Batch 9, gated on greenlight) ?mode=serial uses the
    // mock so the badge stays HONEST (reports "demo", never a fake "live").
    const wire = (source: AttackSource) => {
      if (cancelled) {
        source.stop();
        return;
      }
      sourceRef.current = source;
      setSourceKind(source.kind);
      source.start((e) => setScene((prev) => reduceScene(prev, e)));
    };

    if (mode === "serial") {
      import("../../lib/theater/serialSource")
        .then((mod) => wire(mod.createSerialSource()))
        .catch(() => wire(createMockSource())); // not built / unsupported → mock
    } else {
      // replay shares the mock spine until a recorder exists (optional).
      wire(createMockSource());
    }

    return () => {
      cancelled = true;
      sourceRef.current?.stop();
      sourceRef.current = null;
    };
  }, []);

  const enterAct = useCallback((act: Act) => {
    sourceRef.current?.enterAct(act);
  }, []);

  const injectEvent = useCallback((e: AttackEvent) => {
    setScene((prev) => reduceScene(prev, e));
  }, []);

  const reset = useCallback(() => {
    setScene(initialSceneState());
  }, []);

  const connect = useCallback(async () => {
    const s = sourceRef.current;
    if (!s?.connect) return;
    setStatus("connecting");
    try {
      await s.connect();
      setStatus("connected");
    } catch {
      setStatus("error");
    }
  }, []);

  return { scene, sourceKind, enterAct, injectEvent, reset, connect, status };
}
