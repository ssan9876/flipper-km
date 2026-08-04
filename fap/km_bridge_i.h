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

/* NOTE: never place a KM_LINE_MAX-sized object on the BLE callback's stack.
 * That callback runs inside the BLE core event handler, on a thread whose
 * stack is sized for the BLE stack's own use; a 1.4 KB local overflows it and
 * hangs the device. All line buffers live in the heap-allocated KmApp. */

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
     * lines are handed to the main thread through ready_line + line_ready. */
    Bt* bt;
    FuriHalBleProfileBase* ble_profile;
    bool ble_ready;

    /* Single-producer (BLE thread) / single-consumer (main thread) handoff.
     * The producer only writes ready_line while line_ready is false; the
     * consumer only reads it while true. One flag, no locking, and nothing
     * that can block the BLE event handler. */
    char line[KM_LINE_MAX];
    size_t line_len;
    char ready_line[KM_LINE_MAX];
    volatile bool line_ready;
    volatile bool line_dropped;
    volatile bool line_overflow;

    /* Pending payload, owned by the main thread. Secret: zeroed on every
     * path that leaves the awaiting state. */
    FuriMutex* state_mutex;
    uint8_t payload[KM_PAYLOAD_MAX];
    size_t payload_len;
    bool awaiting_confirm;
    uint32_t confirm_deadline;

    char layout_path[128];
} KmApp;
