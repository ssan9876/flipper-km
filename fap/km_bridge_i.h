#pragma once
#include <furi.h>
#include <furi_hal_usb.h>
#include <gui/gui.h>
#include <cli/cli.h>

#include "km_layout.h"

#define KM_PAYLOAD_MAX 1024
#define KM_KEY_DELAY_MS 8
#define KM_CONFIRM_TIMEOUT_MS 60000
#define KM_FLAG_CONFIRM (1 << 0)
#define KM_FLAG_CANCEL (1 << 1)

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* input_queue;
    FuriHalUsbInterface* usb_prev;
    CliRegistry* cli_registry;
    KmLayout layout;
    bool running;

    FuriEventFlag* confirm_flags;
    FuriMutex* state_mutex;
    size_t pending_len;
    bool awaiting_confirm;
} KmApp;
