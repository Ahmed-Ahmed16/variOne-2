
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <typeinfo>

extern AsyncWebServer *server; // used to check if the webserver is running
typedef void (*SharedCaptiveHandler)(void *ctx, AsyncWebServerRequest *request);

// function defaults
String humanReadableSize(uint64_t bytes);
String listFiles(FS &fs, String folder);
String readLineFromFile(File myFile);

void loopOptionsWebUi();

void serveWebUIFile(AsyncWebServerRequest *request, String filename, const char *contentType);
void serveWebUIFile(
    AsyncWebServerRequest *request, String filename, const char *contentType, bool gzip,
    const uint8_t *originaFile, uint32_t originalFileSize
);
void configureWebServer();
void startWebUi(bool mode_ap = false);
void stopWebUi();
void cleanlyStopWebUiForWiFiFeature();

// Single shared port-80 server, created + begun once and never rebound.
void ensureWebServer();
// Temporarily route captive/root requests to a feature such as VariPortal while
// keeping the single shared port-80 listener alive.
void webPortalBegin(void *ctx, SharedCaptiveHandler handler);
void webPortalEnd(void *ctx);
bool webPortalActive();
// Serve the debrief report from the shared server at /debrief (and on captive
// probes) until webDebriefEnd().
void webDebriefBegin(const String &html);
void webDebriefEnd();

// Arm/disarm the AI Setup provisioning form on the shared server (plan Part C).
void webAiSetupBegin();
void webAiSetupEnd();
