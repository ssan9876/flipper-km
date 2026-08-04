#include <furi.h>
#include <furi_hal_usb.h>
#include <furi_hal_usb_hid.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdio.h>
#include <string.h>

#include "km_bridge_i.h"
#include "km_cli.h"

static void km_draw_callback(Canvas* canvas, void* ctx) {
    UNUSED(ctx);
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 12, "KM Bridge");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 28, furi_hal_hid_is_connected() ? "USB: ready" : "USB: no host");
    canvas_draw_str(canvas, 2, 40, "Waiting for phone");
}

static void km_input_callback(InputEvent* event, void* ctx) {
    KmApp* app = ctx;
    furi_message_queue_put(app->input_queue, event, FuriWaitForever);
}

int32_t km_bridge_app(void* p) {
    UNUSED(p);

    KmApp* app = malloc(sizeof(KmApp));
    app->running = true;
    app->input_queue = furi_message_queue_alloc(8, sizeof(InputEvent));

    km_layout_set_default(&app->layout, hid_asciimap, sizeof(hid_asciimap) / sizeof(uint16_t));

    /* Take over USB as an HID keyboard, remembering what to put back. */
    app->usb_prev = furi_hal_usb_get_config();
    furi_hal_usb_set_config(&usb_hid, NULL);

    km_cli_register(app);

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, km_draw_callback, app);
    view_port_input_callback_set(app->view_port, km_input_callback, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    InputEvent event;
    while(app->running) {
        if(furi_message_queue_get(app->input_queue, &event, 100) == FuriStatusOk) {
            if(event.type == InputTypeShort && event.key == InputKeyBack) {
                app->running = false;
            }
        }
        view_port_update(app->view_port);
    }

    /* MUST come first: a command left registered past app exit dereferences
     * a dangling context pointer and crashes the Flipper on next invocation. */
    km_cli_unregister(app);

    gui_remove_view_port(app->gui, app->view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(app->view_port);
    furi_message_queue_free(app->input_queue);

    furi_hal_usb_set_config(app->usb_prev, NULL);

    free(app);
    return 0;
}
