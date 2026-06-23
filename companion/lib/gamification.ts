// Pure functions that turn raw DeviceStats into the Profile view model.
// Deterministic + side-effect free so the same device export always renders the
// same dashboard (and so a future on-device version could mirror the math).

import type {
  ActivityEvent,
  Badge,
  Category,
  DeviceStats,
  EventKind,
  Mood,
  MoodInfo,
  Profile,
  SkillInfo,
} from "./types";

// XP awarded per event kind. Tuned so attacks feel rewarding but the awareness
// loop (viewing debriefs) is also clearly worth doing.
export const XP: Record<EventKind, number> = {
  attack: 60,
  capture: 35,
  debrief: 45,
  session: 15,
};

// Level curve: cumulative XP needed to *complete* level n grows smoothly.
// xpForLevel(n) = 120 * n^1.35 — gentle early, steeper later.
export function xpForLevel(level: number): number {
  return Math.round(120 * Math.pow(level, 1.35));
}

/** Resolve total XP into {level, xpInLevel, xpForLevel}. */
export function resolveLevel(totalXp: number) {
  let level = 1;
  let remaining = totalXp;
  while (remaining >= xpForLevel(level)) {
    remaining -= xpForLevel(level);
    level += 1;
  }
  return { level, xpInLevel: remaining, xpForLevel: xpForLevel(level) };
}

const CATEGORY_META: Record<Category, { name: string; icon: string }> = {
  wifi: { name: "Wi-Fi Ops", icon: "📡" },
  rfid: { name: "RFID / NFC", icon: "💳" },
  rf: { name: "Sub-GHz RF", icon: "📻" },
  badusb: { name: "BadUSB / HID", icon: "⌨️" },
  awareness: { name: "Awareness", icon: "🎓" },
};

function skillFor(category: Category, events: ActivityEvent[]): SkillInfo {
  const mine = events.filter((e) => e.category === category);
  const xp = mine.reduce((sum, e) => sum + XP[e.kind], 0);
  const { level, xpInLevel, xpForLevel: need } = resolveLevel(xp);
  return {
    category,
    name: CATEGORY_META[category].name,
    icon: CATEGORY_META[category].icon,
    level,
    xp,
    xpInLevel,
    xpForLevel: need,
    count: mine.length,
  };
}

// ---- streaks ---------------------------------------------------------------

function dayKey(d: Date): string {
  return d.toISOString().slice(0, 10);
}

/** Current streak ending today/yesterday, and the best run, from active days. */
function computeStreaks(activeDays: string[]): { current: number; best: number } {
  const set = new Set(activeDays);
  const sorted = [...activeDays].sort();
  // best run
  let best = 0;
  let run = 0;
  let prev: number | null = null;
  for (const day of sorted) {
    const t = new Date(day + "T00:00:00Z").getTime();
    if (prev !== null && t - prev === 86400000) run += 1;
    else run = 1;
    best = Math.max(best, run);
    prev = t;
  }
  // current run ending today or yesterday
  let current = 0;
  const cursor = new Date();
  if (!set.has(dayKey(cursor))) cursor.setUTCDate(cursor.getUTCDate() - 1);
  while (set.has(dayKey(cursor))) {
    current += 1;
    cursor.setUTCDate(cursor.getUTCDate() - 1);
  }
  return { current, best };
}

// ---- mood ------------------------------------------------------------------

const MOODS: Record<Mood, Omit<MoodInfo, "mood" | "art">> = {
  happy: {
    emoji: "😄",
    title: "Happy",
    line: "Good to see you! Ready when you are.",
  },
  excited: {
    emoji: "⚡",
    title: "Excited",
    line: "We are ON FIRE today — keep them coming!",
  },
  curious: {
    emoji: "🧐",
    title: "Curious",
    line: "Ooh, what signal is that? Let's go poke at it.",
  },
  thinking: {
    emoji: "💭",
    title: "Thinking",
    line: "It's quiet... pick a target and let's learn something.",
  },
  surprised: {
    emoji: "😲",
    title: "Surprised",
    line: "Whoa — we actually caught one. That's a real account to rotate.",
  },
  focused: {
    emoji: "🎯",
    title: "Focused",
    line: "Locked in. Every debrief makes us sharper.",
  },
  confused: {
    emoji: "❓",
    title: "Confused",
    line: "Hmm, that didn't land. Want to try a different angle?",
  },
  celebrating: {
    emoji: "🎉",
    title: "Celebrating",
    line: "What a streak! We are absolutely cooking.",
  },
  sad: {
    emoji: "😢",
    title: "Sad",
    line: "Aw... that one stung. We'll get the next one.",
  },
  oops: {
    emoji: "😅",
    title: "Oops",
    line: "Heh — my bad. Let's pretend that didn't happen.",
  },
};

/** Stable display order for the mood gallery / cycling. */
export const MOOD_ORDER: Mood[] = [
  "happy",
  "excited",
  "curious",
  "thinking",
  "surprised",
  "focused",
  "confused",
  "celebrating",
  "sad",
  "oops",
];

/** Full info for a single mood (used by the interactive hero to cycle moods). */
export function moodInfo(mood: Mood): MoodInfo {
  return { mood, art: `/vemo/moods/${mood}.png`, ...MOODS[mood] };
}

/**
 * Pick a mood from recency + the mix of recent activity.
 * Note: "sad" and "oops" are reaction states reserved for failed/empty/error
 * ops once the device link lands (e.g. a portal that caught nothing, a misfire);
 * they're always available in the mood gallery. The live heuristic below selects
 * from the activity-driven set.
 */
function computeMood(events: ActivityEvent[]): MoodInfo {
  const now = Date.now();
  const dayMs = 86400000;
  const recent = events.filter((e) => now - e.ts < 2 * dayMs);
  const attacks = recent.filter((e) => e.kind === "attack").length;
  const debriefs = recent.filter((e) => e.kind === "debrief").length;
  const portalWin = recent.some(
    (e) => e.kind === "attack" && /portal/i.test(e.label),
  );

  let mood: Mood;
  if (recent.length === 0) mood = "thinking";
  else if (recent.length >= 8) mood = "excited";
  else if (portalWin) mood = "surprised";
  else if (debriefs >= attacks && debriefs > 0) mood = "focused";
  else if (attacks >= 3) mood = "celebrating";
  else if (attacks > 0) mood = "curious";
  else mood = "happy";
  return moodInfo(mood);
}

// ---- badges ----------------------------------------------------------------

function computeBadges(
  events: ActivityEvent[],
  skills: SkillInfo[],
  bestStreak: number,
): Badge[] {
  const count = (k: EventKind) => events.filter((e) => e.kind === k).length;
  const usedCats = new Set(events.map((e) => e.category));
  const hasPortalCreds = events.some(
    (e) => e.kind === "attack" && /portal/i.test(e.label),
  );
  const def: Array<Omit<Badge, "earned"> & { earned: boolean }> = [
    {
      id: "first-blood",
      name: "First Contact",
      desc: "Run your first operation",
      icon: "🩸",
      earned: events.length > 0,
    },
    {
      id: "scholar",
      name: "Scholar",
      desc: "View 5 awareness debriefs",
      icon: "🎓",
      earned: count("debrief") >= 5,
    },
    {
      id: "portal-master",
      name: "Portal Master",
      desc: "Land an evil-portal session",
      icon: "🎣",
      earned: hasPortalCreds,
    },
    {
      id: "polyglot",
      name: "Polyglot",
      desc: "Use all 4 attack/capture domains",
      icon: "🧩",
      earned: ["wifi", "rfid", "rf", "badusb"].every((c) => usedCats.has(c as Category)),
    },
    {
      id: "streak-7",
      name: "On a Roll",
      desc: "7-day usage streak",
      icon: "🔥",
      earned: bestStreak >= 7,
    },
    {
      id: "veteran",
      name: "Veteran",
      desc: "Reach Wi-Fi skill level 5",
      icon: "🛰️",
      earned: skills.some((s) => s.category === "wifi" && s.level >= 5),
    },
  ];
  return def;
}

// ---- main ------------------------------------------------------------------

export function buildProfile(stats: DeviceStats): Profile {
  const events = [...stats.events].sort((a, b) => b.ts - a.ts);
  const totalXp = events.reduce((sum, e) => sum + XP[e.kind], 0);
  const { level, xpInLevel, xpForLevel: need } = resolveLevel(totalXp);

  const categories: Category[] = ["wifi", "rfid", "rf", "badusb", "awareness"];
  const skills = categories.map((c) => skillFor(c, events));

  const { current, best } = computeStreaks(stats.activeDays);
  const mood = computeMood(events);
  const badges = computeBadges(events, skills, best);

  const counts: Record<EventKind, number> = {
    attack: 0,
    capture: 0,
    debrief: 0,
    session: 0,
  };
  for (const e of events) counts[e.kind] += 1;

  return {
    deviceName: stats.deviceName,
    firmware: stats.firmware,
    totalXp,
    level,
    xpInLevel,
    xpForLevel: need,
    mood,
    skills,
    badges,
    streakDays: current,
    bestStreak: best,
    counts,
    recent: events.slice(0, 12),
  };
}
