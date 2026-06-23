// Parse + play a DuckyScript payload as a synced, readable scroll.
//
// The badUSB beat: "it's not a USB stick — it's a keyboard that types faster
// than you can." We scroll the REAL `badusb_payloads/*.txt` line-by-line in
// sync with an estimated execution time (cumulative DELAY + char-count × per-key
// delay), but clamp each line to a readable minimum so the audience can follow.
//
// BULLETPROOF: `loadPayload` fetches the real file but falls back to an embedded
// copy if the fetch fails, so the spine never depends on the network or a path.

export type DuckyKind = "rem" | "delay" | "key" | "string" | "blank";

export interface DuckyLine {
  raw: string;
  kind: DuckyKind;
  /** Estimated ms for this line to "execute" — drives scroll pacing. */
  durMs: number;
}

export interface DuckyScript {
  name: string;
  lines: DuckyLine[];
  totalMs: number;
}

interface PaceOpts {
  /** ms per typed character in a STRING line. */
  perKeyMs?: number;
  /** floor so even instant lines stay readable on the wall. */
  minLineMs?: number;
  /** global speed multiplier (>1 = faster). */
  speed?: number;
}

const DEFAULTS: Required<PaceOpts> = { perKeyMs: 14, minLineMs: 420, speed: 1 };

function classify(line: string): DuckyKind {
  const t = line.trim();
  if (t === "") return "blank";
  const up = t.toUpperCase();
  if (up.startsWith("REM")) return "rem";
  if (up.startsWith("DELAY")) return "delay";
  if (up.startsWith("STRING") || up.startsWith("STRINGLN")) return "string";
  return "key"; // ENTER, GUI r, CTRL, TAB, raw key combos, etc.
}

function lineDuration(line: string, kind: DuckyKind, o: Required<PaceOpts>): number {
  let est: number;
  switch (kind) {
    case "delay": {
      const n = Number(line.trim().split(/\s+/)[1]);
      est = Number.isFinite(n) ? n : o.minLineMs;
      break;
    }
    case "string": {
      const payload = line.trim().replace(/^STRINGLN\s?|^STRING\s?/i, "");
      est = payload.length * o.perKeyMs;
      break;
    }
    case "key":
      est = 90;
      break;
    case "rem":
    case "blank":
    default:
      est = o.minLineMs;
      break;
  }
  return Math.max(o.minLineMs, est) / o.speed;
}

/** Parse raw DuckyScript text into timed lines. */
export function parseDucky(text: string, name: string, opts: PaceOpts = {}): DuckyScript {
  const o = { ...DEFAULTS, ...opts };
  const lines = text.replace(/\r\n/g, "\n").split("\n").map((raw) => {
    const kind = classify(raw);
    return { raw, kind, durMs: lineDuration(raw, kind, o) };
  });
  const totalMs = lines.reduce((s, l) => s + l.durMs, 0);
  return { name, lines, totalMs };
}

/**
 * Play a script: calls `onTick(index)` as each line begins "executing", then
 * `onDone()`. Returns a stop function that cancels any pending timers.
 */
export function playDucky(
  script: DuckyScript,
  cb: { onTick: (index: number) => void; onDone?: () => void },
): () => void {
  const timers: ReturnType<typeof setTimeout>[] = [];
  let acc = 0;
  script.lines.forEach((l, i) => {
    timers.push(setTimeout(() => cb.onTick(i), acc));
    acc += l.durMs;
  });
  timers.push(setTimeout(() => cb.onDone?.(), acc));
  return () => {
    for (const t of timers) clearTimeout(t);
  };
}

// Embedded fallback (mirrors badusb_payloads/demo_exfil_sim.txt) so the badUSB
// spine works with zero network / wrong path on stage.
const FALLBACK_DUCKY = `REM VariOne Demo — simulated credential harvest (awareness demo only)
REM Target: Windows. Opens CMD, dumps hostname + user + IP, pastes to
REM a visible text file on Desktop so the audience can see what leaked.

DELAY 2000
GUI r
DELAY 600
STRING cmd
ENTER
DELAY 800
STRING (echo === VariOne Demo Harvest === && hostname && whoami && ipconfig | findstr IPv4) > %USERPROFILE%\\Desktop\\DEMO_harvest.txt && notepad %USERPROFILE%\\Desktop\\DEMO_harvest.txt
ENTER`;

/**
 * Load a payload by URL, parse + time it. Falls back to the embedded script if
 * the fetch fails for any reason (the show never breaks on a missing file).
 */
export async function loadPayload(url: string, opts: PaceOpts = {}): Promise<DuckyScript> {
  const name = url.split("/").pop() || "payload.txt";
  try {
    const res = await fetch(url);
    if (!res.ok) throw new Error(String(res.status));
    const text = await res.text();
    if (!text.trim()) throw new Error("empty");
    return parseDucky(text, name, opts);
  } catch {
    return parseDucky(FALLBACK_DUCKY, "demo_exfil_sim.txt", opts);
  }
}
