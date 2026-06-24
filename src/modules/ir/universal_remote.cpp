#include "universal_remote.h"
#include "TV-B-Gone.h" // checkIrTxPin()
#include "core/display.h"
#include "core/settings.h"
#include "core/utils.h" // addOptionToMainMenu()
#include "custom_ir.h"  // sendDecodedCommand()
#include "ir_utils.h"   // setup_ir_pin()
#include "ir_read.h"   // IrRead() capture fallback
#include <IRac.h>
#include <IRsend.h>

// ---------------------------------------------------------------------------
// Send helpers — every transmission funnels through the existing senders.
// ---------------------------------------------------------------------------

// Simple (non-stateful) protocols: Samsung, NEC/LG, Sony, Epson, etc.
static void sendCode(const char *protocol, const char *value, uint8_t bits) {
    setup_ir_pin(bruceConfigPins.irTx, OUTPUT);
    if (sendDecodedCommand(String(protocol), String(value), bits, true)) {
        displaySuccess("Sent " + String(protocol));
    } else {
        displayRedStripe("Send failed: " + String(protocol));
    }
    delay(600);
    digitalWrite(bruceConfigPins.irTx, LED_OFF);
}

// Air-conditioner state via IRac (fully parametric — no magic codes needed).
static void sendAc(decode_type_t proto, bool power, int tempC) {
    setup_ir_pin(bruceConfigPins.irTx, OUTPUT);
    IRac ac(bruceConfigPins.irTx);
    IRac::initState(&ac.next);
    ac.next.protocol = proto;
    ac.next.power = power;
    ac.next.mode = stdAc::opmode_t::kCool;
    ac.next.celsius = true;
    ac.next.degrees = tempC;
    ac.next.fanspeed = stdAc::fanspeed_t::kAuto;
    displayTextLine(power ? "AC ON " + String(tempC) + "C" : "AC OFF");
    bool ok = ac.sendAc();
    if (ok) displaySuccess("AC command sent");
    else displayRedStripe("AC proto unsupported");
    delay(700);
    digitalWrite(bruceConfigPins.irTx, LED_OFF);
}

// ---------------------------------------------------------------------------
// Brand command tables (best-effort defaults — confirm on real hardware).
// ---------------------------------------------------------------------------

static void samsungTvMenu() {
AGAIN:
    options = {
        {"Power",  []() { sendCode("SAMSUNG", "0xE0E040BF", 32); }},
        {"Vol +",  []() { sendCode("SAMSUNG", "0xE0E0E01F", 32); }},
        {"Vol -",  []() { sendCode("SAMSUNG", "0xE0E0D02F", 32); }},
        {"Mute",   []() { sendCode("SAMSUNG", "0xE0E0F00F", 32); }},
        {"Source", []() { sendCode("SAMSUNG", "0xE0E0807F", 32); }},
    };
    addOptionToMainMenu();
    loopOptions(options, MENU_TYPE_SUBMENU, "Samsung TV");
    if (!returnToMenu) goto AGAIN;
}

static void lgTvMenu() {
AGAIN:
    // LG TVs use the NEC protocol.
    options = {
        {"Power",  []() { sendCode("NEC", "0x20DF10EF", 32); }},
        {"Vol +",  []() { sendCode("NEC", "0x20DF40BF", 32); }},
        {"Vol -",  []() { sendCode("NEC", "0x20DFC03F", 32); }},
        {"Mute",   []() { sendCode("NEC", "0x20DF906F", 32); }},
        {"Input",  []() { sendCode("NEC", "0x20DFD02F", 32); }},
    };
    addOptionToMainMenu();
    loopOptions(options, MENU_TYPE_SUBMENU, "LG TV");
    if (!returnToMenu) goto AGAIN;
}

static void tvMenu() {
AGAIN:
    options = {
        {"Samsung", []() { samsungTvMenu(); }},
        {"LG",      []() { lgTvMenu(); }     },
    };
    addOptionToMainMenu();
    loopOptions(options, MENU_TYPE_SUBMENU, "TV brand");
    if (!returnToMenu) goto AGAIN;
}

static void epsonProjMenu() {
AGAIN:
    // Epson projectors use the EPSON protocol (NEC-family). Power is a discrete
    // ON/OFF on most models; these are best-effort and may need capture.
    options = {
        {"Power",   []() { sendCode("EPSON", "0xC1AA09F6", 32); }},
        {"Source",  []() { sendCode("EPSON", "0xC1AA48B7", 32); }},
        {"Menu",    []() { sendCode("EPSON", "0xC1AA9867", 32); }},
    };
    addOptionToMainMenu();
    loopOptions(options, MENU_TYPE_SUBMENU, "Epson Proj");
    if (!returnToMenu) goto AGAIN;
}

static void sonyProjMenu() {
AGAIN:
    // Sony uses SIRC. Projector power-toggle / input on the 12-bit SIRC space.
    options = {
        {"Power",  []() { sendCode("SONY", "0xA8B47", 20); }},
        {"Input",  []() { sendCode("SONY", "0xA8B4F", 20); }},
    };
    addOptionToMainMenu();
    loopOptions(options, MENU_TYPE_SUBMENU, "Sony Proj");
    if (!returnToMenu) goto AGAIN;
}

static void projectorMenu() {
AGAIN:
    options = {
        {"Epson", []() { epsonProjMenu(); }},
        {"Sony",  []() { sonyProjMenu(); } },
    };
    addOptionToMainMenu();
    loopOptions(options, MENU_TYPE_SUBMENU, "Projector");
    if (!returnToMenu) goto AGAIN;
}

static void acMenu() {
    decode_type_t proto = decode_type_t::CARRIER_AC64;
    String label = "Carrier AC";
    options = {
        {"Carrier",            [&]() { proto = decode_type_t::CARRIER_AC64; label = "Carrier AC"; }},
        {"Union Air (Gree)",   [&]() { proto = decode_type_t::GREE; label = "Union/Gree AC"; }     },
    };
    loopOptions(options, MENU_TYPE_SUBMENU, "AC brand");
    if (returnToMenu) return;

    int temp = 24;
AGAIN:
    options = {
        {"Power ON (Cool)",  [&]() { sendAc(proto, true, temp); }      },
        {"Power OFF",        [&]() { sendAc(proto, false, temp); }     },
        {"Temp +",           [&]() { if (temp < 30) temp++; }          },
        {"Temp -",           [&]() { if (temp > 16) temp--; }          },
    };
    addOptionToMainMenu();
    loopOptions(options, MENU_TYPE_SUBMENU, (label + " " + String(temp) + "C").c_str());
    if (!returnToMenu) goto AGAIN;
}

void universalRemoteMenu() {
    checkIrTxPin();
AGAIN:
    options = {
        {"Air Conditioner", []() { acMenu(); }      },
        {"TV",              []() { tvMenu(); }       },
        {"Projector",       []() { projectorMenu(); }},
        {"Capture your own",
         []() { IrRead(); } }, // existing IR-Read path: capture + replay any remote
    };
    addOptionToMainMenu();
    loopOptions(options, MENU_TYPE_SUBMENU, "Universal Remote");
    if (!returnToMenu) goto AGAIN;
}
