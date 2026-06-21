#pragma once

// Phone probe-request sniffer: listens for 802.11 probe requests (the frames phones
// broadcast looking for remembered networks), and lists each device's MAC + the SSID
// it is searching for, on the TFT. Channel-hops 1-13, BACK exits. Self-contained —
// owns its promiscuous callback and WiFi teardown (no karma_attack coupling). Sits
// next to Sniffer / Channel Graph in the WiFi menu.

void wifi_phone_probe();
