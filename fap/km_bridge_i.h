#pragma once
#include <furi.h>
#include <furi_hal_usb.h>
#include <furi_hal_bt.h>
#include <gui/gui.h>
#include <furi_ble/profile_interface.h>

#include "km_layout.h"

#define KM_PAYLOAD_MAX 1024
#define KM_KEY_DELAY_MS 8
#define KM_CONFIRM_TIMEOUT_MS 60000

/* "kmtype " + base64 of 1024 bytes (1368) + slack */
#define KM_LINE_MAX 1400

typedef struct Bt Bt;

typedef struct {
    char text[KM_LINE_MAX];
    size_t len;
} KmLine;

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* input_queue;
    FuriHalUsbInterface* usb_prev;
    KmLayout layout;
    bool running;

    /* False when furi_hal_usb_set_config was refused because the mode switch
     * was locked. Surfaced on screen rather than failing silently. */
    bool usb_claimed;

    /* BLE serial takeover. `line` is filled by the BLE stack thread; complete
     * lines are handed to the main thread through line_queue. */
    Bt* bt;
    FuriHalBleProfileBase* ble_profile;
    FuriMessageQueue* line_queue;
    char line[KM_LINE_MAX];
    size_t line_len;
    bool line_overflow;
    bool ble_ready;

    /* Pending payload, owned by the main thread. Secret: zeroed on every
     * path that leaves the awaiting state. */
    FuriMutex* state_mutex;
    uint8_t payload[KM_PAYLOAD_MAX];
    size_t payload_len;
    bool awaiting_confirm;
    uint32_t confirm_deadline;

    char layout_path[128];
} KmApp;
