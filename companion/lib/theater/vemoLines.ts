// Vemo's lines per act — in character: a mischievous analyst who pulls off the
// attack AND teaches why it works. Moods reuse the 10 illustrated expressions
// from gamification.ts (no new art). Each act has up to three beats the director
// picks between: `base` (just entered), `peak` (mid-action high), `done`
// (resolved). Voice clips (Batch 8) are keyed by the same act ids.

import type { Act } from "./types";
import type { Mood } from "../types";

export interface VemoBeat {
  mood: Mood;
  line: string;
}

export interface ActVemo {
  base: VemoBeat;
  peak?: VemoBeat;
  done?: VemoBeat;
}

export const VEMO_LINES: Record<Act, ActVemo> = {
  recon: {
    base: { mood: "curious", line: "Before I touch anything — I already know you. Your phone never stops talking." },
    peak: { mood: "thinking", line: "Every network you’ve ever joined, it’s shouting the names right now." },
  },
  ble: {
    base: { mood: "excited", line: "Watch your hands — I’m about to make every phone here buzz." },
    peak: { mood: "celebrating", line: "There they go! No pairing, no tap — just a faked ‘nearby device’." },
    done: { mood: "focused", line: "That was one spoofed advertisement, repeated. Turn off proximity sharing and it dies." },
  },
  deauth: {
    base: { mood: "focused", line: "You all joined my old router earlier. Watch — check your WiFi again." },
    peak: { mood: "surprised", line: "Frames flying. Those deauth packets are unauthenticated — anyone can forge them." },
    done: { mood: "confused", line: "Dropped. WPA3 with PMF would’ve made those frames worthless." },
  },
  clone: {
    base: { mood: "thinking", line: "Knocked off… so your phones go hunting for the same name. I have that name." },
    peak: { mood: "surprised", line: "And they rejoined — me. Anything typed on that page, I’d see. Count only here, I promise." },
    done: { mood: "focused", line: "Don’t auto-reconnect. Check the lock, check the cert. That’s the whole defence." },
  },
  badusb: {
    base: { mood: "excited", line: "This isn’t a USB stick. It’s a keyboard — and it types faster than you ever could." },
    peak: { mood: "celebrating", line: "Look at it go. No exploit, no malware — just keystrokes you’d have typed yourself." },
    done: { mood: "oops", line: "Owned, in seconds. Lock your screen and lock down USB and I’m stuck." },
  },
  debrief: {
    base: { mood: "happy", line: "Four attacks, under five minutes — and every single one has a defence." },
    done: { mood: "celebrating", line: "You just watched all four. That awareness? That’s the patch. Go use it." },
  },
};
