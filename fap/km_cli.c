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

    uint8_t payload[KM_PAYLOAD_MAX];
    size_t payload_len = 0;

    furi_string_trim(args);
    const char* b64 = furi_string_get_cstr(args);
    size_t b64_len = furi_string_size(args);

    KmB64Result decoded = km_base64_decode(b64, b64_len, payload, sizeof(payload), &payload_len);

    if(decoded == KmB64TooLong) {
        km_reply(pipe, "ERR toolong\r\n");
        goto cleanup;
    }
    if(decoded != KmB64Ok) {
        km_reply(pipe, "ERR badb64\r\n");
        goto cleanup;
    }

    km_type_buffer(&app->layout, payload, payload_len);
    km_reply(pipe, "OK\r\n");

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
