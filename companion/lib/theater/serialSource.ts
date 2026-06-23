// Web Serial source — the live path. READ-ONLY: it opens the device's USB-CDC
// port, streams text, and demuxes `>>` lines through the SAME `parseLine` the
// mock uses. It never writes to the device and never transmits anything off the
// laptop (mirrors the firmware "no off-board transmit" rule).
//
// Modelled on tools/usb-serial-mirror/varione-mirror.html (the Web Serial
// precedent already in the repo). Chromium-only; needs the device plugged into
// the presenting laptop. A user gesture is required to pick the port, so the
// page calls `connect()` from a button — `start()` only stores the callback and
// silently re-attaches to an already-granted port if one exists.
//
// GATED: only engaged by `?mode=serial`. The demo (`?mode=demo`) never touches
// this file. Live behaviour is verified against hardware in the Day-2 dry run.

import type { Act, AttackEvent, AttackSource } from "./types";
import { parseLine } from "./parseLine";

const BAUD = 115200;

export function createSerialSource(): AttackSource {
  let onEvent: ((e: AttackEvent) => void) | null = null;
  let port: any = null;
  let reader: ReadableStreamDefaultReader<string> | null = null;
  let keepReading = false;
  let buffer = "";

  const source: AttackSource = {
    kind: "serial",
    status: "idle",

    start(cb) {
      onEvent = cb;
      // If the user already granted this port in a prior session, reattach with
      // no gesture so a reload during the show recovers the link automatically.
      const serial = (globalThis.navigator as any)?.serial;
      if (!serial) return;
      serial
        .getPorts()
        .then((ports: any[]) => {
          if (ports.length && source.status === "idle") openPort(ports[0]);
        })
        .catch(() => {
          /* ignore — user can connect manually */
        });
    },

    async connect() {
      const serial = (globalThis.navigator as any)?.serial;
      if (!serial) {
        source.status = "error";
        throw new Error("Web Serial unavailable — use Chromium.");
      }
      source.status = "connecting";
      const chosen = await serial.requestPort();
      await openPort(chosen);
    },

    // The device drives its own timing once the operator arms an act on the
    // hardware; there is nothing to schedule here.
    enterAct(_act: Act) {
      /* no-op for live serial */
    },

    stop() {
      keepReading = false;
      try {
        reader?.cancel();
      } catch {
        /* already closed */
      }
      reader = null;
      try {
        port?.close?.();
      } catch {
        /* already closed */
      }
      port = null;
      onEvent = null;
      source.status = "idle";
    },
  };

  async function openPort(chosen: any) {
    try {
      port = chosen;
      await port.open({ baudRate: BAUD });
      source.status = "connected";
      keepReading = true;
      pump();
    } catch {
      source.status = "error";
    }
  }

  async function pump() {
    const textStream = port.readable.pipeThrough(new (window as any).TextDecoderStream());
    reader = textStream.getReader();
    try {
      while (keepReading && reader) {
        const { value, done } = await reader.read();
        if (done) break;
        if (value) ingest(value);
      }
    } catch {
      source.status = "error";
    }
  }

  // Buffer partial chunks and emit one event per complete `>>` line.
  function ingest(chunk: string) {
    buffer += chunk;
    let nl: number;
    while ((nl = buffer.indexOf("\n")) >= 0) {
      const line = buffer.slice(0, nl);
      buffer = buffer.slice(nl + 1);
      const event = parseLine(line);
      if (event) onEvent?.(event);
    }
  }

  return source;
}
