"use client";

// The "how it works" reveal — the part the audience doesn't know. A compact,
// looping CSS animation per act that shows the MECHANISM, not just the result.
// This is the teaching payload that beats a chatbot (can't do it) and a paper
// (won't show it live). BLE + badUSB here; deauth added in Batch 7.

import type { Act } from "../../lib/theater/types";

function BleDiagram() {
  return (
    <div className="mdiag ble" aria-hidden>
      <div className="md-node chip">VariOne</div>
      <div className="md-rings">
        <span />
        <span />
        <span />
      </div>
      <div className="md-node phone">
        <span className="md-popup">🔵 “AirPods” — Connect?</span>
      </div>
      <div className="md-cap">one spoofed advert · repeated · no pairing</div>
    </div>
  );
}

function BadusbDiagram() {
  return (
    <div className="mdiag usb" aria-hidden>
      <div className="md-node chip">VariOne · HID</div>
      <div className="md-stream">
        <span>⌨</span>
        <span>⌨</span>
        <span>⌨</span>
      </div>
      <div className="md-node screen">
        <code>$ _</code>
      </div>
      <div className="md-cap">seen as a keyboard · types its own keystrokes</div>
    </div>
  );
}

function DeauthDiagram() {
  return (
    <div className="mdiag deauth" aria-hidden>
      <div className="md-node chip">VariOne</div>
      <div className="md-frame">
        <span>⚡ DEAUTH</span>
      </div>
      <div className="md-node router">📶 AP</div>
      <div className="md-node phone drop">📱</div>
      <div className="md-cap">forged “disconnect” frame · 802.11 never checks who sent it</div>
    </div>
  );
}

function CloneDiagram() {
  return (
    <div className="mdiag clone" aria-hidden>
      <div className="md-node router off">📶 CIC_vari</div>
      <div className="md-node chip">🪤 CIC_vari</div>
      <div className="md-node phone pick">📱</div>
      <div className="md-cap">same name · no auth check · the phone can’t tell which is real</div>
    </div>
  );
}

export default function MechanismDiagram({ act }: { act: Act }) {
  switch (act) {
    case "ble":
      return <BleDiagram />;
    case "badusb":
      return <BadusbDiagram />;
    case "deauth":
      return <DeauthDiagram />;
    case "clone":
      return <CloneDiagram />;
    default:
      return null;
  }
}
