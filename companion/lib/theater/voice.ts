// Pre-rendered Vemo voice-over per act. OPT-IN by existence: a clip plays only
// if the file is present; a missing clip silently no-ops, so the show never
// depends on audio. One global player → advancing acts cuts the prior clip.
// Mute is one key (V) in the page. Autoplay policies are satisfied because the
// first clip fires off the presenter's keypress (a user gesture).
//
// Record clips into companion/public/vemo/vo/<act>.mp3 (see that dir's README).

import type { Act } from "./types";
import { asset } from "../asset";

let current: HTMLAudioElement | null = null;

export function stopClip(): void {
  if (current) {
    try {
      current.pause();
    } catch {
      /* ignore */
    }
    current = null;
  }
}

/** Play the clip for `act` if one exists; cut any clip already playing. */
export function playClip(act: Act): void {
  stopClip();
  if (typeof Audio === "undefined") return; // SSR / no DOM
  try {
    const a = new Audio(asset(`/vemo/vo/${act}.mp3`));
    a.volume = 0.9;
    // Missing file, decode error, or autoplay block → resolve to silence.
    a.play().catch(() => {});
    current = a;
  } catch {
    /* no audio support — silent */
  }
}
