#include "wol.h"
#include <WiFiUdp.h>
#include "config.h"

static WiFiUDP udp;

bool wolSend(const String& mac, const IPAddress& broadcast) {
    int mb[6];
    if (sscanf(mac.c_str(), "%x:%x:%x:%x:%x:%x",
               &mb[0], &mb[1], &mb[2], &mb[3], &mb[4], &mb[5]) != 6) {
        return false;
    }

    byte packet[102];
    for (int i = 0; i < 6; i++) packet[i] = 0xFF;
    for (int i = 1; i <= 16; i++) {
        for (int j = 0; j < 6; j++) {
            packet[i * 6 + j] = (byte)mb[j];
        }
    }

    udp.beginPacket(broadcast, WOL_PORT);
    udp.write(packet, sizeof(packet));
    udp.endPacket();
    return true;
}
