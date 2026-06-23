/** Compact relative-time label, e.g. "2h", "3d", "now". */
export function timeAgo(ts: number, now: number = Date.now()): string {
  const s = Math.max(0, Math.floor((now - ts) / 1000));
  if (s < 60) return "now";
  const m = Math.floor(s / 60);
  if (m < 60) return `${m}m`;
  const h = Math.floor(m / 60);
  if (h < 24) return `${h}h`;
  const d = Math.floor(h / 24);
  return `${d}d`;
}

export function pct(inLevel: number, forLevel: number): number {
  if (forLevel <= 0) return 0;
  return Math.min(100, Math.round((inLevel / forLevel) * 100));
}
