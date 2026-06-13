/*
 * VariOne Keyfob Inspect — see keyfob_inspect.h for the module responsibility.
 * Captures two keyfob presses on the configured RF frequency, classifies FIXED
 * vs ROLLING, optionally KeeLoq-decrypts, and renders an explain screen.
 */
#include "keyfob_inspect.h"
#include "core/display.h"
#include "modules/rf/rf_scan.h"  // PRESET_KEELOQ, keeloq_identify (extern)
#include "modules/rf/rf_utils.h" // initRfModule / deinitRfModule / reverse_bits
#include "modules/rf/structs.h"  // RfCodes
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <RCSwitch.h>
#include <globals.h>

// keeloq_identify() is a free function with external linkage defined in
// rf_scan.cpp; it is not declared in a public header, so forward-declare it.
void keeloq_identify(RfCodes &instance);

// --------------------------------------------------------------------------
// Capture exactly one decoded RF code on the configured frequency. Blocks until
// a code is decoded or BACK is pressed. Returns false on BACK / RF init error.
// --------------------------------------------------------------------------
static bool captureOneCode(RfCodes &out, const String &prompt) {
    drawMainBorderWithTitle("Keyfob Inspect");
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    padprintln("");
    padprintln(prompt);
    padprintln("");
    padprintln("Waiting for signal...");
    padprintln("");
    padprintln("[BACK] cancel");

    float frequency = bruceConfigPins.rfFreq;
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
// --------------------------------------------------------------------------
static void showExplain(bool keeloq, bool fixed) {
    drawMainBorderWithTitle("Why it matters");
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);

    if (keeloq || !fixed) {
        padprintln("Rolling code: each");
        padprintln("press sends a new");
        padprintln("encrypted counter.");
        padprintln("");
        padprintln("Replaying a captured");
        padprintln("code FAILS: receiver");
        padprintln("rejects a counter it");
        padprintln("already saw.");
        padprintln("");
        padprintln("Mitigation: rolling");
        padprintln("codes (KeeLoq) are");
        padprintln("the defense.");
    } else {
        padprintln("Fixed code: same code");
        padprintln("every press.");
        padprintln("");
        padprintln("A single capture can");
        padprintln("be replayed forever");
        padprintln("to open the device.");
        padprintln("");
        padprintln("Mitigation: replace");
        padprintln("with a rolling-code");
        padprintln("remote.");
    }

    padprintln("");
    padprintln("[BACK] exit");
}

void keyfob_inspect() {
    RfCodes a, b;

    if (!captureOneCode(a, "Press fob (1/2)")) {
        deinitRfModule();
        return;
    }
    delay(300);
    if (!captureOneCode(b, "Press again (2/2)")) {
        deinitRfModule();
        return;
    }

    // KeeLoq (protocol 23) sets fix != 0 and is rolling by nature. Otherwise,
    // identical keys across two presses => FIXED; differing keys => ROLLING.
    bool keeloq = (a.fix != 0);
    bool fixed = !keeloq && (a.key == b.key);

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

    showExplain(keeloq, fixed);
    waitNextOrBack(); // any exit returns to RF menu
}
