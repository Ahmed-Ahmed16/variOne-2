#include "core/powerSave.h"
#include "core/config.h"
#include "core/configPins.h"
#include <interface.h>

/***************************************************************************************
** Function name: _setup_gpio()
** Description:   VariOne S3 board GPIO setup for buttons and idle SPI chip selects.
***************************************************************************************/
void _setup_gpio() {
    pinMode(L_BTN, INPUT_PULLUP);
    pinMode(UP_BTN, INPUT_PULLUP);
    pinMode(R_BTN, INPUT_PULLUP);
    pinMode(DW_BTN, INPUT_PULLUP);
    pinMode(OK_BTN, INPUT_PULLUP);
    pinMode(BACK_BTN, INPUT_PULLUP);
    pinMode(CC1101_SS_PIN, OUTPUT);
    pinMode(NRF24_SS_PIN, OUTPUT);
    pinMode(SDCARD_CS, OUTPUT);
    digitalWrite(CC1101_SS_PIN, HIGH);
    digitalWrite(NRF24_SS_PIN, HIGH);
    digitalWrite(SDCARD_CS, HIGH);
}

void _post_setup_gpio() {
    bruceConfigPins.rfidModule = PN532_I2C_MODULE;
    bruceConfigPins.rfModule  = CC1101_SPI_MODULE;
    bruceConfigPins.irTx  = 18;
    bruceConfigPins.irRx  = 14;
    bruceConfigPins.rfTx  = 18;
    bruceConfigPins.rfRx  = 14;
    bruceConfig.dimmerSet = 0;
    // Clear any stale startup app saved in NVS — always boot to main menu
    if (bruceConfig.startupApp != "") bruceConfig.setStartupApp("");
    Serial.println("[VariOne] forced board pin/module config");
}

int getBattery() { return 0; }
bool isCharging() { return false; }
void _setBrightness(uint8_t brightval) {}
void powerOff() {}
void checkReboot() {}

/*********************************************************************
** Function: InputHandler
** Description: Six-button VariOne S3 input: LEFT/RIGHT navigate, OK selects, BACK escapes.
**
** With VARIONE_LONGPRESS_NAV (default on), OK and BACK gain a long-press
** action so full navigate+select+back+open-options is reachable from just
** OK+BACK while the D-pad is flaky:
**   OK   short -> SelPress (select)   long -> NextPress (down / open RF opts / save IR)
**   BACK short -> EscPress (back)     long -> PrevPress (up)
** Short actions fire on release (< threshold); long fires once on hold.
** The six original short-press mappings stay live for any working directional.
**********************************************************************/
void InputHandler(void) {
    static unsigned long tm = millis();

    bool left = digitalRead(L_BTN) == LOW;
    bool up = digitalRead(UP_BTN) == LOW;
    bool right = digitalRead(R_BTN) == LOW;
    bool down = digitalRead(DW_BTN) == LOW;
    bool ok = digitalRead(OK_BTN) == LOW;
    bool back = digitalRead(BACK_BTN) == LOW;
    unsigned long now = millis();

#if VARIONE_LONGPRESS_NAV
    // Per-button edge state for OK/BACK long-press nav.
    static bool okPrev = false, backPrev = false;
    static unsigned long okStart = 0, backStart = 0;
    static bool okLong = false, backLong = false;
    static bool okWake = false, backWake = false;

    // ---- OK: short=SEL (on release), long=NEXT (on hold) ----
    if (ok && !okPrev) { // press edge
        okStart = now;
        okLong = false;
        okWake = (isScreenOff || dimmer); // swallow press that only wakes screen
        wakeUpScreen();
    }
    if (ok && !okLong && !okWake && (now - okStart >= VARIONE_LONGPRESS_MS)) {
        NextPress = true;
        AnyKeyPress = true;
        okLong = true;
        Serial.println("[BTN] OK long=1 -> NEXT");
    }
    if (!ok && okPrev && !okLong && !okWake) { // release as short press
        SelPress = true;
        AnyKeyPress = true;
        Serial.println("[BTN] OK short -> SEL");
    }
    okPrev = ok;

    // ---- BACK: short=ESC (on release), long=PREV (on hold) ----
    if (back && !backPrev) {
        backStart = now;
        backLong = false;
        backWake = (isScreenOff || dimmer);
        wakeUpScreen();
    }
    if (back && !backLong && !backWake && (now - backStart >= VARIONE_LONGPRESS_MS)) {
        PrevPress = true;
        AnyKeyPress = true;
        backLong = true;
        Serial.println("[BTN] BACK long=1 -> PREV");
    }
    if (!back && backPrev && !backLong && !backWake) {
        EscPress = true;
        AnyKeyPress = true;
        Serial.println("[BTN] BACK short -> ESC");
    }
    backPrev = back;
#endif

    if (!(now - tm > 200 || LongPress)) return;

    if (left || up || right || down || ok || back) {
        Serial.printf(
            "[BTN] L=%d U=%d R=%d D=%d OK=%d BACK=%d wake=%d\n",
            left, up, right, down, ok, back, (isScreenOff || dimmer)
        );
        tm = now;
        if (!wakeUpScreen()) AnyKeyPress = true;
        else return;
    }

    if (left) PrevPress = true;
    if (right) NextPress = true;
    if (up) UpPress = true;
    if (down) DownPress = true;
#if !VARIONE_LONGPRESS_NAV
    if (ok) SelPress = true;
    if (back) {
        EscPress = true;
        Serial.println("[BTN] ESC set");
    }
#endif
}
