/*
 * Universal IR remote (PRD §9 IR feature — "Universal Remote" extension).
 * A brand/category-driven front-end over the IRremoteESP8266 protocol senders
 * already compiled in: parametric AC control via IRac (Carrier, Gree/Union Air)
 * and well-known power/volume/input codes for TVs (Samsung, LG) and projectors
 * (Epson, Sony). Codes are best-effort defaults meant to be confirmed on real
 * hardware; "Capture your own" falls back to the existing IR-Read path.
 *
 * Single responsibility: present the menu + map a chosen command to one IR send.
 * No new send engine — every transmission goes through custom_ir / IRac / IRsend.
 */
#ifndef __UNIVERSAL_REMOTE_H__
#define __UNIVERSAL_REMOTE_H__

// Entry point from IRMenu::optionsMenu().
void universalRemoteMenu();

#endif // __UNIVERSAL_REMOTE_H__
