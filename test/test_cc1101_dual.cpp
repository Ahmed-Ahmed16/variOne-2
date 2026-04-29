/*
 * test_cc1101_dual.cpp — Smoke test for dual CC1101 on shared VSPI.
 * Verifies both modules respond with correct chip identity registers
 * before any RollJam integration into main.cpp.
 *
 * CC1101 #1: CS=15, GDO0=4  (existing, primary RX)
 * CC1101 #2: CS=25, GDO0=34 (new, jammer/TX; IR TX postponed for this test)
 * Shared VSPI: SCK=18, MISO=19, MOSI=23
 *
 * Pass criteria: both chips print partnum=0x00 version=0x04 or 0x14
 * Run with: pio run -e cc1101_dual_test -t upload
 */

#include <Arduino.h>
#include <SPI.h>
#include <ELECHOUSE_CC1101_SRC_DRV.h>

#define PIN_VSPI_SCK   18
#define PIN_VSPI_MISO  19
#define PIN_VSPI_MOSI  23

#define PIN_CC1_CS     15
#define PIN_CC1_GDO0   4

#define PIN_CC2_CS     25
#define PIN_CC2_GDO0   34

#define PIN_SD_CS      5

// Raw SPI register read — bypasses driver, uses whichever CS is passed
static uint8_t rawReadStatus(uint8_t csPin, uint8_t addr) {
    SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
    digitalWrite(csPin, LOW);
    delayMicroseconds(50);
    SPI.transfer(addr | 0xC0);
    uint8_t val = SPI.transfer(0x00);
    digitalWrite(csPin, HIGH);
    SPI.endTransaction();
    return val;
}

static bool testChip(uint8_t csPin, uint8_t gdo0Pin, const char* label) {
    Serial.printf("\n[DUAL-TEST] === %s (CS=%d GDO0=%d) ===\n", label, csPin, gdo0Pin);

    // deselect other chip
    uint8_t otherCs = (csPin == PIN_CC1_CS) ? PIN_CC2_CS : PIN_CC1_CS;
    digitalWrite(otherCs, HIGH);
    delay(10);

    uint8_t partnum = rawReadStatus(csPin, CC1101_PARTNUM);
    uint8_t version = rawReadStatus(csPin, CC1101_VERSION);
    uint8_t marc    = rawReadStatus(csPin, CC1101_MARCSTATE);

    Serial.printf("[DUAL-TEST] %s raw: partnum=0x%02X version=0x%02X marc=0x%02X\n",
                  label, partnum, version, marc);
    Serial.println("[DUAL-TEST] expected: partnum=0x00, version=0x04 or 0x14");

    bool ok = (partnum == 0x00) && (version == 0x04 || version == 0x14);
    if (!ok) {
        Serial.printf("[DUAL-TEST] %s FAIL — chip not detected. Check wiring.\n", label);
        return false;
    }

    // Init via driver
    ELECHOUSE_cc1101.setSpiPin(PIN_VSPI_SCK, PIN_VSPI_MISO, PIN_VSPI_MOSI, csPin);
    ELECHOUSE_cc1101.setGDO0(gdo0Pin);
    ELECHOUSE_cc1101.Init();

    // Apply OOK RX
    ELECHOUSE_cc1101.setMHZ(433.92f);
    ELECHOUSE_cc1101.setCCMode(0);
    ELECHOUSE_cc1101.setModulation(2);
    ELECHOUSE_cc1101.setDRate(3.79372f);
    ELECHOUSE_cc1101.setRxBW(325.0f);
    ELECHOUSE_cc1101.setSyncMode(0);
    ELECHOUSE_cc1101.setCrc(false);
    ELECHOUSE_cc1101.SetRx();
    delay(100);

    int rssi = ELECHOUSE_cc1101.getRssi();
    Serial.printf("[DUAL-TEST] %s PASS — driver init OK, RSSI=%ddBm\n", label, rssi);
    return true;
}

static bool cc1ok = false;
static bool cc2ok = false;
static bool holdCc2CsLow = false;

static void deselectBoth() {
    digitalWrite(PIN_CC1_CS, HIGH);
    digitalWrite(PIN_CC2_CS, holdCc2CsLow ? LOW : HIGH);
}

static void printCommands() {
    Serial.println();
    Serial.println("[DUAL-TEST] commands:");
    Serial.println("  h  help");
    Serial.println("  1  raw-read CC1101-1 once");
    Serial.println("  2  raw-read CC1101-2 once");
    Serial.println("  p  print CS/GDO pin levels");
    Serial.println("  l  hold CC1101-2 CS LOW for multimeter check");
    Serial.println("  H  release CC1101-2 CS HIGH");
    Serial.println();
}

static void printRawOnce(uint8_t csPin, uint8_t gdo0Pin, const char* label) {
    uint8_t otherCs = (csPin == PIN_CC1_CS) ? PIN_CC2_CS : PIN_CC1_CS;
    digitalWrite(otherCs, HIGH);
    delayMicroseconds(100);
    uint8_t partnum = rawReadStatus(csPin, CC1101_PARTNUM);
    uint8_t version = rawReadStatus(csPin, CC1101_VERSION);
    uint8_t marc = rawReadStatus(csPin, CC1101_MARCSTATE);
    Serial.printf("[DUAL-TEST] %s manual raw: partnum=0x%02X version=0x%02X marc=0x%02X gdo0=%d cs=%d\n",
                  label, partnum, version, marc, digitalRead(gdo0Pin), digitalRead(csPin));
}

static void printPinLevels() {
    Serial.printf("[DUAL-TEST] pins: cc1_cs=%d cc1_gdo0=%d cc2_cs=%d cc2_gdo0=%d hold_cc2_cs_low=%s\n",
                  digitalRead(PIN_CC1_CS), digitalRead(PIN_CC1_GDO0),
                  digitalRead(PIN_CC2_CS), digitalRead(PIN_CC2_GDO0),
                  holdCc2CsLow ? "yes" : "no");
}

static void handleSerial() {
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') continue;
        if (c == 'h') {
            printCommands();
        } else if (c == '1') {
            printRawOnce(PIN_CC1_CS, PIN_CC1_GDO0, "CC1101-1");
        } else if (c == '2') {
            printRawOnce(PIN_CC2_CS, PIN_CC2_GDO0, "CC1101-2");
        } else if (c == 'p') {
            printPinLevels();
        } else if (c == 'l') {
            holdCc2CsLow = true;
            digitalWrite(PIN_CC1_CS, HIGH);
            digitalWrite(PIN_CC2_CS, LOW);
            Serial.println("[DUAL-TEST] CC1101-2 CS held LOW; measure module CSN, then send H");
        } else if (c == 'H') {
            holdCc2CsLow = false;
            digitalWrite(PIN_CC2_CS, HIGH);
            Serial.println("[DUAL-TEST] CC1101-2 CS released HIGH");
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(1200);
    Serial.println("=== DUAL CC1101 SMOKE TEST v2 ===");
    Serial.printf("CC1101 #1: CS=%d GDO0=%d\n", PIN_CC1_CS, PIN_CC1_GDO0);
    Serial.printf("CC1101 #2: CS=%d GDO0=%d\n", PIN_CC2_CS, PIN_CC2_GDO0);
    Serial.println("Shared VSPI: SCK=18 MISO=19 MOSI=23");
    Serial.println("Note: CC1101 #2 CS uses GPIO25; IR TX is postponed for this test");
    printCommands();

    // Pull SD CS high to prevent bus contention
    pinMode(PIN_SD_CS, OUTPUT);
    digitalWrite(PIN_SD_CS, HIGH);

    pinMode(PIN_CC1_CS, OUTPUT); digitalWrite(PIN_CC1_CS, HIGH);
    pinMode(PIN_CC2_CS, OUTPUT); digitalWrite(PIN_CC2_CS, HIGH);
    pinMode(PIN_CC1_GDO0, INPUT);
    pinMode(PIN_CC2_GDO0, INPUT);
    deselectBoth();

    SPI.begin(PIN_VSPI_SCK, PIN_VSPI_MISO, PIN_VSPI_MOSI);
    delay(100);

    cc1ok = testChip(PIN_CC1_CS, PIN_CC1_GDO0, "CC1101-1");
    delay(50);
    cc2ok = testChip(PIN_CC2_CS, PIN_CC2_GDO0, "CC1101-2");

    Serial.println("\n[DUAL-TEST] === RESULT ===");
    Serial.printf("[DUAL-TEST] CC1101 #1: %s\n", cc1ok ? "PASS" : "FAIL");
    Serial.printf("[DUAL-TEST] CC1101 #2: %s\n", cc2ok ? "PASS" : "FAIL");

    if (cc1ok && cc2ok) {
        Serial.println("[DUAL-TEST] Both chips detected — safe to integrate.");
    } else {
        Serial.println("[DUAL-TEST] Fix wiring on failing chip before integration.");
        if (!cc2ok) {
            Serial.println("[DUAL-TEST] CC1101 #2 checklist:");
            Serial.println("  CS  → GPIO 25 (IR TX postponed for this test)");
            Serial.println("  GDO0→ GPIO 34 (input-only OK; do not use GPIO12)");
            Serial.println("  SCK → GPIO 18 (shared)");
            Serial.println("  MISO→ GPIO 19 (shared)");
            Serial.println("  MOSI→ GPIO 23 (shared)");
            Serial.println("  VCC → 3.3V only");
        }
    }
}

void loop() {
    handleSerial();

    if (holdCc2CsLow) {
        delay(20);
        return;
    }

    // Live status from both chips every 2s. Raw register reads are printed even
    // for failed chips so wiring fixes are visible without reflashing.
    static uint32_t last = 0;
    if (millis() - last >= 2000) {
        last = millis();
        if (cc1ok) {
            digitalWrite(PIN_CC2_CS, HIGH);
            delayMicroseconds(100);
            ELECHOUSE_cc1101.setSpiPin(PIN_VSPI_SCK, PIN_VSPI_MISO, PIN_VSPI_MOSI, PIN_CC1_CS);
            ELECHOUSE_cc1101.setGDO0(PIN_CC1_GDO0);
            Serial.printf("[DUAL-TEST] CC1101-1 rssi=%ddBm gdo0=%d\n",
                          ELECHOUSE_cc1101.getRssi(), digitalRead(PIN_CC1_GDO0));
        } else {
            digitalWrite(PIN_CC2_CS, HIGH);
            delayMicroseconds(100);
            uint8_t partnum = rawReadStatus(PIN_CC1_CS, CC1101_PARTNUM);
            uint8_t version = rawReadStatus(PIN_CC1_CS, CC1101_VERSION);
            Serial.printf("[DUAL-TEST] CC1101-1 retry raw: partnum=0x%02X version=0x%02X gdo0=%d\n",
                          partnum, version, digitalRead(PIN_CC1_GDO0));
            if (partnum == 0x00 && (version == 0x04 || version == 0x14)) {
                cc1ok = true;
                Serial.println("[DUAL-TEST] CC1101-1 recovered — raw ID is valid now");
            }
        }
        if (cc2ok) {
            digitalWrite(PIN_CC1_CS, HIGH);
            delayMicroseconds(100);
            ELECHOUSE_cc1101.setSpiPin(PIN_VSPI_SCK, PIN_VSPI_MISO, PIN_VSPI_MOSI, PIN_CC2_CS);
            ELECHOUSE_cc1101.setGDO0(PIN_CC2_GDO0);
            Serial.printf("[DUAL-TEST] CC1101-2 rssi=%ddBm gdo0=%d\n",
                          ELECHOUSE_cc1101.getRssi(), digitalRead(PIN_CC2_GDO0));
        } else {
            digitalWrite(PIN_CC1_CS, HIGH);
            delayMicroseconds(100);
            uint8_t partnum = rawReadStatus(PIN_CC2_CS, CC1101_PARTNUM);
            uint8_t version = rawReadStatus(PIN_CC2_CS, CC1101_VERSION);
            Serial.printf("[DUAL-TEST] CC1101-2 retry raw: partnum=0x%02X version=0x%02X gdo0=%d cs=%d\n",
                          partnum, version, digitalRead(PIN_CC2_GDO0), digitalRead(PIN_CC2_CS));
            if (partnum == 0x00 && (version == 0x04 || version == 0x14)) {
                cc2ok = true;
                Serial.println("[DUAL-TEST] CC1101-2 recovered — raw ID is valid now");
            }
        }
        deselectBoth();
    }
}
