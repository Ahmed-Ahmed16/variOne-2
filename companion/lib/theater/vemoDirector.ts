// Pick Vemo's current beat from the act + live scene state. Pure + deterministic
// so mock and live drive identical narration. Reuses the mood → art/emoji/title
// mapping from gamification.ts (moodInfo) — no new mood data here.

import type { Act, SceneState } from "./types";
import type { MoodInfo } from "../types";
import { moodInfo } from "../gamification";
import { VEMO_LINES, type VemoBeat } from "./vemoLines";

export interface VemoDirection extends VemoBeat {
  /** Full mood info (art path, emoji, title) for rendering. */
  info: MoodInfo;
}

/** Choose base/peak/done for the act based on how far the action has gone. */
export function directVemo(act: Act, s: SceneState): VemoDirection {
  const v = VEMO_LINES[act];
  let beat: VemoBeat = v.base;

  switch (act) {
    case "recon":
      if (s.recon.devicesSeen >= 8) beat = v.peak ?? v.base;
      break;
    case "ble":
      if (!s.ble.running && s.ble.peakBursts > 0) beat = v.done ?? v.base;
      else if (s.ble.bursts >= 60) beat = v.peak ?? v.base;
      break;
    case "deauth":
      if (!s.deauth.running && s.deauth.framesTotal > 0) beat = v.done ?? v.base;
      else if (s.deauth.running && s.deauth.framesTotal >= 250) beat = v.peak ?? v.base;
      break;
    case "clone":
      if (s.clone.credCount >= 2) beat = v.done ?? v.base;
      else if (s.clone.credCount > 0 || s.clone.clients >= 2) beat = v.peak ?? v.base;
      break;
    case "badusb":
      if (s.badusb.lineTotal > 0 && s.badusb.lineIndex >= s.badusb.lineTotal)
        beat = v.done ?? v.base;
      else if (s.badusb.lineIndex > 0) beat = v.peak ?? v.base;
      break;
    case "debrief":
      beat = v.base;
      break;
  }

  return { ...beat, info: moodInfo(beat.mood) };
}
