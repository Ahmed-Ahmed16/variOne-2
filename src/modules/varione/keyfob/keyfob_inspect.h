/*
 * VariOne Keyfob Inspect — educational capture + classify + explain for RF
 * remotes. Captures two presses of a keyfob, classifies the protocol as FIXED
 * or ROLLING (KeeLoq), and — when a manufacturer key is loaded in the keystore
 * — decrypts to show serial + counter. Ends on an explain screen in the AI
 * Debrief tone: why a naive replay fails against rolling codes, and mitigation.
 *
 * Deliberately NO brute-force: rolling codes are cryptographic and cannot be
 * brute-forced. Reuses the CC1101 RX/decode + KeeLoq stack from the RF module
 * (rf_scan.cpp / rf_utils.cpp); adds no libraries.
 */
#ifndef VARIONE_KEYFOB_INSPECT_H
#define VARIONE_KEYFOB_INSPECT_H

// Menu entry point: runs the full capture -> classify -> explain flow, then
// returns to the RF menu. Honors BACK (EscPress) at every wait.
void keyfob_inspect();

#endif // VARIONE_KEYFOB_INSPECT_H
