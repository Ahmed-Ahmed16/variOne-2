/*
 * test_ir.cpp — Smoke test for IR TX (GPIO 25) and IR RX (GPIO 34).
 * Implements PRD §9 IR feature hardware verification.
 * No main.cpp changes. Run with: pio run -e ir_test -t upload
 *
 * Pass criteria:
 *   RX: pointing any IR remote at HX1838 prints decoded protocol + hex code.
 *   TX: sending NEC 0xDEADBEEF every 3s — HX1838 should echo it back and
 *       print "LOOPBACK OK" if TX and RX are both working.
 */

#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRsend.h>
#include <IRutils.h>

#define PIN_IR_RX  34
#define PIN_IR_TX  25

#define TEST_NEC_ADDR 0xDEAD
#define TEST_NEC_CMD  0xBEEF

IRrecv irrecv(PIN_IR_RX, 1024, 50, true);
IRsend irsend(PIN_IR_TX);

decode_results results;

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("=== IR SMOKE TEST ===");
    Serial.printf("IR RX: GPIO %d\n", PIN_IR_RX);
    Serial.printf("IR TX: GPIO %d\n", PIN_IR_TX);

    irsend.begin();
    irrecv.enableIRIn();

    Serial.println("TX init OK");
    Serial.println("RX init OK");
    Serial.println("Point any remote at sensor — decoded output below.");
    Serial.println("TX sends NEC test code every 3s; loopback prints LOOPBACK OK.");
    Serial.println("-----------------------------------------------------");
}

static uint32_t lastTx = 0;

void loop() {
    // TX: send test NEC code every 3 seconds
    if (millis() - lastTx >= 3000) {
        lastTx = millis();
        Serial.println("[TX] sending NEC 0xDEADBEEF...");
        irsend.sendNEC(irsend.encodeNEC(TEST_NEC_ADDR, TEST_NEC_CMD));
        irrecv.resume();  // re-arm after TX
    }

    // RX: print any received signal
    if (irrecv.decode(&results)) {
        Serial.printf("[RX] protocol=%-12s  hex=0x%08llX  bits=%d  RSSI=%d\n",
            typeToString(results.decode_type, results.repeat).c_str(),
            results.value,
            results.bits,
            results.overflow ? -1 : 0);

        // loopback check
        if (results.decode_type == NEC &&
            results.value == irsend.encodeNEC(TEST_NEC_ADDR, TEST_NEC_CMD)) {
            Serial.println("  >>> LOOPBACK OK — TX and RX both working <<<");
        }

        irrecv.resume();
    }
}
