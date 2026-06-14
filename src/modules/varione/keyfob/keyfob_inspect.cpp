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

    rcswitch.resetAvailable(); // just noise, no stable frame
    return false;
}

// --------------------------------------------------------------------------
// Capture exactly one code on `frequency`. Prefers an RCSwitch decode (gives
// KeeLoq identifiers); falls back to a RAW signature for non-decoding remotes
// (car keys). Blocks until a code is captured or BACK. Returns false on BACK /
// RF init error.
// --------------------------------------------------------------------------
static bool captureOneCode(RfCodes &out, const String &prompt, float frequency) {
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
        if (rcswitch.available()) {
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
        } else if (rcswitch.RAWavailable()) {
            // No clean decode — try a RAW signature (car keys / odd protocols).
            if (captureRawInto(rcswitch, out, frequency)) {
                got = true;
                break;
            }
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
        padprintln("Sig A: " + String(a.key, HEX));
        padprintln("Sig B: " + String(b.key, HEX));
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

// --------------------------------------------------------------------------
// Any-freq scan: sweep the whole sub-GHz list reading RSSI while the operator
// holds the fob, lock onto the strongest frequency. Lets the RAW mode catch
// remotes that are not on 433.92. Returns false on BACK / RF init error; on no
// strong signal it falls back to 433.92 so capture can still be attempted.
// --------------------------------------------------------------------------
static bool scanForActiveFreq(float &outFreq) {
    drawMainBorderWithTitle("Keyfob Inspect");
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    padprintln("");
    padprintln("HOLD the fob button");
    padprintln("Scanning bands...");
    padprintln("");
    padprintln("[BACK] cancel");

    if (!initRfModule("rx", subghz_frequency_list[0])) {
        displayError("RF init failed", true);
        return false;
    }

    const int N = 57; // subghz_frequency_list size
    float bestFreq = 0;
    int bestRssi = -127;
    uint32_t scanEnd = millis() + 4000; // ~4 s window of sweeps
    while (millis() < scanEnd && !check(EscPress)) {
        for (int i = 0; i < N; i++) {
            setMHZ(subghz_frequency_list[i]);
            vTaskDelay(3 / portTICK_PERIOD_MS);
            int rssi = ELECHOUSE_cc1101.getRssi();
            if (rssi > bestRssi) {
                bestRssi = rssi;
                bestFreq = subghz_frequency_list[i];
            }
        }
    }
    deinitRfModule();
    if (check(EscPress)) return false;

    if (bestFreq <= 0 || bestRssi < -85) bestFreq = 433.92; // nothing strong -> default
    outFreq = bestFreq;
    Serial.printf("[KEYFOB] scan best=%.2f MHz rssi=%d\n", bestFreq, bestRssi);
    displayTextLine("Found " + String(bestFreq, 2) + " MHz");
    delay(800);
    return true;
}

void keyfob_inspect() {
    RfCodes a, b;

    // Two explicit modes (operator-chosen): RAW any-freq for non-decoding
    // remotes (car keys), decode for OOK/ASK remotes + KeeLoq (fan/garage).
    int mode = -1;
    returnToMenu = false;
    options = {
        {"Any-freq scan (RAW)", [&]() { mode = 0; }},
        {"Keylock / remote", [&]() { mode = 1; }},
    };
    loopOptions(options);
    options.clear();
    if (returnToMenu || mode < 0) return;

    float freq = bruceConfigPins.rfFreq > 0 ? bruceConfigPins.rfFreq : 433.92;
    if (mode == 0 && !scanForActiveFreq(freq)) return; // BACK during scan

    if (!captureOneCode(a, "Press fob (1/2)", freq)) {
        deinitRfModule();
        return;
    }
    delay(300);
    if (!captureOneCode(b, "Press again (2/2)", freq)) {
        deinitRfModule();
        return;
    }

    // KeeLoq (protocol 23) sets fix != 0 and is rolling by nature. Otherwise,
    // identical keys/signatures across two presses => FIXED; differing => ROLLING.
    bool keeloq = (a.fix != 0);
    bool fixed = !keeloq && a.key != 0 && (a.key == b.key);

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
