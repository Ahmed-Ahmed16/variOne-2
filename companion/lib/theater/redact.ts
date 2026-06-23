// Redaction for anything sensitive that reaches the wall. Default posture:
// spotlight-only — show just enough to be legible as "a real MAC / a real
// network" while masking the identifying middle. The presenter can reveal with
// a key (R) when the room is the operator's own gear.
//
// Credential VALUES never come here at all — the firmware keeps them on-device
// and the pipeline only ever models a COUNT. This is for MACs / SSIDs / the odd
// hostname that legitimately appears on screen.

/**
 * Mask a MAC to its first + last octet: `A4:C1:••:••:••:0F`. When `redacted`
 * is false the full MAC is returned (operator's own gear, reveal key pressed).
 */
export function redactMac(mac: string, redacted: boolean): string {
  if (!redacted || !mac) return mac;
  const parts = mac.split(":");
  if (parts.length < 2) return "••:••:••";
  return parts.map((p, i) => (i === 0 || i === parts.length - 1 ? p : "••")).join(":");
}

/**
 * Keep the first + last char of an SSID, mask the middle: `C•••••i`. Short
 * names (≤2 chars) blur entirely.
 */
export function redactSsid(ssid: string, redacted: boolean): string {
  if (!redacted || !ssid) return ssid;
  if (ssid.length <= 2) return "•".repeat(ssid.length);
  return ssid[0] + "•".repeat(Math.max(3, ssid.length - 2)) + ssid[ssid.length - 1];
}

/** Generic spotlight mask: keep the first `keep` chars, blur the rest. */
export function redactString(s: string, redacted: boolean, keep = 2): string {
  if (!redacted || !s) return s;
  if (s.length <= keep) return "•".repeat(s.length);
  return s.slice(0, keep) + "•".repeat(Math.max(3, s.length - keep));
}
