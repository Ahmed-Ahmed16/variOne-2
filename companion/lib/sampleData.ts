// Sample device export used until the real device link lands.
// SEAM: replace getDeviceStats() with a fetch of the device-written JSON; the
// shape (DeviceStats) is the contract. Timestamps are relative to "now" so the
// demo always looks live (recent activity -> lively Vemo mood + streak).

import type { ActivityEvent, Category, DeviceStats, EventKind } from "./types";

const H = 3600000;
const D = 86400000;
const now = Date.now();

let seq = 0;
function ev(
  kind: EventKind,
  category: Category,
  label: string,
  agoMs: number,
  detail?: string,
): ActivityEvent {
  return { id: `e${seq++}`, kind, category, label, ts: now - agoMs, detail };
}

const events: ActivityEvent[] = [
  // today — busy session (drives a lively mood)
  ev("attack", "wifi", "Deauth flood", 1 * H, "412 frames · 9s"),
  ev("debrief", "awareness", "Deauth debrief", 1 * H - 5 * 60000, "read fully"),
  ev("attack", "wifi", "Evil portal (cloned AP)", 3 * H, "2 clients · 1 cred"),
  ev("debrief", "awareness", "Evil-portal debrief", 3 * H - 4 * 60000),
  ev("capture", "rfid", "MIFARE Classic read", 5 * H, "4-byte UID"),
  ev("capture", "rf", "Sub-GHz capture", 6 * H, "433.92 MHz · OOK"),
  ev("attack", "badusb", "BadUSB script run", 7 * H, "awareness payload · 6s"),
  ev("debrief", "awareness", "BadUSB debrief", 7 * H - 3 * 60000),
  // yesterday
  ev("attack", "wifi", "Beacon spam", 1 * D + 2 * H, "1-11 hopping · 30s"),
  ev("capture", "rfid", "NTAG NDEF read", 1 * D + 4 * H),
  ev("capture", "wifi", "Wi-Fi scan", 1 * D + 5 * H, "23 APs"),
  ev("debrief", "awareness", "Beacon debrief", 1 * D + 2 * H - 60000),
  // earlier this week (keeps the streak alive + builds skill levels)
  ev("capture", "rf", "Keyfob inspect", 2 * D + 3 * H, "fixed-code"),
  ev("attack", "wifi", "Deauth flood", 2 * D + 6 * H, "300 frames · 7s"),
  ev("capture", "rfid", "EMV card read", 3 * D + 2 * H, "PAN masked ****1234"),
  ev("debrief", "awareness", "RFID debrief", 3 * D + 2 * H - 60000),
  ev("attack", "badusb", "BadUSB script run", 4 * D + 4 * H),
  ev("capture", "rf", "Sub-GHz capture", 4 * D + 5 * H, "315 MHz"),
  ev("debrief", "awareness", "Awareness recap", 5 * D + 3 * H),
  ev("capture", "wifi", "Wi-Fi scan", 6 * D + 1 * H, "18 APs"),
];

function recentActiveDays(count: number): string[] {
  const days: string[] = [];
  for (let i = 0; i < count; i++) {
    const d = new Date(now - i * D);
    days.push(d.toISOString().slice(0, 10));
  }
  return days;
}

export const SAMPLE_STATS: DeviceStats = {
  deviceName: "VariOne S3",
  firmware: "v1.0",
  activeDays: recentActiveDays(7), // 7-day streak for the demo
  events,
};

/** Single entry point the UI calls. Swap the body for a real device fetch later. */
export async function getDeviceStats(): Promise<DeviceStats> {
  return SAMPLE_STATS;
}
