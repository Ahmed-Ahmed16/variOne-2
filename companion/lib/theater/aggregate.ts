// Fold normalized `AttackEvent`s into a render-ready `SceneState`.
//
// ONE place owns the counter math, used by every source (mock + live + replay),
// so the wall shows identical numbers no matter where the events came from. The
// reducer is PURE and IMMUTABLE — it returns a fresh state object so React can
// detect the change by reference. `null` fields mean "keep prior" (see
// types.ts NULL SEMANTICS): a stop event flips `running` without wiping totals.

import type { Act, AttackEvent, SceneState } from "./types";

export function initialSceneState(): SceneState {
  return {
    recon: { devicesSeen: 0, ssidsLeaked: 0 },
    ble: { bursts: 0, peakBursts: 0, running: false },
    deauth: {
      targetMac: "",
      channel: 0,
      fps: 0,
      peakFps: 0,
      framesTotal: 0,
      running: false,
    },
    clone: { clients: 0, peakClients: 0, credCount: 0 },
    badusb: { scriptName: "", lineIndex: 0, lineTotal: 0 },
    lastAct: null,
    eventCount: 0,
  };
}

/** Fold one event into the scene, returning a new state. */
export function reduceScene(prev: SceneState, e: AttackEvent): SceneState {
  const s: SceneState = {
    ...prev,
    recon: { ...prev.recon },
    ble: { ...prev.ble },
    deauth: { ...prev.deauth },
    clone: { ...prev.clone },
    badusb: { ...prev.badusb },
    eventCount: prev.eventCount + 1,
  };

  switch (e.act) {
    case "recon":
      s.recon.devicesSeen = e.devicesSeen;
      s.recon.ssidsLeaked = e.ssidsLeaked;
      s.lastAct = "recon";
      break;

    case "ble":
      if (e.bursts != null) {
        s.ble.bursts = e.bursts;
        s.ble.peakBursts = Math.max(s.ble.peakBursts, e.bursts);
      }
      s.ble.running = e.running;
      s.lastAct = "ble";
      break;

    case "deauth":
      if (e.targetMac != null) s.deauth.targetMac = e.targetMac;
      if (e.channel != null) s.deauth.channel = e.channel;
      s.deauth.fps = e.fps;
      s.deauth.peakFps = Math.max(s.deauth.peakFps, e.fps);
      if (e.framesTotal != null) s.deauth.framesTotal = e.framesTotal;
      s.deauth.running = e.running;
      s.lastAct = "deauth";
      break;

    case "clone":
      if (e.clients != null) {
        s.clone.clients = e.clients;
        s.clone.peakClients = Math.max(s.clone.peakClients, e.clients);
      }
      if (e.credCount != null) s.clone.credCount = e.credCount;
      s.lastAct = "clone";
      break;

    case "badusb":
      s.badusb.scriptName = e.scriptName;
      s.badusb.lineIndex = e.lineIndex;
      s.badusb.lineTotal = e.lineTotal;
      s.lastAct = "badusb";
      break;

    case "debrief":
      s.lastAct = "debrief";
      break;
  }

  return s;
}

/** Aggregate-only recap for the debrief — counts, never values. */
export interface DebriefSummary {
  bleBursts: number;
  deauthFrames: number;
  deauthPeakFps: number;
  cloneClients: number;
  credCount: number;
  badusbLines: number;
  /** How many distinct attack acts actually fired (for the "N attacks" line). */
  actsFired: number;
}

export function debriefSummary(s: SceneState): DebriefSummary {
  const acts: Act[] = ["recon", "ble", "deauth", "clone", "badusb"];
  const fired = acts.filter((a) => {
    switch (a) {
      case "recon":
        return s.recon.devicesSeen > 0;
      case "ble":
        return s.ble.peakBursts > 0;
      case "deauth":
        return s.deauth.framesTotal > 0;
      case "clone":
        return s.clone.peakClients > 0 || s.clone.credCount > 0;
      case "badusb":
        return s.badusb.lineTotal > 0;
      default:
        return false;
    }
  }).length;

  return {
    bleBursts: s.ble.peakBursts,
    deauthFrames: s.deauth.framesTotal,
    deauthPeakFps: s.deauth.peakFps,
    cloneClients: s.clone.peakClients,
    credCount: s.clone.credCount,
    badusbLines: s.badusb.lineTotal,
    actsFired: fired,
  };
}
