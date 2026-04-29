// PN532 smoke test for VariOne. Isolates the I2C NFC reader and prints bus,
// firmware, ISO14443A UID, and optional shallow EMV APDU metadata over serial.

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_PN532.h>

#define PIN_I2C_SDA 21
#define PIN_I2C_SCL 22
#define PIN_NFC_IRQ 13
#define PIN_NFC_RST -1
#define PN532_I2C_ADDR 0x24

Adafruit_PN532 nfc(PIN_NFC_IRQ, PIN_NFC_RST);

static bool emvProbe = true;

static bool i2cPresent(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

static void printHex(const uint8_t* data, uint8_t len) {
  for (uint8_t i = 0; i < len; i++) {
    if (i) Serial.print(':');
    if (data[i] < 0x10) Serial.print('0');
    Serial.print(data[i], HEX);
  }
}

static const char* networkFromAid(const uint8_t* aid, uint8_t len) {
  if (len < 5) return "unknown";
  if (memcmp(aid, "\xA0\x00\x00\x00\x03", 5) == 0) return "Visa";
  if (memcmp(aid, "\xA0\x00\x00\x00\x04", 5) == 0) return "Mastercard";
  if (memcmp(aid, "\xA0\x00\x00\x00\x25", 5) == 0) return "Amex";
  if (memcmp(aid, "\xA0\x00\x00\x00\x65", 5) == 0) return "JCB";
  return "unknown";
}

static void maskPan(const char* pan) {
  size_t len = strlen(pan);
  if (len >= 4) Serial.printf("**** **** **** %.4s", pan + len - 4);
  else Serial.print("(short)");
}

static bool apdu(const char* label, const uint8_t* tx, uint8_t txLen, uint8_t* rx, uint8_t* rxLen) {
  *rxLen = 96;
  bool ok = nfc.inDataExchange((uint8_t*)tx, txLen, rx, rxLen);
  Serial.printf("[PN532-TEST] APDU %s transport=%s len=%u", label, ok ? "OK" : "FAIL", *rxLen);
  if (ok && *rxLen >= 2) {
    Serial.printf(" sw=%02X%02X", rx[*rxLen - 2], rx[*rxLen - 1]);
    ok = rx[*rxLen - 2] == 0x90 && rx[*rxLen - 1] == 0x00;
  }
  Serial.println();
  return ok;
}

static void shallowEmvProbe() {
  uint8_t rsp[96];
  uint8_t rspLen = sizeof(rsp);
  uint8_t ppse[] = {
    0x00, 0xA4, 0x04, 0x00, 0x0E,
    '2','P','A','Y','.','S','Y','S','.','D','D','F','0','1',
    0x00
  };
  if (!apdu("SELECT_PPSE", ppse, sizeof(ppse), rsp, &rspLen)) return;

  uint8_t aid[16] = {0};
  uint8_t aidLen = 0;
  for (uint8_t i = 0; i + 2 < rspLen - 2; i++) {
    if (rsp[i] == 0x4F) {
      uint8_t len = rsp[i + 1];
      if (len >= 5 && i + 2 + len <= rspLen - 2) {
        aidLen = min((int)len, 16);
        memcpy(aid, rsp + i + 2, aidLen);
        break;
      }
    }
  }
  if (!aidLen) {
    Serial.println("[PN532-TEST] EMV: PPSE OK but no AID tag");
    return;
  }

  Serial.print("[PN532-TEST] EMV AID=");
  printHex(aid, aidLen);
  Serial.printf(" network=%s\n", networkFromAid(aid, aidLen));

  uint8_t sel[24] = {0x00, 0xA4, 0x04, 0x00, aidLen};
  memcpy(sel + 5, aid, aidLen);
  sel[5 + aidLen] = 0x00;
  rspLen = sizeof(rsp);
  if (!apdu("SELECT_AID", sel, 6 + aidLen, rsp, &rspLen)) return;

  uint8_t gpo[] = {0x80, 0xA8, 0x00, 0x00, 0x02, 0x83, 0x00, 0x00};
  rspLen = sizeof(rsp);
  apdu("GPO_EMPTY_PDOL", gpo, sizeof(gpo), rsp, &rspLen);

  for (uint8_t sfi = 1; sfi <= 6; sfi++) {
    for (uint8_t rec = 1; rec <= 3; rec++) {
      uint8_t readRecord[] = {0x00, 0xB2, rec, (uint8_t)((sfi << 3) | 0x04), 0x00};
      rspLen = sizeof(rsp);
      if (!apdu("READ_RECORD", readRecord, sizeof(readRecord), rsp, &rspLen)) continue;
      for (uint8_t i = 0; i + 2 < rspLen - 2; i++) {
        if (rsp[i] == 0x5A) {
          uint8_t len = rsp[i + 1];
          char pan[24] = {0};
          uint8_t pj = 0;
          for (uint8_t b = 0; b < len && i + 2 + b < rspLen - 2 && pj + 1 < sizeof(pan); b++) {
            uint8_t byteVal = rsp[i + 2 + b];
            uint8_t hi = byteVal >> 4;
            uint8_t lo = byteVal & 0x0F;
            if (hi <= 9) pan[pj++] = '0' + hi;
            if (lo <= 9 && pj + 1 < sizeof(pan)) pan[pj++] = '0' + lo;
          }
          Serial.print("[PN532-TEST] EMV PAN(masked)=");
          maskPan(pan);
          Serial.println();
          return;
        }
      }
    }
  }
}

static void pollCard() {
  if (!i2cPresent(PN532_I2C_ADDR)) {
    Serial.println("[PN532-TEST] PN532 absent at I2C 0x24");
    return;
  }

  uint8_t uid[7] = {0};
  uint8_t uidLen = 0;
  bool got = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 120);
  if (!got) {
    Serial.println("[PN532-TEST] no card");
    return;
  }

  Serial.print("[PN532-TEST] card UID=");
  printHex(uid, uidLen);
  Serial.printf(" uidLen=%u\n", uidLen);
  if (emvProbe && nfc.inListPassiveTarget()) shallowEmvProbe();
}

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println("VariOne PN532 smoke test boot");
  Serial.printf("[PN532-TEST] wiring: SDA=%d SCL=%d IRQ=%d addr=0x%02X\n",
                PIN_I2C_SDA, PIN_I2C_SCL, PIN_NFC_IRQ, PN532_I2C_ADDR);

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setTimeOut(180);
  Serial.printf("[PN532-TEST] I2C 0x24 present=%s\n", i2cPresent(PN532_I2C_ADDR) ? "yes" : "no");
  nfc.begin();
  uint32_t version = nfc.getFirmwareVersion();
  if (!version) {
    Serial.println("[PN532-TEST] getFirmwareVersion=FAIL");
  } else {
    Serial.printf("[PN532-TEST] firmware IC=0x%02X ver=%u.%u support=0x%02X\n",
                  (uint8_t)(version >> 24), (uint8_t)(version >> 16),
                  (uint8_t)(version >> 8), (uint8_t)version);
    nfc.SAMConfig();
  }
  Serial.println("[PN532-TEST] commands: e toggle EMV, p poll now");
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'e') {
      emvProbe = !emvProbe;
      Serial.printf("[PN532-TEST] EMV probe=%s\n", emvProbe ? "on" : "off");
    } else if (c == 'p') {
      pollCard();
    }
  }

  static uint32_t last = 0;
  if (millis() - last >= 1500) {
    last = millis();
    pollCard();
  }
}
