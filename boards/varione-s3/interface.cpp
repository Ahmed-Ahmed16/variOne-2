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
**********************************************************************/
void InputHandler(void) {
    static unsigned long tm = millis();
    if (!(millis() - tm > 200 || LongPress)) return;

    bool left = digitalRead(L_BTN) == LOW;
    bool up = digitalRead(UP_BTN) == LOW;
    bool right = digitalRead(R_BTN) == LOW;
    bool down = digitalRead(DW_BTN) == LOW;
    bool ok = digitalRead(OK_BTN) == LOW;
    bool back = digitalRead(BACK_BTN) == LOW;

    if (left || up || right || down || ok || back) {
        Serial.printf(
            "[BTN] L=%d U=%d R=%d D=%d OK=%d BACK=%d wake=%d\n",
            left, up, right, down, ok, back, (isScreenOff || dimmer)
        );
        tm = millis();
        if (!wakeUpScreen()) AnyKeyPress = true;
        else return;
    }

    if (left) PrevPress = true;
    if (right) NextPress = true;
    if (up) UpPress = true;
    if (down) DownPress = true;
    if (ok) SelPress = true;
    if (back) {
        EscPress = true;
        Serial.println("[BTN] ESC set");
    }
}
