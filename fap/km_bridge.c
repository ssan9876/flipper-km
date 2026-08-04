#include <furi.h>
#include <furi_hal_usb.h>
#include <furi_hal_usb_hid.h>
#include <gui/gui.h>
#include <input/input.h>
#include <dialogs/dialogs.h>
#include <stdio.h>
#include <string.h>

#include "km_bridge_i.h"
#include "km_ble.h"
#include "km_base64.h"
#include "km_layout_file.h"

#define KM_COMMAND "kmtype"

static void km_draw_callback(Canvas* canvas, void* ctx) {
    KmApp* app = ctx;

    furi_mutex_acquire(app->state_mutex, FuriWaitForever);
    bool awaiting = app->awaiting_confirm;
    size_t len = app->payload_len;
    furi_mutex_release(app->state_mutex);

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 12, "KM Bridge");
    canvas_set_font(canvas, FontSecondary);

    if(awaiting) {
        /* Character count only -- never the payload itself. */
        char line[32];
        snprintf(line, sizeof(line), "Type %u chars?", (unsigned)len);
        canvas_draw_str(canvas, 2, 28, line);
        canvas_draw_str(canvas, 2, 40, "OK = type");
        canvas_draw_str(canvas, 2, 52, "Back = cancel");
    } else {
        const char* usb_state;
        if(!app->usb_claimed) {
            usb_state = "USB: claim FAILED";
        } else if(furi_hal_hid_is_connected()) {
            usb_state = "USB: ready";
        } else {
            usb_state = "USB: no host";
        }
        canvas_draw_str(canvas, 2, 26, usb_state);
        canvas_draw_str(canvas, 2, 38, app->ble_ready ? "BLE: waiting" : "BLE: FAILED");

        const char* name = app->layout_path[0] ? strrchr(app->layout_path, '/') : NULL;
        char line[32];
        snprintf(line, sizeof(line), "Layout: %s", name ? name + 1 : "US");
        canvas_draw_str(canvas, 2, 50, line);
        canvas_draw_str(canvas, 2, 62, "OK = change layout");
    }
}

static void km_input_callback(InputEvent* event, void* ctx) {
    KmApp* app = ctx;
    furi_message_queue_put(app->input_queue, event, FuriWaitForever);
}

static void km_pick_layout(KmApp* app) {
    DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);
    FuriString* path = furi_string_alloc_set(KM_LAYOUT_DIR);

    DialogsFileBrowserOptions options;
    dialog_file_browser_set_basic_options(&options, ".kl", NULL);
    options.base_path = KM_LAYOUT_DIR;

    if(dialog_file_browser_show(dialogs, path, path, &options)) {
        if(km_layout_file_load(&app->layout, furi_string_get_cstr(path))) {
            strncpy(app->layout_path, furi_string_get_cstr(path), sizeof(app->layout_path) - 1);
            app->layout_path[sizeof(app->layout_path) - 1] = '\0';
            km_layout_settings_save(app->layout_path);
        }
    }

    furi_string_free(path);
    furi_record_close(RECORD_DIALOGS);
}

/* NOTE: `text` is secret. Never log it. */
static void km_type_buffer(const KmLayout* layout, const uint8_t* text, size_t len) {
    for(size_t i = 0; i < len; i++) {
        uint16_t key = km_layout_lookup(layout, text[i]);
        furi_hal_hid_kb_press(key);
        furi_delay_ms(KM_KEY_DELAY_MS);
        furi_hal_hid_kb_release(key);
        furi_delay_ms(KM_KEY_DELAY_MS);
    }
    furi_hal_hid_kb_release_all();
}

static void km_clear_pending(KmApp* app) {
    furi_mutex_acquire(app->state_mutex, FuriWaitForever);
    memset(app->payload, 0, sizeof(app->payload));
    app->payload_len = 0;
    app->awaiting_confirm = false;
    furi_mutex_release(app->state_mutex);
}

/* Parse and stage one received line. Replies on every rejection so the phone
 * always learns the outcome. */
static void km_handle_line(KmApp* app, const char* line) {
    /* Already holding a payload: refuse rather than silently replacing it. */
    furi_mutex_acquire(app->state_mutex, FuriWaitForever);
    bool busy = app->awaiting_confirm;
    furi_mutex_release(app->state_mutex);
    if(busy) {
        km_ble_reply(app, "ERR busy\r\n");
        return;
    }

    if(!furi_hal_hid_is_connected()) {
        km_ble_reply(app, "ERR nohost\r\n");
        return;
    }

    size_t cmd_len = strlen(KM_COMMAND);

    if(strncmp(line, KM_COMMAND, cmd_len) != 0 || line[cmd_len] != ' ') {
        km_ble_reply(app, "ERR badcmd\r\n");
        return;
    }

    const char* b64 = line + cmd_len + 1;
    while(*b64 == ' ') b64++;
    size_t b64_len = strlen(b64);

    if(b64_len == 0) {
        km_ble_reply(app, "ERR badb64\r\n");
        return;
    }

    uint8_t decoded[KM_PAYLOAD_MAX];
    size_t decoded_len = 0;
    KmB64Result result = km_base64_decode(b64, b64_len, decoded, sizeof(decoded), &decoded_len);

    if(result == KmB64TooLong) {
        memset(decoded, 0, sizeof(decoded));
        km_ble_reply(app, "ERR toolong\r\n");
        return;
    }
    if(result != KmB64Ok) {
        memset(decoded, 0, sizeof(decoded));
        km_ble_reply(app, "ERR badb64\r\n");
        return;
    }

    int bad = km_layout_first_unmappable(&app->layout, decoded, decoded_len);
    if(bad >= 0) {
        memset(decoded, 0, sizeof(decoded));
        /* Index only -- never the offending character. */
        char reply[32];
        snprintf(reply, sizeof(reply), "ERR unmappable@%d\r\n", bad);
        km_ble_reply(app, reply);
        return;
    }

    furi_mutex_acquire(app->state_mutex, FuriWaitForever);
    memcpy(app->payload, decoded, decoded_len);
    app->payload_len = decoded_len;
    app->awaiting_confirm = true;
    app->confirm_deadline = furi_get_tick() + furi_ms_to_ticks(KM_CONFIRM_TIMEOUT_MS);
    furi_mutex_release(app->state_mutex);

    memset(decoded, 0, sizeof(decoded));
}

int32_t km_bridge_app(void* p) {
    UNUSED(p);

    KmApp* app = malloc(sizeof(KmApp));
    memset(app, 0, sizeof(KmApp));
    app->running = true;
    app->input_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->state_mutex = furi_mutex_alloc(FuriMutexTypeNormal);

    km_layout_set_default(&app->layout, hid_asciimap, sizeof(hid_asciimap) / sizeof(uint16_t));
    if(km_layout_settings_load(app->layout_path, sizeof(app->layout_path))) {
        if(!km_layout_file_load(&app->layout, app->layout_path)) {
            /* Bad or missing file: keep the US default. */
            app->layout_path[0] = '\0';
        }
    }

    /* Take over USB as an HID keyboard, remembering what to put back.
     * The switch is refused while another session holds a USB mode lock. */
    app->usb_prev = furi_hal_usb_get_config();
    if(furi_hal_usb_is_locked()) {
        furi_hal_usb_unlock();
    }
    app->usb_claimed = furi_hal_usb_set_config(&usb_hid, NULL);

    app->ble_ready = km_ble_start(app);

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, km_draw_callback, app);
    view_port_input_callback_set(app->view_port, km_input_callback, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    InputEvent event;

    while(app->running) {
        if(app->line_ready) {
            km_handle_line(app, app->ready_line);
            memset(app->ready_line, 0, sizeof(app->ready_line));
            app->line_ready = false; /* release only after consuming */
        }

        if(app->line_overflow) {
            app->line_overflow = false;
            km_ble_reply(app, "ERR toolong\r\n");
        }

        if(app->line_dropped) {
            app->line_dropped = false;
            km_ble_reply(app, "ERR busy\r\n");
        }

        furi_mutex_acquire(app->state_mutex, FuriWaitForever);
        bool awaiting = app->awaiting_confirm;
        bool expired = awaiting && furi_get_tick() > app->confirm_deadline;
        furi_mutex_release(app->state_mutex);

        if(expired) {
            km_clear_pending(app);
            km_ble_reply(app, "ERR timeout\r\n");
        }

        if(furi_message_queue_get(app->input_queue, &event, 50) == FuriStatusOk) {
            if(event.type == InputTypeShort) {
                if(awaiting) {
                    if(event.key == InputKeyOk) {
                        furi_mutex_acquire(app->state_mutex, FuriWaitForever);
                        size_t len = app->payload_len;
                        furi_mutex_release(app->state_mutex);

                        km_type_buffer(&app->layout, app->payload, len);
                        km_clear_pending(app);
                        km_ble_reply(app, "OK\r\n");
                    } else if(event.key == InputKeyBack) {
                        km_clear_pending(app);
                        km_ble_reply(app, "ERR cancelled\r\n");
                    }
                } else if(event.key == InputKeyOk) {
                    km_pick_layout(app);
                } else if(event.key == InputKeyBack) {
                    app->running = false;
                }
            }
        }

        view_port_update(app->view_port);
    }

    /* Detach BLE before anything is freed, or the stack calls into freed
     * memory from its own thread. */
    km_ble_stop(app);

    gui_remove_view_port(app->gui, app->view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(app->view_port);

    furi_message_queue_free(app->input_queue);
    furi_mutex_free(app->state_mutex);

    furi_hal_usb_set_config(app->usb_prev, NULL);

    /* Secret: never leave the payload in freed heap. */
    memset(app, 0, sizeof(KmApp));
    free(app);
    return 0;
}
