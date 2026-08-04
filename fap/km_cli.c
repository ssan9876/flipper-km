#include "km_cli.h"
#include "km_base64.h"

#include <furi_hal_usb_hid.h>
#include <toolbox/pipe.h>
#include <stdio.h>
#include <string.h>

#define KM_CLI_COMMAND "kmtype"

static void km_reply(PipeSide* pipe, const char* msg) {
    pipe_send(pipe, msg, strlen(msg));
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

static void km_cli_kmtype(PipeSide* pipe, FuriString* args, void* context) {
    KmApp* app = context;

    if(!furi_hal_hid_is_connected()) {
        km_reply(pipe, "ERR nohost\r\n");
        return;
    }

    uint8_t payload[KM_PAYLOAD_MAX];
    size_t payload_len = 0;

    furi_string_trim(args);
    const char* b64 = furi_string_get_cstr(args);
    size_t b64_len = furi_string_size(args);

    if(b64_len == 0) {
        km_reply(pipe, "ERR badb64\r\n");
        return;
    }

    KmB64Result decoded = km_base64_decode(b64, b64_len, payload, sizeof(payload), &payload_len);

    if(decoded == KmB64TooLong) {
        km_reply(pipe, "ERR toolong\r\n");
        goto cleanup;
    }
    if(decoded != KmB64Ok) {
        km_reply(pipe, "ERR badb64\r\n");
        goto cleanup;
    }

    /* Braces are load-bearing: the goto statements above jump forward past
     * this point, and jumping over an initialised declaration at function
     * scope draws a warning under some GCC configurations. */
    {
        int bad_index = km_layout_first_unmappable(&app->layout, payload, payload_len);
        if(bad_index >= 0) {
            /* Index only -- never the offending character. */
            char msg[32];
            snprintf(msg, sizeof(msg), "ERR unmappable@%d\r\n", bad_index);
            km_reply(pipe, msg);
            goto cleanup;
        }
    }

    furi_mutex_acquire(app->state_mutex, FuriWaitForever);
    app->pending_len = payload_len;
    app->awaiting_confirm = true;
    furi_mutex_release(app->state_mutex);

    furi_event_flag_clear(app->confirm_flags, KM_FLAG_CONFIRM | KM_FLAG_CANCEL);

    uint32_t flags = furi_event_flag_wait(
        app->confirm_flags,
        KM_FLAG_CONFIRM | KM_FLAG_CANCEL,
        FuriFlagWaitAny,
        KM_CONFIRM_TIMEOUT_MS);

    furi_mutex_acquire(app->state_mutex, FuriWaitForever);
    app->awaiting_confirm = false;
    app->pending_len = 0;
    furi_mutex_release(app->state_mutex);

    if(flags & KM_FLAG_CONFIRM) {
        km_type_buffer(&app->layout, payload, payload_len);
        km_reply(pipe, "OK\r\n");
    } else if(flags & KM_FLAG_CANCEL) {
        km_reply(pipe, "ERR cancelled\r\n");
    } else {
        km_reply(pipe, "ERR timeout\r\n");
    }

cleanup:
    memset(payload, 0, sizeof(payload));
}

void km_cli_register(KmApp* app) {
    app->cli_registry = furi_record_open(RECORD_CLI);
    cli_registry_add_command(
        app->cli_registry, KM_CLI_COMMAND, CliCommandFlagParallelSafe, km_cli_kmtype, app);
}

void km_cli_unregister(KmApp* app) {
    cli_registry_delete_command(app->cli_registry, KM_CLI_COMMAND);
    furi_record_close(RECORD_CLI);
    app->cli_registry = NULL;
}
