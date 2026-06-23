// In-page mock source — the show's spine. Zero hardware, fully deterministic.
//
// Implements `AttackSource`: when the presenter enters an act, it schedules that
// act's `MOCK_SCRIPT` steps on real timers and emits them through the same
// callback a live serial source would use. The UI cannot tell the difference —
// that is the whole point (build + rehearse on mock Day 1, swap to live Day 2).
//
// Re-entering an act cancels any in-flight schedule and replays from the top, so
// a presenter can rehearse a beat repeatedly or recover mid-show.

import type { Act, AttackEvent, AttackSource } from "./types";
import { MOCK_SCRIPT } from "./mockScript";

export function createMockSource(): AttackSource {
  let onEvent: ((e: AttackEvent) => void) | null = null;
  let timers: ReturnType<typeof setTimeout>[] = [];

  const clearTimers = () => {
    for (const t of timers) clearTimeout(t);
    timers = [];
  };

  return {
    kind: "demo",

    start(cb) {
      onEvent = cb;
    },

    enterAct(act: Act) {
      clearTimers(); // cancel any prior act still playing out
      const steps = MOCK_SCRIPT[act] ?? [];
      let acc = 0;
      for (const step of steps) {
        acc += step.delayMs;
        timers.push(
          setTimeout(() => {
            onEvent?.(step.event);
          }, acc),
        );
      }
    },

    stop() {
      clearTimers();
      onEvent = null;
    },
  };
}
