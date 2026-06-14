/*
 * VariOne paged text viewer (PRD §UI helpers).
 * Reusable on-device multi-page reader for long explanations (keyfob "why",
 * future on-screen debrief). Thin wrapper over core ScrollableTextArea: word-
 * wraps the body to TFT width, paginates to height, and scrolls with the same
 * globals the long-press nav drives (Prev/Up = up, Next/Down = down). Exits on
 * SelPress or EscPress so OK-short or BACK both leave the screen.
 */
#pragma once
#include <Arduino.h>

// Block, showing `body` paged under `title`, until OK (Sel) or BACK (Esc).
void showPagedText(const String &title, const String &body);
