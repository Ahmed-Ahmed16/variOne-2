"use client";

// Live Attack Control Room — orchestrator. Owns the act state machine + keyboard
// pacing; the data comes from useAttackStream (mock by default, live serial on
// ?mode=serial). Presenter advances acts; entering an act arms its source.
//
//   → / Space / Enter  next act      ← back      0 reset
//   R redact toggle     V mute toggle
//
// Scenes start as inline counter frames here and are swapped for dedicated,
// richer Scene components batch by batch — the show is walkable end-to-end on
// mock at every step.

import { useCallback, useEffect, useState } from "react";
import "./theater.css";
import { useAttackStream } from "../../components/theater/useAttackStream";
import ControlRoomStage from "../../components/theater/ControlRoomStage";
import SceneBLE from "../../components/theater/SceneBLE";
import SceneBadUSB from "../../components/theater/SceneBadUSB";
import SceneDeauth from "../../components/theater/SceneDeauth";
import SceneClone from "../../components/theater/SceneClone";
import SceneRecon from "../../components/theater/SceneRecon";
import DebriefPanel from "../../components/theater/DebriefPanel";
import AnalystVemo from "../../components/theater/AnalystVemo";
import { playClip, stopClip } from "../../lib/theater/voice";
import { ACTS, ACT_ORDER } from "../../lib/theater/show";
import type { Act, AttackEvent, SceneState } from "../../lib/theater/types";

export default function TheaterPage() {
  const stream = useAttackStream();
  const [idx, setIdx] = useState(0);
  const [muted, setMuted] = useState(false);
  const [redacted, setRedacted] = useState(true); // privacy default: ON
  const currentAct: Act = ACT_ORDER[idx];

  // Arm the current act's source whenever the presenter advances.
  useEffect(() => {
    stream.enterAct(currentAct);
  }, [idx, stream]);

  // Fire the act's Vemo voice-over (opt-in by file existence; muted = silence).
  useEffect(() => {
    if (muted) {
      stopClip();
      return;
    }
    playClip(currentAct);
    return () => stopClip();
  }, [idx, muted]); // eslint-disable-line react-hooks/exhaustive-deps

  // Global keyboard pacing.
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      switch (e.key) {
        case "ArrowRight":
        case " ":
        case "Enter":
          e.preventDefault();
          setIdx((i) => Math.min(ACT_ORDER.length - 1, i + 1));
          break;
        case "ArrowLeft":
          e.preventDefault();
          setIdx((i) => Math.max(0, i - 1));
          break;
        case "r":
        case "R":
          setRedacted((v) => !v);
          break;
        case "v":
        case "V":
          setMuted((v) => !v);
          break;
        case "0":
          stream.reset();
          setIdx(0);
          break;
      }
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [stream]);

  return (
    <ControlRoomStage
      acts={ACTS}
      currentIdx={idx}
      onSelect={setIdx}
      sourceKind={stream.sourceKind}
      status={stream.status}
      onConnect={stream.connect}
      muted={muted}
      redacted={redacted}
    >
      {renderScene(currentAct, stream.scene, redacted, stream.injectEvent)}
      <AnalystVemo act={currentAct} scene={stream.scene} />
    </ControlRoomStage>
  );
}

// Inline scene renderer — dedicated Scene* components replace these cases in
// later batches. Kept generic so every act is already walkable on mock.
function renderScene(
  act: Act,
  s: SceneState,
  redacted: boolean,
  inject: (e: AttackEvent) => void,
) {
  switch (act) {
    case "recon":
      return <SceneRecon recon={s.recon} redacted={redacted} />;

    case "ble":
      return <SceneBLE ble={s.ble} />;

    case "deauth":
      return <SceneDeauth deauth={s.deauth} redacted={redacted} />;

    case "clone":
      return <SceneClone clone={s.clone} />;

    case "badusb":
      return <SceneBadUSB inject={inject} />;

    case "debrief":
      return <DebriefPanel scene={s} />;
  }
}
