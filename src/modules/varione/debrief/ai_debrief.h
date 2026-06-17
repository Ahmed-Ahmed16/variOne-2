/*
 * VariOne AI Debrief — Gemini narrative generator. Implements the AI half of the
 * debrief (plan Part B). Given aggregate attack facts, builds a per-attack prompt,
 * POSTs it to Google's generativelanguage `generateContent` REST endpoint over
 * HTTPS (proven WiFiClientSecure + setInsecure pattern from gps/wigle.cpp), and
 * returns the model's narrative as plain text.
 *
 * SECURITY: only AGGREGATE facts are sent (type, target SSID/BSSID, channel,
 * frame/client/credential COUNTS, duration). NEVER credentials, captures or PANs.
 *
 * The caller (runDebrief) is responsible for STA connectivity: this module
 * assumes WiFi is already connected when aiGenerateDebrief() is called, and does
 * not raise/tear down the radio (the debrief is two-phase: STA → AP).
 */
#ifndef VARIONE_AI_DEBRIEF_H
#define VARIONE_AI_DEBRIEF_H

#include "debrief.h" // DebriefFacts, DebriefType
#include <Arduino.h>

// Generate an AI narrative for the given attack facts. Returns plain text on
// success (caller escapes before embedding in HTML), or "" on ANY failure —
// missing key, no connectivity, HTTP error, parse error — so the caller can fall
// back to the canned/deterministic report. Requires STA already connected.
String aiGenerateDebrief(const DebriefFacts &f);

// Serial smoke test (Verify #2): connect to a known WiFi net, hit Gemini with a
// fixed prompt, print the parsed reply to Serial, then disconnect. Returns true
// on a parsed 200. Safe to call from a serial command or a hidden menu entry.
bool aiDebriefSmokeTest();

#endif // VARIONE_AI_DEBRIEF_H
