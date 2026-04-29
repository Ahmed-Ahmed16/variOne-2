// CC1101 smoke test for VariOne. Implements the sensor-first bring-up rule by
// isolating the CC1101 on VSPI and printing chip identity, OOK config, RSSI,
// GDO0 edge activity, and common-frequency scan results over serial.

#include <Arduino.h>
#include <SPI.h>
#include <ELECHOUSE_CC1101_SRC_DRV.h>

#define PIN_CC_CS      15
#define PIN_SD_CS      5
#define PIN_VSPI_SCK   18
#define PIN_VSPI_MISO  19
#define PIN_VSPI_MOSI  23
#define PIN_CC_GDO0    4

static const float kFreqsMHz[] = {
  300.00f, 303.875f, 315.00f, 330.00f, 345.00f,
  390.00f, 433.92f, 868.35f, 915.00f
};
static const size_t kFreqCount = sizeof(kFreqsMHz) / sizeof(kFreqsMHz[0]);

static const float kRxBandwidths[] = {58.0f, 68.0f, 81.0f, 102.0f, 135.0f, 203.0f, 325.0f, 406.0f, 541.0f, 650.0f, 812.0f};
static const size_t kBwCount = sizeof(kRxBandwidths) / sizeof(kRxBandwidths[0]);

static float activeFreqMHz = 433.92f;
static size_t activeFreqIndex = 6;
static size_t activeBwIndex = 6; // 325 kHz, matching HIZMOS OOK default.
static bool ccReady = false;

static const uint16_t kMaxPulses = 512;
static uint16_t capturedUs[kMaxPulses];
static uint8_t capturedLevel[kMaxPulses];
static uint16_t capturedCount = 0;
static uint8_t capturedStartLevel = 0;
static int capturedRssi = -127;
static float capturedFreqMHz = 0.0f;
static float capturedBwKHz = 0.0f;
static bool captureValid = false;

static uint8_t rawReadStatus(uint8_t addr) {
  SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_CC_CS, LOW);
  delayMicroseconds(10);
  SPI.transfer(addr | 0xC0);
  uint8_t val = SPI.transfer(0x00);
  digitalWrite(PIN_CC_CS, HIGH);
  SPI.endTransaction();
  return val;
}

static void printHelp() {
  Serial.println();
  Serial.println("[CC1101-TEST] commands:");
  Serial.println("  h  help");
  Serial.println("  i  chip/status registers");
  Serial.println("  o  reapply HIZMOS-style OOK RX config");
  Serial.println("  f  next frequency preset");
  Serial.println("  b  next RX bandwidth");
  Serial.println("  s  scan common frequencies by GDO0 edge count");
  Serial.println("  d  auto-detect frequency, then lock winner");
  Serial.println("  u  auto-tune RX bandwidth on current frequency");
  Serial.println("  c  wait for remote press, then capture raw OOK timings");
  Serial.println("  r  replay last captured raw OOK timings");
  Serial.println("  p  print last capture summary");
  Serial.println();
}

static int countEdges(uint32_t windowMs) {
  uint8_t last = digitalRead(PIN_CC_GDO0);
  int edges = 0;
  uint32_t start = millis();
  while (millis() - start < windowMs) {
    uint8_t now = digitalRead(PIN_CC_GDO0);
    if (now != last) {
      edges++;
      last = now;
    }
    delayMicroseconds(150);
  }
  return edges;
}

static int averageRssi(uint8_t samples = 8) {
  long sum = 0;
  for (uint8_t i = 0; i < samples; i++) {
    sum += ELECHOUSE_cc1101.getRssi();
    delay(20);
  }
  return (int)(sum / samples);
}

static void printChipStatus(const char* tag) {
  uint8_t partnum = rawReadStatus(CC1101_PARTNUM);
  uint8_t version = rawReadStatus(CC1101_VERSION);
  uint8_t marc = rawReadStatus(CC1101_MARCSTATE);
  uint8_t pkt = rawReadStatus(CC1101_PKTSTATUS);
  Serial.printf("[CC1101-TEST] %s partnum=0x%02X version=0x%02X marc=0x%02X pkt=0x%02X gdo0=%u\n",
                tag, partnum, version, marc, pkt, digitalRead(PIN_CC_GDO0));
  Serial.println("[CC1101-TEST] expected: partnum=0x00 version=0x04 or 0x14");
}

static void applyOokRx() {
  if (!ccReady) return;

  // HIZMOS-inspired remote-control baseline, corrected for SmartRC driver:
  // setModulation(2) is ASK/OOK in this driver family.
  ELECHOUSE_cc1101.setMHZ(activeFreqMHz);
  ELECHOUSE_cc1101.setCCMode(0);       // async serial raw OOK on GDO0
  ELECHOUSE_cc1101.setModulation(2);   // ASK/OOK
  ELECHOUSE_cc1101.setDRate(3.79372f);
  ELECHOUSE_cc1101.setDeviation(0.0f);
  ELECHOUSE_cc1101.setRxBW(kRxBandwidths[activeBwIndex]);
  ELECHOUSE_cc1101.setSyncMode(0);     // no preamble/sync filtering
  ELECHOUSE_cc1101.setCrc(false);
  ELECHOUSE_cc1101.setPA(10);
  pinMode(PIN_CC_GDO0, INPUT);
  ELECHOUSE_cc1101.SetRx();
  delay(120);

  Serial.printf("[CC1101-TEST] OOK RX freq=%.3fMHz bw=%.0fkHz dr=3.79372 gdo0=%d\n",
                activeFreqMHz, kRxBandwidths[activeBwIndex], PIN_CC_GDO0);
}

static int scanScore(int edges, int rssi) {
  int signal = constrain(rssi + 95, 0, 80);
  return edges + (signal * 20);
}

static void scanCommonFreqs() {
  if (!ccReady) return;

  Serial.println("[CC1101-TEST] scan start: hold remote button near antenna");
  int bestIdx = 0;
  int bestEdges = -1;

  for (size_t i = 0; i < kFreqCount; i++) {
    activeFreqMHz = kFreqsMHz[i];
    applyOokRx();
    delay(80);
    int edges = countEdges(900);
    int rssi = averageRssi(4);
    Serial.printf("[CC1101-TEST] scan %.3fMHz edges=%d avg_rssi=%ddBm\n",
                  activeFreqMHz, edges, rssi);
    if (edges > bestEdges) {
      bestEdges = edges;
      bestIdx = (int)i;
    }
  }

  activeFreqIndex = (size_t)bestIdx;
  activeFreqMHz = kFreqsMHz[activeFreqIndex];
  applyOokRx();
  Serial.printf("[CC1101-TEST] scan winner=%.3fMHz edges=%d\n", activeFreqMHz, bestEdges);
}

static void autoDetectFrequency() {
  if (!ccReady) return;

  const size_t savedBw = activeBwIndex;
  activeBwIndex = 6; // 325 kHz proved best for 433 OOK remotes in lab test.
  Serial.println("[CC1101-TEST] auto-detect: HOLD remote button until winner prints");

  int bestIdx = (int)activeFreqIndex;
  int bestScore = -1;
  int bestEdges = 0;
  int bestRssi = -127;

  for (size_t i = 0; i < kFreqCount; i++) {
    activeFreqMHz = kFreqsMHz[i];
    applyOokRx();
    delay(60);
    int edges = countEdges(800);
    int rssi = averageRssi(4);
    int score = scanScore(edges, rssi);
    Serial.printf("[CC1101-TEST] detect %.3fMHz edges=%d rssi=%ddBm score=%d\n",
                  activeFreqMHz, edges, rssi, score);
    if (score > bestScore) {
      bestScore = score;
      bestIdx = (int)i;
      bestEdges = edges;
      bestRssi = rssi;
    }
  }

  activeFreqIndex = (size_t)bestIdx;
  activeFreqMHz = kFreqsMHz[activeFreqIndex];
  if (bestEdges < 25 && bestRssi < -80) {
    activeBwIndex = savedBw;
    Serial.println("[CC1101-TEST] detect weak/no signal; keeping previous settings");
  }
  applyOokRx();
  Serial.printf("[CC1101-TEST] detect winner=%.3fMHz bw=%.0fkHz edges=%d rssi=%ddBm score=%d\n",
                activeFreqMHz, kRxBandwidths[activeBwIndex], bestEdges, bestRssi, bestScore);
}

static void autoTuneBandwidth() {
  if (!ccReady) return;

  Serial.println("[CC1101-TEST] auto-tune BW: HOLD remote button on current frequency");
  size_t bestBw = activeBwIndex;
  int bestScore = -1;
  int bestEdges = 0;
  int bestRssi = -127;

  for (size_t i = 0; i < kBwCount; i++) {
    activeBwIndex = i;
    applyOokRx();
    delay(60);
    int edges = countEdges(700);
    int rssi = averageRssi(3);
    int score = scanScore(edges, rssi);
    Serial.printf("[CC1101-TEST] tune bw=%.0fkHz edges=%d rssi=%ddBm score=%d\n",
                  kRxBandwidths[i], edges, rssi, score);
    if (score > bestScore) {
      bestScore = score;
      bestBw = i;
      bestEdges = edges;
      bestRssi = rssi;
    }
  }

  activeBwIndex = bestBw;
  applyOokRx();
  Serial.printf("[CC1101-TEST] tune winner bw=%.0fkHz edges=%d rssi=%ddBm score=%d\n",
                kRxBandwidths[activeBwIndex], bestEdges, bestRssi, bestScore);
}

static void printCaptureSummary() {
  if (!captureValid || capturedCount == 0) {
    Serial.println("[CC1101-TEST] capture: none");
    return;
  }

  Serial.printf("[CC1101-TEST] capture summary freq=%.3fMHz bw=%.0fkHz rssi=%ddBm start=%u pulses=%u\n",
                capturedFreqMHz, capturedBwKHz, capturedRssi,
                capturedStartLevel, capturedCount);
  Serial.print("[CC1101-TEST] levels=");
  for (uint16_t i = 0; i < min<uint16_t>(capturedCount, 48); i++) {
    if (i) Serial.print(',');
    Serial.print(capturedLevel[i]);
  }
  if (capturedCount > 48) Serial.print(",...");
  Serial.println();

  Serial.print("[CC1101-TEST] us=");
  for (uint16_t i = 0; i < min<uint16_t>(capturedCount, 48); i++) {
    if (i) Serial.print(',');
    Serial.print(capturedUs[i]);
  }
  if (capturedCount > 48) Serial.print(",...");
  Serial.println();
}

static void captureRawOokNow(uint32_t windowUs) {
  uint8_t last = digitalRead(PIN_CC_GDO0);
  capturedStartLevel = last ? 1 : 0;
  capturedCount = 0;
  memset(capturedUs, 0, sizeof(capturedUs));
  memset(capturedLevel, 0, sizeof(capturedLevel));

  uint32_t edgeStart = micros();
  uint32_t captureStart = edgeStart;
  uint32_t lastEdgeAt = edgeStart;

  while ((uint32_t)(micros() - captureStart) < windowUs && capturedCount < kMaxPulses) {
    uint8_t now = digitalRead(PIN_CC_GDO0);
    if (now != last) {
      uint32_t dur = micros() - edgeStart;
      capturedUs[capturedCount] = (uint16_t)min<uint32_t>(dur, 65535);
      capturedLevel[capturedCount] = last ? 1 : 0;
      capturedCount++;
      last = now;
      edgeStart = micros();
      lastEdgeAt = edgeStart;
    } else if (capturedCount >= 24 && (uint32_t)(micros() - lastEdgeAt) > 30000) {
      break;
    }
    delayMicroseconds(40);
  }

  capturedRssi = ELECHOUSE_cc1101.getRssi();
  capturedFreqMHz = activeFreqMHz;
  capturedBwKHz = kRxBandwidths[activeBwIndex];
  captureValid = capturedCount >= 24;

  Serial.printf("[CC1101-TEST] capture %s pulses=%u rssi=%ddBm freq=%.3fMHz bw=%.0fkHz\n",
                captureValid ? "OK" : "WEAK", capturedCount, capturedRssi,
                capturedFreqMHz, capturedBwKHz);
  printCaptureSummary();
}

static void waitAndCapture() {
  if (!ccReady) return;

  applyOokRx();
  int floor = averageRssi(8);
  uint8_t last = digitalRead(PIN_CC_GDO0);
  uint32_t start = millis();
  uint32_t lastRssiCheck = 0;
  Serial.printf("[CC1101-TEST] capture armed floor=%ddBm; press remote within 8s\n", floor);

  while (millis() - start < 8000) {
    uint8_t now = digitalRead(PIN_CC_GDO0);
    if (now != last) {
      Serial.println("[CC1101-TEST] trigger=edge");
      captureRawOokNow(320000);
      return;
    }
    last = now;

    if (millis() - lastRssiCheck >= 25) {
      lastRssiCheck = millis();
      int rssi = ELECHOUSE_cc1101.getRssi();
      if (rssi > floor + 8 && rssi > -82) {
        Serial.printf("[CC1101-TEST] trigger=rssi rssi=%ddBm floor=%ddBm\n", rssi, floor);
        captureRawOokNow(320000);
        return;
      }
    }
    delayMicroseconds(120);
  }

  Serial.println("[CC1101-TEST] capture timeout: no edge/RSSI trigger");
}

static void delayMicrosChunked(uint16_t us) {
  while (us > 12000) {
    delayMicroseconds(12000);
    us -= 12000;
  }
  if (us) delayMicroseconds(us);
}

static void replayCapture() {
  if (!ccReady || !captureValid || capturedCount == 0) {
    Serial.println("[CC1101-TEST] replay blocked: capture first with 'c'");
    return;
  }

  activeFreqMHz = capturedFreqMHz;
  for (size_t i = 0; i < kBwCount; i++) {
    if ((int)kRxBandwidths[i] == (int)capturedBwKHz) activeBwIndex = i;
  }

  int replayStart = 0;
  for (uint16_t i = 0; i < capturedCount; i++) {
    if (capturedUs[i] >= 5000) {
      replayStart = i + 1;
      break;
    }
  }
  int replayLen = capturedCount - replayStart;
  if (replayLen < 20) {
    replayStart = 0;
    replayLen = capturedCount;
  }

  Serial.printf("[CC1101-TEST] replay start freq=%.3fMHz pulses=%d offset=%d repeats=6\n",
                activeFreqMHz, replayLen, replayStart);
  ELECHOUSE_cc1101.setMHZ(activeFreqMHz);
  ELECHOUSE_cc1101.setCCMode(0);
  ELECHOUSE_cc1101.setModulation(2);
  ELECHOUSE_cc1101.setPA(10);
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_IOCFG0, 0x2E);
  ELECHOUSE_cc1101.SetTx();
  pinMode(PIN_CC_GDO0, OUTPUT);
  digitalWrite(PIN_CC_GDO0, LOW);
  delayMicroseconds(500);

  for (uint8_t repeat = 0; repeat < 6; repeat++) {
    for (int i = 0; i < replayLen; i++) {
      int idx = replayStart + i;
      digitalWrite(PIN_CC_GDO0, capturedLevel[idx] ? HIGH : LOW);
      delayMicrosChunked(capturedUs[idx]);
    }
    digitalWrite(PIN_CC_GDO0, LOW);
    delay(12);
  }

  ELECHOUSE_cc1101.SpiWriteReg(CC1101_IOCFG0, 0x0D);
  pinMode(PIN_CC_GDO0, INPUT);
  applyOokRx();
  Serial.println("[CC1101-TEST] replay done");
}

static void initCc1101() {
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);
  pinMode(PIN_CC_CS, OUTPUT);
  digitalWrite(PIN_CC_CS, HIGH);
  pinMode(PIN_CC_GDO0, INPUT);

  SPI.end();
  SPI.begin(PIN_VSPI_SCK, PIN_VSPI_MISO, PIN_VSPI_MOSI, PIN_CC_CS);
  delay(100);

  printChipStatus("raw-before-driver");
  uint8_t partnum = rawReadStatus(CC1101_PARTNUM);
  uint8_t version = rawReadStatus(CC1101_VERSION);
  if (partnum != 0x00 || (version != 0x04 && version != 0x14)) {
    Serial.println("[CC1101-TEST] FAIL: chip not detected");
    Serial.println("[CC1101-TEST] check: VCC=3V3, GND, CS=15, SCK=18, MISO=19, MOSI=23, GDO0=4");
    Serial.println("[CC1101-TEST] if SD attached: ensure SD CS=5 HIGH and SD MISO is tri-stated");
    ccReady = false;
    return;
  }

  ELECHOUSE_cc1101.setSpiPin(PIN_VSPI_SCK, PIN_VSPI_MISO, PIN_VSPI_MOSI, PIN_CC_CS);
  ELECHOUSE_cc1101.setGDO0(PIN_CC_GDO0);
  ELECHOUSE_cc1101.Init();
  ccReady = true;
  applyOokRx();
  printChipStatus("after-init");
}

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println("VariOne CC1101 HIZMOS-style smoke test boot");
  Serial.printf("[CC1101-TEST] wiring: CS=%d GDO0=%d SCK=%d MISO=%d MOSI=%d SD_CS=%d\n",
                PIN_CC_CS, PIN_CC_GDO0, PIN_VSPI_SCK, PIN_VSPI_MISO, PIN_VSPI_MOSI, PIN_SD_CS);
  initCc1101();
  printHelp();
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'h') printHelp();
    else if (c == 'i') printChipStatus("manual");
    else if (c == 'o') applyOokRx();
    else if (c == 'f') {
      activeFreqIndex = (activeFreqIndex + 1) % kFreqCount;
      activeFreqMHz = kFreqsMHz[activeFreqIndex];
      applyOokRx();
    } else if (c == 'b') {
      activeBwIndex = (activeBwIndex + 1) % kBwCount;
      applyOokRx();
    } else if (c == 's') {
      scanCommonFreqs();
    } else if (c == 'd') {
      autoDetectFrequency();
    } else if (c == 'u') {
      autoTuneBandwidth();
    } else if (c == 'c') {
      waitAndCapture();
    } else if (c == 'r') {
      replayCapture();
    } else if (c == 'p') {
      printCaptureSummary();
    }
  }

  static uint32_t lastReport = 0;
  if (millis() - lastReport >= 2000) {
    lastReport = millis();
    if (!ccReady) {
      printChipStatus("not-ready");
      return;
    }
    int edges = countEdges(1000);
    int rssi = averageRssi();
    uint8_t marc = rawReadStatus(CC1101_MARCSTATE);
    uint8_t pkt = rawReadStatus(CC1101_PKTSTATUS);
    Serial.printf("[CC1101-TEST] live freq=%.3fMHz bw=%.0fkHz edges_1s=%d avg_rssi=%ddBm gdo0=%u marc=0x%02X pkt=0x%02X\n",
                  activeFreqMHz, kRxBandwidths[activeBwIndex], edges, rssi,
                  digitalRead(PIN_CC_GDO0), marc, pkt);
  }
}
