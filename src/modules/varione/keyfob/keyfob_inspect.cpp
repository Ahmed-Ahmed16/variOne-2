/*
 * VariOne Keyfob Inspect — see keyfob_inspect.h for the module responsibility.
 * Captures two keyfob presses on the configured RF frequency, classifies FIXED
 * vs ROLLING, optionally KeeLoq-decrypts, and renders an explain screen.
 */
#include "keyfob_inspect.h"
#include "core/display.h"
#include "modules/varione/ui/paged_text.h" // showPagedText
#include "modules/rf/rf_scan.h"  // PRESET_KEELOQ, keeloq_identify (extern)
#include "modules/rf/rf_utils.h" // initRfModule / deinitRfModule / reverse_bits
#include "modules/rf/structs.h"  // RfCodes
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <RCSwitch.h>
#include <globals.h>
#include <vector>

// keeloq_identify() is a free function with external linkage defined in
// rf_scan.cpp; it is not declared in a public header, so forward-declare it.
void keeloq_identify(RfCodes &instance);

// --------------------------------------------------------------------------
// RAW fallback (B4). Many car keys do not decode under RCSwitch and/or sit off
// the 433.92 default. When no decode arrives but a RAW waveform is buffered,
// build a stable signature from it — the SAME quantized-CRC scheme RFScan uses
// (find_pulse_index + crc64_ecma) so two presses of a fixed remote produce the
// same out.key, while a rolling remote produces different keys. Returns false
// if the buffer was just noise (no stable repeating frame).
// --------------------------------------------------------------------------
static bool captureRawInto(RCSwitch &rcswitch, RfCodes &out, float frequency) {
    vTaskDelay(400 / portTICK_PERIOD_MS); // let the whole signal land in the buffer
    unsigned int *raw = rcswitch.getRAWReceivedRawdata();
    uint64_t decoded = rcswitch.getReceivedValue();

    String dataStr = "";
    std::vector<int> durations;         // pulse indexes, for the CRC
    std::vector<int> indexed_durations; // distinct quantized pulse widths
    uint8_t repetition = 0;
    int te = 0;

    for (int t = 0; t < RCSWITCH_RAW_MAX_CHANGES; t++) {
        if (raw[t] == 0) break;
        if (t > 0) dataStr += " ";
        int sign = (t % 2 == 0) ? 1 : -1;
        int duration = sign * (int)raw[t];
        if (duration < -5000 && repetition < 2) repetition += 1;
        dataStr += String(duration);
        if (te == 0 && duration > 0) te = duration;
        if (!decoded && repetition == 1 && duration >= -5000) {
            int index = find_pulse_index(indexed_durations, duration);
            if (index == -1) {
                indexed_durations.push_back(abs(duration));
                index = indexed_durations.size() - 1;
            }
            durations.push_back(index);
        }
    }

    out = RfCodes();
    out.frequency = long(frequency * 1000000);
    out.te = te;
    out.data = dataStr;

    // A decode slipped in alongside the RAW buffer — treat it as a decode.
    if (decoded) {
        out.key = decoded;
        out.preset = String(rcswitch.getReceivedProtocol());
        out.protocol = "RcSwitch";
        out.Bit = rcswitch.getReceivedBitlength();
        if (rcswitch.getReceivedProtocol() == PRESET_KEELOQ) {
            uint64_t yek = reverse_bits(decoded, 64);
            out.fix = yek >> 32;
            out.btn = out.fix >> 28;
            out.encrypted = yek & 0xFFFFFFFF;
            out.serial = (yek >> 32) & 0xFFFFFFF;
            keeloq_identify(out);
        }
        rcswitch.resetAvailable();
        return true;
    }

    // No decode, but a repeating RAW frame -> stable CRC signature we can compare.
    if (repetition >= 2 && !durations.empty()) {
        out.protocol = "RAW";
        out.preset = "0";
        out.key = crc64_ecma(durations);
        out.indexed_durations = indexed_durations;
        out.Bit = durations.size();
        rcswitch.resetAvailable();
        return true;
    }

    // Permissive fallback (mirrors RFScan's non-codesOnly branch): accept ANY
    // raw waveform with real transitions, even without a clean repeating frame
    // to CRC. key stays 0; comparison falls back to the raw timing string. This
    // is what lets car keys / odd remotes register at all (they were rejected
    // before -> "nothing recorded").
    if (dataStr.length() > 0) {
        out.protocol = "RAW";
        out.preset = "0";
        out.key = 0;
        out.indexed_durations = indexed_durations;
        out.Bit = 0;
        rcswitch.resetAvailable();
        return true;
    }

    rcswitch.resetAvailable(); // truly nothing
    return false;
}

// --------------------------------------------------------------------------
// Capture exactly one code on `frequency`. Two disjoint paths so they can't
// starve each other:
//   rawMode=false (Keylock): poll the RCSwitch DECODE buffer; ignore undecoded
//     noise. Gives KeeLoq identifiers. Good for OOK/ASK remotes (fan/garage).
//   rawMode=true  (Any-freq): poll ONLY the RAW buffer and build a CRC sig.
//     Needed for rolling/car remotes that set available() with no clean decode
//     (those would otherwise reset the buffer before RAW ever ran).
// Blocks until a code is captured or BACK. Returns false on BACK / RF init err.
// --------------------------------------------------------------------------
static bool captureOneCode(RfCodes &out, const String &prompt, float frequency, bool rawMode) {
    drawMainBorderWithTitle("Keyfob Inspect");
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    padprintln("");
    padprintln(prompt);
    padprintln("");
    padprintln("Waiting for signal...");
    padprintln("");
    padprintln("[BACK] cancel");

    if (frequency <= 0) frequency = 433.92;

    if (!initRfModule("rx", frequency)) {
        displayError("RF init failed", true);
        return false;
    }

    RCSwitch rcswitch = RCSwitch();
    if (bruceConfigPins.rfModule == CC1101_SPI_MODULE) {
        rcswitch.enableReceive(bruceConfigPins.CC1101_bus.io0);
    } else {
        rcswitch.enableReceive(bruceConfigPins.rfRx);
    }
    rcswitch.resetAvailable();

    bool got = false;
    while (!check(EscPress)) {
        if (rawMode) {
            // RAW-only: build a CRC signature from any repeating waveform.
            if (rcswitch.RAWavailable()) {
                if (captureRawInto(rcswitch, out, frequency)) {
                    got = true;
                    break;
                }
                rcswitch.resetAvailable(); // was noise; keep listening
            }
        } else if (rcswitch.available()) {
            uint64_t decoded = rcswitch.getReceivedValue();
            if (decoded) {
                out = RfCodes(); // clear all fields
                out.frequency = long(frequency * 1000000);
                out.key = decoded;
                out.preset = String(rcswitch.getReceivedProtocol());
                out.protocol = "RcSwitch";
                out.te = rcswitch.getReceivedDelay();
                out.Bit = rcswitch.getReceivedBitlength();
                out.data = "";

                // KeeLoq (RCSwitch protocol 23): split the 66-bit frame into the
                // fixed (serial+button) and encrypted (hopping) halves, then try
                // the keystore to recover serial + counter. Mirrors rf_scan.cpp.
                if (rcswitch.getReceivedProtocol() == PRESET_KEELOQ) {
                    uint64_t yek = reverse_bits(decoded, 64);
                    out.fix = yek >> 32;
                    out.btn = out.fix >> 28;
                    out.encrypted = yek & 0xFFFFFFFF;
                    out.serial = (yek >> 32) & 0xFFFFFFF;
                    keeloq_identify(out);
                }

                got = true;
                rcswitch.resetAvailable();
                break;
            }
            rcswitch.resetAvailable();
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    deinitRfModule();
    return got;
}

// Wait for OK (advance) or BACK (exit). Returns true if BACK was pressed.
static bool waitNextOrBack() {
    while (1) {
        if (check(SelPress)) return false;
        if (check(EscPress)) return true;
        vTaskDelay(30 / portTICK_PERIOD_MS);
    }
}

// --------------------------------------------------------------------------
// Page 1: classification + identifiers.
// --------------------------------------------------------------------------
static void showClassification(const RfCodes &a, const RfCodes &b, bool keeloq, bool fixed) {
    drawMainBorderWithTitle("Keyfob Inspect");
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);

    if (keeloq) {
        padprintln("Protocol: KeeLoq");
        padprintln("Type: ROLLING");
        if (a.mf_name != "Unknown") {
            padprintln("Mfr: " + a.mf_name);
            padprintln("Serial: " + String(a.serial, HEX));
            // Counter must increment between two presses of a healthy fob.
            padprintln("Cnt: " + String(a.cnt) + " -> " + String(b.cnt));
        } else {
            padprintln("Key: unknown");
            padprintln("(no mfr key loaded)");
        }
    } else if (a.protocol == "RAW") {
        // Non-decoding remote (e.g. car key): compare RAW signatures.
        padprintln("Protocol: RAW");
        padprintln(fixed ? "Type: FIXED" : "Type: ROLLING");
        if (a.key != 0 || b.key != 0) {
            padprintln("Sig A: " + String(a.key, HEX));
            padprintln("Sig B: " + String(b.key, HEX));
        } else {
            // No CRC frame; show raw edge counts so the two captures are visible.
            auto edges = [](const String &s) {
                int n = s.length() ? 1 : 0;
                for (int i = 0; i < (int)s.length(); i++)
                    if (s[i] == ' ') n++;
                return n;
            };
            padprintln("Edges A: " + String(edges(a.data)));
            padprintln("Edges B: " + String(edges(b.data)));
        }
        padprintln("Freq: " + String(a.frequency / 1000000.0, 2) + " MHz");
    } else {
        padprintln("Protocol: " + a.protocol + "(" + a.preset + ")");
        padprintln(fixed ? "Type: FIXED" : "Type: ROLLING");
        padprintln("Code A: " + String(a.key, HEX));
        padprintln("Code B: " + String(b.key, HEX));
    }

    padprintln("");
    padprintln("[OK] why  [BACK] exit");
}

// --------------------------------------------------------------------------
// Page 2: explain — why replay fails on rolling codes, and the mitigation.
// Body text is built here and shown through the scrollable paged viewer so it
// stays readable however long it grows.
// --------------------------------------------------------------------------
static String explainBody(bool keeloq, bool fixed) {
    if (keeloq || !fixed) {
        return "Rolling code: each press sends a new encrypted counter.\n"
               "\n"
               "Replaying a captured code FAILS: the receiver rejects a counter "
               "it has already seen.\n"
               "\n"
               "Mitigation: rolling codes (KeeLoq) are the defense.";
    }
    return "Fixed code: the same code every press.\n"
           "\n"
           "A single capture can be replayed forever to open the device.\n"
           "\n"
           "Mitigation: replace with a rolling-code remote.";
}

// Lenient compare of two raw timing strings for the key==0 case (no decode, no
// CRC frame). Fixed remotes repeat near-identically bar timing jitter; rolling
// codes differ heavily. Returns true (FIXED) if most tokens line up.
static bool rawSimilar(const String &a, const String &b) {
    int ia = 0, ib = 0, total = 0, match = 0;
    while (ia < (int)a.length() && ib < (int)b.length()) {
        int na = a.indexOf(' ', ia);
        if (na < 0) na = a.length();
        int nb = b.indexOf(' ', ib);
        if (nb < 0) nb = b.length();
        long va = a.substring(ia, na).toInt();
        long vb = b.substring(ib, nb).toInt();
        long tol = max(80L, labs(va) / 4); // within 25% or 80us
        if (labs(va - vb) <= tol) match++;
        total++;
        ia = na + 1;
        ib = nb + 1;
    }
    return total > 8 && (match * 100 / total) >= 80;
}

// Pick a capture frequency. Manual, not an RSSI auto-scan — a full-band RSSI
// sweep reliably mis-locks onto ambient noise (e.g. 300 MHz) instead of the
// remote's real band. Default to the configured RF freq.
static float pickFrequency() {
    float freq = bruceConfigPins.rfFreq > 0 ? bruceConfigPins.rfFreq : 433.92;
    options = {
        {"433.92 MHz", [&]() { freq = 433.92; }},
        {"315 MHz", [&]() { freq = 315.0; }},
        {"868.35 MHz", [&]() { freq = 868.35; }},
        {"915 MHz", [&]() { freq = 915.0; }},
    };
    loopOptions(options);
    options.clear();
    return freq;
}

void keyfob_inspect() {
    RfCodes a, b;

    // Two explicit modes (operator-chosen): RAW any-freq for rolling/non-
    // decoding remotes (car keys), decode for OOK/ASK remotes + KeeLoq (fan).
    int mode = -1;
    returnToMenu = false;
    options = {
        {"Any-freq (RAW)", [&]() { mode = 0; }},
        {"Keylock / remote", [&]() { mode = 1; }},
    };
    loopOptions(options);
    options.clear();
    if (returnToMenu || mode < 0) return;

    bool rawMode = (mode == 0);
    // RAW mode lets the operator pick the band (car keys may be off 433.92);
    // decode mode stays on the configured/default freq.
    float freq = bruceConfigPins.rfFreq > 0 ? bruceConfigPins.rfFreq : 433.92;
    if (rawMode) {
        returnToMenu = false;
        freq = pickFrequency();
        if (returnToMenu) return;
    }

    if (!captureOneCode(a, "Press fob (1/2)", freq, rawMode)) {
        deinitRfModule();
        return;
    }
    delay(300);
    if (!captureOneCode(b, "Press again (2/2)", freq, rawMode)) {
        deinitRfModule();
        return;
    }

    // KeeLoq (protocol 23, fix != 0) is rolling by nature. Else: prefer a real
    // key (decode value or RAW CRC) — equal => FIXED. When neither press yielded
    // a key (raw-only car remotes), fall back to comparing the raw waveforms.
    bool keeloq = (a.fix != 0);
    bool fixed;
    if (keeloq) fixed = false;
    else if (a.key != 0 || b.key != 0) fixed = (a.key != 0 && a.key == b.key);
    else fixed = rawSimilar(a.data, b.data);

    Serial.printf(
        "[KEYFOB] keeloq=%d fixed=%d A=%llx B=%llx mfr=%s\n",
        keeloq,
        fixed,
        (unsigned long long)a.key,
        (unsigned long long)b.key,
        a.mf_name.c_str()
    );

    showClassification(a, b, keeloq, fixed);
    if (waitNextOrBack()) return; // BACK on page 1

    showPagedText("Why it matters", explainBody(keeloq, fixed));
}
