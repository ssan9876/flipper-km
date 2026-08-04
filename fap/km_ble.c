#include "km_ble.h"

#include <bt/bt_service/bt.h>
#include <profiles/serial_profile.h>
#include <string.h>

/* The BLE serial link is an RPC channel by default: on connect, the bt service
 * opens an RPC session and feeds every incoming byte to the protobuf parser.
 * A plain-text command written to it is therefore silently discarded.
 *
 * To receive raw bytes we must do BOTH of the following. Installing the
 * callback alone is not enough -- the bt service keeps consuming the data and
 * the callback never fires, which is the failure reported in
 * https://forum.flipper.net/t/trying-to-send-some-bytes-via-ble-to-an-app-on-the-flipper/15746
 *   1. ble_profile_serial_set_rpc_active(profile, false)
 *   2. ble_profile_serial_set_event_callback(profile, ...)
 */

/* Runs INSIDE the BLE core event handler, on the BLE stack's own thread, while
 * that handler holds a mutex. Three hard rules:
 *   - no large stack locals (its stack is small; overflowing it hangs the device)
 *   - never block (no waiting on mutexes, queues, or the main thread)
 *   - return promptly
 * All buffers therefore live in the heap-allocated KmApp, and handoff is a
 * flag rather than a queue. */
static uint16_t km_ble_event_callback(SerialServiceEvent event, void* context) {
    KmApp* app = context;

    if(event.event == SerialServiceEventTypeDataReceived) {
        for(uint16_t i = 0; i < event.data.size; i++) {
            char c = (char)event.data.buffer[i];

            if(c == '\r' || c == '\n') {
                if(app->line_len > 0) {
                    if(!app->line_ready) {
                        memcpy(app->ready_line, app->line, app->line_len);
                        app->ready_line[app->line_len] = '\0';
                        app->line_ready = true; /* publish only after the copy */
                    } else {
                        /* Main thread has not consumed the previous line yet. */
                        app->line_dropped = true;
                    }
                    /* Secret: do not leave it in the accumulator. */
                    memset(app->line, 0, sizeof(app->line));
                    app->line_len = 0;
                }
            } else if(app->line_len < KM_LINE_MAX - 1) {
                app->line[app->line_len++] = c;
            } else {
                /* Overlong line: drop it whole rather than truncate into a
                 * half-payload that might still decode. */
                memset(app->line, 0, sizeof(app->line));
                app->line_len = 0;
                app->line_overflow = true;
            }
        }
    } else if(event.event == SerialServiceEventTypesBleResetRequest) {
        memset(app->line, 0, sizeof(app->line));
        app->line_len = 0;
    }

    /* Remaining capacity, used by the stack for flow control. */
    return (uint16_t)(KM_LINE_MAX - app->line_len);
}

bool km_ble_start(KmApp* app) {
    app->bt = furi_record_open(RECORD_BT);

    app->ble_profile = bt_profile_start(app->bt, ble_profile_serial, NULL);
    if(app->ble_profile == NULL) {
        furi_record_close(RECORD_BT);
        app->bt = NULL;
        return false;
    }

    /* Order matters: stop RPC consuming the stream, then claim it. */
    ble_profile_serial_set_rpc_active(app->ble_profile, false);
    ble_profile_serial_set_event_callback(
        app->ble_profile, KM_LINE_MAX, km_ble_event_callback, app);

    /* bt_profile_start already brings up advertising; calling
     * furi_hal_bt_start_advertising() again here is redundant and risks
     * disturbing the stack. */
    return true;
}

void km_ble_stop(KmApp* app) {
    if(app->ble_profile != NULL) {
        /* Detach before the app's memory goes away, or the stack calls into
         * a freed context. */
        ble_profile_serial_set_event_callback(app->ble_profile, 0, NULL, NULL);
        app->ble_profile = NULL;
    }
    if(app->bt != NULL) {
        bt_profile_restore_default(app->bt);
        furi_record_close(RECORD_BT);
        app->bt = NULL;
    }
    memset(app->line, 0, sizeof(app->line));
    app->line_len = 0;
}

void km_ble_reply(KmApp* app, const char* msg) {
    if(app->ble_profile == NULL) return;
    ble_profile_serial_tx(app->ble_profile, (uint8_t*)msg, (uint16_t)strlen(msg));
}
