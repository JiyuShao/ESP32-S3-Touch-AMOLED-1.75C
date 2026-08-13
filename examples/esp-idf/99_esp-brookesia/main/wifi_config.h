#pragma once

/*
 * WiFi credentials for the STA link (ticket 02 MVP: hardcoded).
 * Fill in your 2.4GHz WiFi SSID and password.
 * TODO(ticket 05+): move to SoftAP provisioning portal + NVS persistence.
 *
 * The PC Bridge endpoint (IP/port/path) lives in the ai_coding_pet
 * component's pet_bridge_config.h — that config belongs to the pet app.
 */

#define WIFI_SSID          "YOUR_WIFI_SSID"
#define WIFI_PASS          "YOUR_WIFI_PASSWORD"
