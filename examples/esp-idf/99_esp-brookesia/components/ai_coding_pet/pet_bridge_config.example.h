#pragma once

/*
 * PC Bridge endpoint configuration (ticket 02 MVP: hardcoded).
 * Copy this file to pet_bridge_config.h and set the IP of the machine
 * running tools/pet-bridge/bridge.js. pet_bridge_config.h is gitignored.
 * TODO(ticket 05+): mDNS discovery of the bridge on the local network.
 */

#define PET_BRIDGE_IP      "192.168.1.100"
#define PET_BRIDGE_PORT    8787
#define PET_BRIDGE_WS_PATH "/pet"
