// Live Attack Control Room — normalized event + scene model.
//
// Every source (Web Serial, in-page mock, replay bridge) produces the SAME
// `AttackEvent` so the UI is byte-identical regardless of where the data came
// from (PRD-adjacent: see ~/.claude/plans wow-effect Theater plan, "Architecture").
//
// NULL SEMANTICS: a parsed line only carries the fields the firmware actually
// printed. Fields the line did not carry are `null` ("unchanged — keep prior
// value"), NOT 0 — so a `spam stopped` line flips `running` without resetting
// the burst total the debrief needs. `aggregate.ts` honours this.
//
// PRIVACY: only counts + events are ever modelled here. Never raw credential
// values, never a PAN (mirrors the firmware CLAUDE.md rule). `credCount` is a
// count only; the captured strings never leave the device.

/** The acts of the show, in escalation order. */
export type Act = "recon" | "ble" | "deauth" | "clone" | "badusb" | "debrief";

/**
 * One normalized event emitted by any source. Numeric/string fields are
 * `null` when the originating line did not carry them (see NULL SEMANTICS).
 */
export type AttackEvent =
  | { act: "recon"; devicesSeen: number; ssidsLeaked: number }
  | { act: "ble"; bursts: number | null; running: boolean }
  | {
      act: "deauth";
      targetMac: string | null;
      channel: number | null;
      fps: number;
      framesTotal: number | null;
      running: boolean;
    }
  // clone: the optional "+1" firmware line carries `clients`; the VARIPORTAL
  // creds line carries `credCount`. Never both, never raw values.
  | { act: "clone"; clients: number | null; credCount: number | null }
  // badusb is client-side: duckyScroll emits progress, no serial line exists
  // (the USB port is busy as an HID keyboard while typing).
  | { act: "badusb"; scriptName: string; lineIndex: number; lineTotal: number }
  // a presenter-paced marker that the show reached the debrief act.
  | { act: "debrief" };

/** Accumulated, render-ready snapshot. Produced once in `aggregate.ts`. */
export interface SceneState {
  recon: { devicesSeen: number; ssidsLeaked: number };
  ble: { bursts: number; peakBursts: number; running: boolean };
  deauth: {
    targetMac: string;
    channel: number;
    fps: number;
    peakFps: number;
    framesTotal: number;
    running: boolean;
  };
  clone: { clients: number; peakClients: number; credCount: number };
  badusb: { scriptName: string; lineIndex: number; lineTotal: number };
  /** Which act most recently produced an event (drives Vemo + active scene). */
  lastAct: Act | null;
  /** Total events folded in — a cheap "is anything happening yet" signal. */
  eventCount: number;
}

/**
 * A pluggable event source. The hook is source-agnostic: mock, Web Serial and
 * replay all implement this, so going live is a swapped source on a proven path.
 *
 * `enterAct` lets a presenter-paced source play a specific act on demand (the
 * mock schedules that act's scripted events; a live serial source ignores it —
 * the device drives timing when the operator arms the act on the hardware).
 */
export interface AttackSource {
  /** Begin producing events to `onEvent`. Idempotent. */
  start(onEvent: (e: AttackEvent) => void): void;
  /** Presenter advanced to `act` (mock plays it; live sources may no-op). */
  enterAct(act: Act): void;
  /** Stop and release everything (timers, serial reader, etc.). */
  stop(): void;
  /** Label for the LIVE/DEMO/REPLAY source badge. */
  readonly kind: "demo" | "serial" | "replay";
  /**
   * Optional: open a real device link. Web Serial needs a user gesture to pick
   * a port, so the page calls this from a button (mock/replay omit it).
   */
  connect?(): Promise<void>;
  /** Optional live connection state for the badge ("idle" until connected). */
  status?: "idle" | "connecting" | "connected" | "error";
}
