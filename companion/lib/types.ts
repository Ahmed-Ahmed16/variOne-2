// Domain model for the VariOne Companion gamification layer.
//
// SEAM: everything here is fed today by lib/sampleData.ts (mock). When the device
// link lands, a real on-device exporter writes a `DeviceStats` JSON (over the SD
// card or a local pull) and we swap the sample loader for a fetch — nothing else
// in the UI changes. Only AGGREGATE activity is ever modelled here: counts, types,
// timestamps. Never credentials, captures or PANs (mirrors firmware CLAUDE.md).

/** Category a piece of activity belongs to — also the skill-tree axes. */
export type Category = "wifi" | "rfid" | "rf" | "badusb" | "awareness";

/** What kind of thing the user did on the device. */
export type EventKind =
  | "attack" // deauth / beacon / evil portal / badusb run
  | "capture" // rfid/nfc read, rf capture, keyfob inspect, wifi scan
  | "debrief" // viewed/completed an awareness debrief
  | "session"; // device session / streak tick

/** One aggregate activity record exported by the device. */
export interface ActivityEvent {
  id: string;
  kind: EventKind;
  category: Category;
  /** Short human label, e.g. "Deauth flood", "MIFARE read". */
  label: string;
  /** Unix ms. */
  ts: number;
  /** Optional aggregate detail, e.g. "412 frames · 8s". Never sensitive. */
  detail?: string;
}

/** The raw aggregate stats blob the device exports (the future JSON contract). */
export interface DeviceStats {
  deviceName: string;
  firmware: string;
  /** ISO date strings (YYYY-MM-DD) the device was actively used. Drives streaks. */
  activeDays: string[];
  events: ActivityEvent[];
}

// ---- derived, computed view models ----------------------------------------

// One per illustrated Vemo expression (art/vemo masters sheet).
export type Mood =
  | "happy"
  | "excited"
  | "curious"
  | "thinking"
  | "surprised"
  | "focused"
  | "confused"
  | "celebrating"
  | "sad"
  | "oops";

export interface MoodInfo {
  mood: Mood;
  emoji: string;
  title: string;
  line: string; // Vemo speaking, first person
  art: string; // /vemo/* asset
}

export interface SkillInfo {
  category: Category;
  name: string;
  icon: string;
  level: number;
  xp: number;
  xpInLevel: number;
  xpForLevel: number;
  count: number;
}

export interface Badge {
  id: string;
  name: string;
  desc: string;
  icon: string;
  earned: boolean;
}

export interface Profile {
  deviceName: string;
  firmware: string;
  totalXp: number;
  level: number;
  xpInLevel: number;
  xpForLevel: number;
  mood: MoodInfo;
  skills: SkillInfo[];
  badges: Badge[];
  streakDays: number;
  bestStreak: number;
  counts: Record<EventKind, number>;
  recent: ActivityEvent[];
}
