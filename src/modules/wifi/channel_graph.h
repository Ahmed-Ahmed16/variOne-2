#pragma once

// WiFi 2.4 GHz channel activity grapher: hops channels 1-14 in promiscuous mode,
// counts packets seen per channel each sweep, and draws them as bars scaled to the
// TFT. BACK exits. No dedicated PRD section — WiFi situational-awareness utility,
// sits next to Sniffer in the WiFi menu.

void wifi_channel_graph();
