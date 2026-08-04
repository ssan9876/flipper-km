#pragma once
#include <furi.h>
#include <furi_hal_usb.h>
#include <gui/gui.h>
#include <cli/cli.h>

#include "km_layout.h"

#define KM_PAYLOAD_MAX 1024
#define KM_KEY_DELAY_MS 8

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* input_queue;
    FuriHalUsbInterface* usb_prev;
    CliRegistry* cli_registry;
    KmLayout layout;
    bool running;
} KmApp;
