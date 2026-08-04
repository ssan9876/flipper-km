#pragma once
#include "km_bridge_i.h"

/** Take over the BLE serial link: start the serial profile, disable RPC
 *  routing, and install our own byte callback. Returns false if the profile
 *  could not be started. */
bool km_ble_start(KmApp* app);

/** Hand the BLE link back to the firmware's default profile. */
void km_ble_stop(KmApp* app);

/** Send a reply line back to the phone. Safe to call from the main thread. */
void km_ble_reply(KmApp* app, const char* msg);
