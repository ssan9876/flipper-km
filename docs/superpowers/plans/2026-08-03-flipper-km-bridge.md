# Flipper KM Bridge Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Paste text on a phone, have a Flipper Zero type it into the focused field on a USB-connected PC — so passwords from a phone password manager can reach a computer without retyping.

**Architecture:** A web page on the phone connects to the Flipper's BLE serial service, which by default is wired to the Flipper's CLI shell. An external Flipper app (`km_bridge.fap`) registers a `kmtype` CLI command, puts USB into HID-keyboard mode, and types the base64-decoded payload after the user physically presses OK on the Flipper. No server, no accounts, no storage of the payload.

**Tech Stack:** C against the Flipper SDK (built with `ufbt`), clang for host-side unit tests, plain HTML/JS with Web Bluetooth for the phone, GitHub Pages for hosting.

**Spec:** `docs/superpowers/specs/2026-08-03-flipper-km-bridge-design.md`

## Global Constraints

- **Target firmware:** official/stock. Verified against **ufbt release channel, firmware 1.4.3, API version 87.1**. (The plan was originally drafted against `dev`, which numbers its API 88.2; every symbol below was re-checked against 87.1 and all signatures are identical.) All symbols used are confirmed present in `targets/f7/api_symbols.csv`.
- **The payload must never be passed to `FURI_LOG_*`.** Flipper logs are emitted over the serial console in plaintext. This applies to every task.
- **The payload must never be written to the SD card.**
- **Payload cap: 1024 bytes of decoded text** (base64 line may reach ~1368 bytes).
- **Confirm timeout: 60 seconds.**
- **ASCII only.** Unmappable characters abort the entire string; `HID_KEYBOARD_NONE` (value `0`) marks unmappable.
- **Zero every payload buffer with `memset` on every exit path** — success, abort, timeout, disconnect, app exit.
- **`cli_registry_delete_command` must be called before app teardown.** Leaving the command registered after the app exits causes a null pointer dereference and crashes the Flipper.
- The web page must **never** send `start_rpc_session` — that would switch the link out of shell mode into protobuf RPC mode.

## Verified API reference

Confirmed against `flipperdevices/flipperzero-firmware@dev`. Use these exact spellings.

```c
// lib/toolbox/cli/cli_registry.h  (reachable via #include <cli/cli.h>)
void cli_registry_add_command(
    CliRegistry* registry, const char* name, CliCommandFlag flags,
    CliCommandExecuteCallback callback, void* context);
void cli_registry_delete_command(CliRegistry* registry, const char* name);

// lib/toolbox/cli/cli_command.h
typedef void (*CliCommandExecuteCallback)(PipeSide* pipe, FuriString* args, void* context);
CliCommandFlagParallelSafe = (1 << 0)

// applications/services/cli/cli.h
#define RECORD_CLI "cli"     // furi_record_open(RECORD_CLI) returns CliRegistry*

// pipes
size_t pipe_send(PipeSide* pipe, const void* data, size_t length);

// USB / HID
FuriHalUsbInterface* furi_hal_usb_get_config(void);
bool furi_hal_usb_set_config(FuriHalUsbInterface* iface, void* ctx);
extern FuriHalUsbInterface usb_hid;
bool furi_hal_hid_is_connected(void);
bool furi_hal_hid_kb_press(uint16_t button);
bool furi_hal_hid_kb_release(uint16_t button);
bool furi_hal_hid_kb_release_all(void);

// targets/furi_hal_include/furi_hal_usb_hid.h
static const uint16_t hid_asciimap[];   // US ASCII -> keycode, HID_KEYBOARD_NONE where unmappable
KEY_MOD_LEFT_SHIFT = (1 << 9)
```

**Bad USB `.kl` layout file format** (confirmed in `applications/main/bad_usb/helpers/ducky_script.c`): a raw `uint16_t[128]`, exactly **256 bytes**, read in a single `storage_file_read`. A read returning anything other than 256 is a rejected file. Both the Flipper (ARM) and the host (x86) are little-endian, so a straight `memcpy` is correct.

**BLE identifiers:**

| Purpose | UUID |
|---|---|
| Serial service | `8fe5b3d5-2e7f-4a98-2a48-7acc60fe0000` |
| TX (phone writes) | `19ed82ae-ed21-4c9d-4145-228e62fe0000` |
| RX (phone notifies) | `19ed82ae-ed21-4c9d-4145-228e61fe0000` |

---

### Task 1: BLE reachability spike

**No code.** This retires the project's one unproven assumption before anything depends on it. If it fails, stop and revisit the spec — the fallback is a sideloaded native app or the paid WebBLE browser.

**Files:**
- Create: `docs/spike-01-ble-reachability.md`

- [ ] **Step 1: Install Bluefy on the iPhone**

Install "Bluefy – Web BLE Browser" from the App Store (free).

- [ ] **Step 2: Enable Bluetooth on the Flipper**

On the Flipper: `Settings` → `Bluetooth` → `ON`. Leave it on the desktop screen (no app running).

- [ ] **Step 3: Connect from Bluefy using a scratch page**

In Bluefy, open `https://googlechrome.github.io/samples/web-bluetooth/device-info.html` (any Web Bluetooth sample works). Tap the button to request a device and select the Flipper.

Expected: iOS shows a pairing prompt; the Flipper displays a six-digit passkey; entering it completes bonding.

- [ ] **Step 4: Record the result**

Write `docs/spike-01-ble-reachability.md` documenting: whether Bluefy could see the Flipper, whether pairing completed, the exact passkey flow, and any error text. Record the answer plainly, including if it failed.

- [ ] **Step 5: Commit**

```bash
git add docs/spike-01-ble-reachability.md
git commit -m "docs: record BLE reachability spike result"
```

**STOP.** If pairing failed, do not proceed to Task 2 — report back for a spec revision.

---

### Task 2: Host test harness and base64 decoder

**Files:**
- Create: `fap/km_base64.h`, `fap/km_base64.c`
- Create: `tests/minunit.h`, `tests/test_base64.c`, `tests/run.sh`

**Interfaces:**
- Consumes: nothing
- Produces: `KmB64Result km_base64_decode(const char* in, size_t in_len, uint8_t* out, size_t out_cap, size_t* out_len)` and `typedef enum { KmB64Ok = 0, KmB64BadChar, KmB64BadLength, KmB64TooLong } KmB64Result`

- [ ] **Step 1: Install a C compiler**

```bash
winget install --id zig.zig -e --accept-package-agreements --accept-source-agreements
```

Open a new shell afterward so `PATH` is refreshed, then verify:

```bash
zig version
```

**Do not use plain `clang` on Windows.** It was tried first and does not work standalone: LLVM's Windows build targets MSVC and resolves neither `stdio.h` nor the C runtime without a separate Windows SDK / MSVC Build Tools installation (several GB). `zig cc` is a clang frontend that ships its own libc headers, so it needs nothing else.

`tests/run.sh` auto-detects a compiler — it prefers `zig`, falls back to the winget install path when `zig` is not yet on `PATH`, then tries `cc`/`gcc`/`clang`. Override with `KM_CC` if you want a specific one.

- [ ] **Step 2: Create the test harness**

Create `tests/minunit.h`:

```c
#pragma once
#include <stdio.h>
#include <string.h>

static int km_tests_run = 0;
static int km_tests_failed = 0;

#define CHECK(cond, msg)                                             \
    do {                                                             \
        if(!(cond)) {                                                \
            printf("  FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
            km_tests_failed++;                                       \
            return;                                                  \
        }                                                            \
    } while(0)

#define RUN(test)                     \
    do {                              \
        printf("RUN  %s\n", #test);   \
        km_tests_run++;               \
        int before = km_tests_failed; \
        test();                       \
        if(before == km_tests_failed) printf("  ok\n"); \
    } while(0)

#define REPORT()                                                          \
    do {                                                                  \
        printf("\n%d run, %d failed\n", km_tests_run, km_tests_failed);   \
        return km_tests_failed == 0 ? 0 : 1;                              \
    } while(0)
```

Create `tests/run.sh`:

```bash
#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"
mkdir -p build
clang -std=c11 -Wall -Wextra -Werror -g -I../fap -I. \
    test_base64.c ../fap/km_base64.c -o build/test_base64
./build/test_base64
```

- [ ] **Step 3: Write the failing test**

Create `tests/test_base64.c`:

```c
#include "minunit.h"
#include "km_base64.h"

static void test_decodes_empty(void) {
    uint8_t out[8];
    size_t out_len = 99;
    CHECK(km_base64_decode("", 0, out, sizeof(out), &out_len) == KmB64Ok, "empty ok");
    CHECK(out_len == 0, "empty length");
}

static void test_decodes_one_two_three_bytes(void) {
    uint8_t out[8];
    size_t out_len = 0;

    CHECK(km_base64_decode("QQ==", 4, out, sizeof(out), &out_len) == KmB64Ok, "QQ== ok");
    CHECK(out_len == 1 && out[0] == 'A', "QQ== -> A");

    CHECK(km_base64_decode("QUI=", 4, out, sizeof(out), &out_len) == KmB64Ok, "QUI= ok");
    CHECK(out_len == 2 && memcmp(out, "AB", 2) == 0, "QUI= -> AB");

    CHECK(km_base64_decode("QUJD", 4, out, sizeof(out), &out_len) == KmB64Ok, "QUJD ok");
    CHECK(out_len == 3 && memcmp(out, "ABC", 3) == 0, "QUJD -> ABC");
}

static void test_preserves_embedded_null(void) {
    /* base64 of the three bytes 'a', 0x00, 'b' */
    uint8_t out[8];
    size_t out_len = 0;
    CHECK(km_base64_decode("YQBi", 4, out, sizeof(out), &out_len) == KmB64Ok, "YQBi ok");
    CHECK(out_len == 3, "embedded null length");
    CHECK(out[0] == 'a' && out[1] == 0 && out[2] == 'b', "embedded null bytes");
}

static void test_rejects_bad_length(void) {
    uint8_t out[8];
    size_t out_len = 0;
    CHECK(km_base64_decode("QQ=", 3, out, sizeof(out), &out_len) == KmB64BadLength, "len 3");
    CHECK(km_base64_decode("QQQQQ", 5, out, sizeof(out), &out_len) == KmB64BadLength, "len 5");
}

static void test_rejects_bad_char(void) {
    uint8_t out[8];
    size_t out_len = 0;
    CHECK(km_base64_decode("QQ$=", 4, out, sizeof(out), &out_len) == KmB64BadChar, "dollar");
    CHECK(km_base64_decode("QQ =", 4, out, sizeof(out), &out_len) == KmB64BadChar, "space");
}

static void test_rejects_overlong_output(void) {
    uint8_t out[2];
    size_t out_len = 0;
    CHECK(km_base64_decode("QUJD", 4, out, sizeof(out), &out_len) == KmB64TooLong, "cap 2");
}

int main(void) {
    RUN(test_decodes_empty);
    RUN(test_decodes_one_two_three_bytes);
    RUN(test_preserves_embedded_null);
    RUN(test_rejects_bad_length);
    RUN(test_rejects_bad_char);
    RUN(test_rejects_overlong_output);
    REPORT();
}
```

Create `fap/km_base64.h`:

```c
#pragma once
#include <stddef.h>
#include <stdint.h>

typedef enum {
    KmB64Ok = 0,
    KmB64BadChar,
    KmB64BadLength,
    KmB64TooLong,
} KmB64Result;

/**
 * Decode standard base64. Requires in_len to be a multiple of 4.
 * Writes at most out_cap bytes; returns KmB64TooLong if the decoded
 * form would exceed out_cap. On any error *out_len is set to 0.
 */
KmB64Result km_base64_decode(
    const char* in,
    size_t in_len,
    uint8_t* out,
    size_t out_cap,
    size_t* out_len);
```

- [ ] **Step 4: Run the test to verify it fails**

```bash
bash tests/run.sh
```

Expected: FAIL — linker error, undefined symbol `km_base64_decode`.

- [ ] **Step 5: Write the implementation**

Create `fap/km_base64.c`:

```c
#include "km_base64.h"

static int b64_value(char c) {
    if(c >= 'A' && c <= 'Z') return c - 'A';
    if(c >= 'a' && c <= 'z') return c - 'a' + 26;
    if(c >= '0' && c <= '9') return c - '0' + 52;
    if(c == '+') return 62;
    if(c == '/') return 63;
    return -1;
}

KmB64Result km_base64_decode(
    const char* in,
    size_t in_len,
    uint8_t* out,
    size_t out_cap,
    size_t* out_len) {
    *out_len = 0;

    if(in_len % 4 != 0) return KmB64BadLength;

    size_t written = 0;

    for(size_t i = 0; i < in_len; i += 4) {
        int vals[4];
        size_t pad = 0;

        for(size_t j = 0; j < 4; j++) {
            char c = in[i + j];
            if(c == '=') {
                /* Padding is only legal in the last group, last two slots. */
                if(i + 4 != in_len || j < 2) return KmB64BadChar;
                pad++;
                vals[j] = 0;
            } else {
                if(pad > 0) return KmB64BadChar; /* data after padding */
                int v = b64_value(c);
                if(v < 0) return KmB64BadChar;
                vals[j] = v;
            }
        }

        uint32_t triple = ((uint32_t)vals[0] << 18) | ((uint32_t)vals[1] << 12) |
                          ((uint32_t)vals[2] << 6) | (uint32_t)vals[3];

        size_t produce = 3 - pad;
        if(written + produce > out_cap) {
            *out_len = 0;
            return KmB64TooLong;
        }

        if(produce > 0) out[written++] = (uint8_t)((triple >> 16) & 0xFF);
        if(produce > 1) out[written++] = (uint8_t)((triple >> 8) & 0xFF);
        if(produce > 2) out[written++] = (uint8_t)(triple & 0xFF);
    }

    *out_len = written;
    return KmB64Ok;
}
```

- [ ] **Step 6: Run the tests to verify they pass**

```bash
bash tests/run.sh
```

Expected: `6 run, 0 failed`

- [ ] **Step 7: Commit**

```bash
git add fap/km_base64.h fap/km_base64.c tests/
git commit -m "feat: add base64 decoder with host test harness"
```

---

### Task 3: Layout mapping core

Pure C with no Flipper dependencies, so it is host-testable. File I/O lives in Task 8.

**Files:**
- Create: `fap/km_layout.h`, `fap/km_layout.c`
- Create: `tests/test_layout.c`
- Modify: `tests/run.sh`

**Interfaces:**
- Consumes: nothing
- Produces:
  - `void km_layout_set_default(KmLayout* layout, const uint16_t* asciimap, size_t asciimap_entries)`
  - `bool km_layout_load_bytes(KmLayout* layout, const uint8_t* data, size_t len)`
  - `uint16_t km_layout_lookup(const KmLayout* layout, uint8_t c)`
  - `int km_layout_first_unmappable(const KmLayout* layout, const uint8_t* text, size_t len)` — returns `-1` when every character maps, else the zero-based index of the first that does not
  - `#define KM_LAYOUT_SIZE 128`, `#define KM_KEY_NONE 0`, `#define KM_LAYOUT_FILE_BYTES 256`

- [ ] **Step 1: Write the header**

Create `fap/km_layout.h`:

```c
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KM_LAYOUT_SIZE 128
#define KM_KEY_NONE 0
#define KM_LAYOUT_FILE_BYTES (KM_LAYOUT_SIZE * 2u) /* uint16_t[128] == 256 bytes */

typedef struct {
    uint16_t map[KM_LAYOUT_SIZE];
} KmLayout;

/** Seed the layout from the firmware's hid_asciimap (or any table). Entries
 *  beyond asciimap_entries, and beyond KM_LAYOUT_SIZE, become KM_KEY_NONE. */
void km_layout_set_default(KmLayout* layout, const uint16_t* asciimap, size_t asciimap_entries);

/** Load a Bad USB .kl file body. Requires exactly KM_LAYOUT_FILE_BYTES bytes. */
bool km_layout_load_bytes(KmLayout* layout, const uint8_t* data, size_t len);

/** Keycode for a byte, or KM_KEY_NONE if unmappable (includes all bytes >= 128). */
uint16_t km_layout_lookup(const KmLayout* layout, uint8_t c);

/** -1 if every byte maps, else the zero-based index of the first that does not. */
int km_layout_first_unmappable(const KmLayout* layout, const uint8_t* text, size_t len);
```

- [ ] **Step 2: Write the failing test**

Create `tests/test_layout.c`:

```c
#include "minunit.h"
#include "km_layout.h"

/* A stand-in for the firmware's hid_asciimap: 'a'..'c' map, everything
 * else is unmappable. Index == ASCII code. */
static uint16_t stub_map[128];

static void build_stub(void) {
    for(size_t i = 0; i < 128; i++) stub_map[i] = KM_KEY_NONE;
    stub_map['a'] = 0x04;
    stub_map['b'] = 0x05;
    stub_map['c'] = 0x06;
    stub_map['A'] = 0x04 | (1 << 9); /* shifted */
}

static void test_default_seeds_from_table(void) {
    build_stub();
    KmLayout l;
    km_layout_set_default(&l, stub_map, 128);
    CHECK(km_layout_lookup(&l, 'a') == 0x04, "a maps");
    CHECK(km_layout_lookup(&l, 'A') == (0x04 | (1 << 9)), "A maps with shift");
    CHECK(km_layout_lookup(&l, 'z') == KM_KEY_NONE, "z unmapped");
}

static void test_short_table_pads_with_none(void) {
    build_stub();
    KmLayout l;
    km_layout_set_default(&l, stub_map, 8); /* only first 8 entries provided */
    CHECK(km_layout_lookup(&l, 'a') == KM_KEY_NONE, "beyond provided range is none");
}

static void test_high_bytes_never_map(void) {
    build_stub();
    KmLayout l;
    km_layout_set_default(&l, stub_map, 128);
    CHECK(km_layout_lookup(&l, 0xC3) == KM_KEY_NONE, "0xC3 unmappable");
    CHECK(km_layout_lookup(&l, 0xFF) == KM_KEY_NONE, "0xFF unmappable");
}

static void test_first_unmappable_finds_index(void) {
    build_stub();
    KmLayout l;
    km_layout_set_default(&l, stub_map, 128);

    const uint8_t good[] = {'a', 'b', 'c'};
    CHECK(km_layout_first_unmappable(&l, good, 3) == -1, "all good");

    const uint8_t bad[] = {'a', 'b', 'c', 'z'};
    CHECK(km_layout_first_unmappable(&l, bad, 4) == 3, "index 3");

    const uint8_t utf8[] = {'a', 0xC3, 0xA9}; /* 'a' then UTF-8 e-acute */
    CHECK(km_layout_first_unmappable(&l, utf8, 3) == 1, "utf8 index 1");
}

static void test_load_bytes_requires_exact_size(void) {
    KmLayout l;
    uint8_t data[KM_LAYOUT_FILE_BYTES];
    for(size_t i = 0; i < sizeof(data); i++) data[i] = 0;

    CHECK(km_layout_load_bytes(&l, data, KM_LAYOUT_FILE_BYTES) == true, "exact size ok");
    CHECK(km_layout_load_bytes(&l, data, 255) == false, "255 rejected");
    CHECK(km_layout_load_bytes(&l, data, 257) == false, "257 rejected");
    CHECK(km_layout_load_bytes(&l, data, 0) == false, "0 rejected");
}

static void test_load_bytes_reads_little_endian(void) {
    KmLayout l;
    uint8_t data[KM_LAYOUT_FILE_BYTES];
    for(size_t i = 0; i < sizeof(data); i++) data[i] = 0;
    /* index 'a' == 97, byte offset 194: little-endian 0x0204 */
    data[97 * 2] = 0x04;
    data[97 * 2 + 1] = 0x02;

    CHECK(km_layout_load_bytes(&l, data, KM_LAYOUT_FILE_BYTES) == true, "loaded");
    CHECK(km_layout_lookup(&l, 'a') == 0x0204, "little-endian decode");
}

int main(void) {
    RUN(test_default_seeds_from_table);
    RUN(test_short_table_pads_with_none);
    RUN(test_high_bytes_never_map);
    RUN(test_first_unmappable_finds_index);
    RUN(test_load_bytes_requires_exact_size);
    RUN(test_load_bytes_reads_little_endian);
    REPORT();
}
```

- [ ] **Step 3: Add the test to the runner**

Replace `tests/run.sh` with:

```bash
#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"
mkdir -p build

clang -std=c11 -Wall -Wextra -Werror -g -I../fap -I. \
    test_base64.c ../fap/km_base64.c -o build/test_base64
./build/test_base64

clang -std=c11 -Wall -Wextra -Werror -g -I../fap -I. \
    test_layout.c ../fap/km_layout.c -o build/test_layout
./build/test_layout
```

- [ ] **Step 4: Run to verify it fails**

```bash
bash tests/run.sh
```

Expected: base64 tests pass, then FAIL — undefined symbols `km_layout_set_default` and friends.

- [ ] **Step 5: Write the implementation**

Create `fap/km_layout.c`:

```c
#include "km_layout.h"
#include <string.h>

void km_layout_set_default(KmLayout* layout, const uint16_t* asciimap, size_t asciimap_entries) {
    for(size_t i = 0; i < KM_LAYOUT_SIZE; i++) {
        layout->map[i] = (i < asciimap_entries) ? asciimap[i] : KM_KEY_NONE;
    }
}

bool km_layout_load_bytes(KmLayout* layout, const uint8_t* data, size_t len) {
    if(len != KM_LAYOUT_FILE_BYTES) return false;
    for(size_t i = 0; i < KM_LAYOUT_SIZE; i++) {
        layout->map[i] = (uint16_t)data[i * 2] | ((uint16_t)data[i * 2 + 1] << 8);
    }
    return true;
}

uint16_t km_layout_lookup(const KmLayout* layout, uint8_t c) {
    if(c >= KM_LAYOUT_SIZE) return KM_KEY_NONE;
    return layout->map[c];
}

int km_layout_first_unmappable(const KmLayout* layout, const uint8_t* text, size_t len) {
    for(size_t i = 0; i < len; i++) {
        if(km_layout_lookup(layout, text[i]) == KM_KEY_NONE) return (int)i;
    }
    return -1;
}
```

- [ ] **Step 6: Run to verify they pass**

```bash
bash tests/run.sh
```

Expected: `6 run, 0 failed` twice.

- [ ] **Step 7: Commit**

```bash
git add fap/km_layout.h fap/km_layout.c tests/test_layout.c tests/run.sh
git commit -m "feat: add keyboard layout mapping core"
```

---

### Task 4: FAP skeleton that builds, enters USB HID mode, and restores on exit

**Files:**
- Create: `fap/application.fam`, `fap/km_bridge.c`
- Create: `.gitignore`

**Interfaces:**
- Consumes: nothing yet
- Produces: `typedef struct KmApp KmApp` with fields used by later tasks — `Gui* gui`, `ViewPort* view_port`, `FuriMessageQueue* input_queue`, `FuriHalUsbInterface* usb_prev`, `KmLayout layout`, `bool running`

- [ ] **Step 1: Install ufbt and fetch the SDK**

```bash
pip install --upgrade ufbt
ufbt update
```

Expected: downloads the SDK and ARM toolchain for the stock firmware channel. Confirm the API version matches the plan:

```bash
grep -m1 "^Version" ~/.ufbt/current/sdk/targets/f7/api_symbols.csv
```

Expected: `Version,+,88.2,,` — if it differs, re-verify the API reference section before continuing.

- [ ] **Step 2: Create `.gitignore`**

```
build/
tests/build/
.ufbt/
dist/
*.fap
```

- [ ] **Step 3: Create the app manifest**

Create `fap/application.fam`:

```python
App(
    appid="km_bridge",
    name="KM Bridge",
    apptype=FlipperAppType.EXTERNAL,
    entry_point="km_bridge_app",
    stack_size=4 * 1024,
    fap_category="Tools",
    fap_description="Type text sent from a phone over BLE as USB HID keystrokes",
)
```

- [ ] **Step 4: Write the app skeleton**

Create `fap/km_bridge.c`:

```c
#include <furi.h>
#include <furi_hal_usb.h>
#include <furi_hal_usb_hid.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdio.h>
#include <string.h>

#include "km_layout.h"

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* input_queue;
    FuriHalUsbInterface* usb_prev;
    KmLayout layout;
    bool running;
} KmApp;

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

    gui_remove_view_port(app->gui, app->view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(app->view_port);
    furi_message_queue_free(app->input_queue);

    furi_hal_usb_set_config(app->usb_prev, NULL);

    free(app);
    return 0;
}
```

- [ ] **Step 5: Build**

```bash
cd fap && ufbt
```

Expected: builds `km_bridge.fap` with no errors and no unresolved-symbol warnings.

- [ ] **Step 6: Deploy and verify manually**

Connect the Flipper by USB, then:

```bash
cd fap && ufbt launch
```

Verify on the device: the app opens, shows "KM Bridge" and `USB: ready`, and Back exits cleanly. On the PC, confirm a new HID keyboard device appeared while the app was open and disappeared after exit (Windows Device Manager → Keyboards).

- [ ] **Step 7: Commit**

```bash
git add fap/application.fam fap/km_bridge.c .gitignore
git commit -m "feat: add FAP skeleton with USB HID mode and restore on exit"
```

---

### Task 5: `kmtype` CLI command typing over the USB CLI

Driven over USB serial here, deliberately — this decouples HID correctness from BLE. No confirmation step yet; that lands in Task 6.

**Files:**
- Create: `fap/km_cli.h`, `fap/km_cli.c`
- Modify: `fap/km_bridge.c`

**Interfaces:**
- Consumes: `km_base64_decode`, `km_layout_lookup`, `km_layout_first_unmappable`, `KmApp`
- Produces:
  - `void km_cli_register(KmApp* app)` and `void km_cli_unregister(KmApp* app)`
  - `KmApp` gains `CliRegistry* cli_registry`
  - `#define KM_PAYLOAD_MAX 1024`, `#define KM_KEY_DELAY_MS 8`

- [ ] **Step 1: Add the shared app header**

Extract the `KmApp` struct so `km_cli.c` can see it. Create `fap/km_bridge_i.h`:

```c
#pragma once
#include <furi.h>
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
```

In `fap/km_bridge.c`, delete the inline `KmApp` definition and `#include "km_bridge_i.h"` instead.

- [ ] **Step 2: Write the CLI module**

Create `fap/km_cli.h`:

```c
#pragma once
#include "km_bridge_i.h"

void km_cli_register(KmApp* app);
void km_cli_unregister(KmApp* app);
```

Create `fap/km_cli.c`:

```c
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
```

- [ ] **Step 3: Wire it into the app lifecycle**

In `fap/km_bridge.c`, add `#include "km_cli.h"`.

Call `km_cli_register(app);` immediately after the `furi_hal_usb_set_config(&usb_hid, NULL);` line.

In teardown, call `km_cli_unregister(app);` as the **first** statement after the event loop ends — before `gui_remove_view_port`. Leaving the command registered past app exit crashes the Flipper on next invocation.

- [ ] **Step 4: Build and deploy**

```bash
cd fap && ufbt launch
```

- [ ] **Step 5: Verify manually over USB serial**

Open the Flipper CLI over USB (`ufbt cli`, or any serial terminal at the Flipper's virtual COM port). With the KM Bridge app open on the device, focus a text editor on the PC and run:

```
kmtype aGVsbG8gd29ybGQ=
```

Expected: `hello world` is typed into the focused editor, and the CLI prints `OK`.

Then verify the error paths:

```
kmtype QQ=
```
Expected: `ERR badb64`

- [ ] **Step 6: Verify the teardown ordering**

Exit the app with Back, then run `kmtype QQ==` in the CLI again. Expected: the Flipper reports an unknown command and **does not crash**. A crash here means `km_cli_unregister` is not being reached.

- [ ] **Step 7: Commit**

```bash
git add fap/km_bridge_i.h fap/km_cli.h fap/km_cli.c fap/km_bridge.c
git commit -m "feat: add kmtype CLI command that types decoded text over USB HID"
```

---

### Task 6: Confirmation screen, 60-second timeout, and buffer zeroing

**Files:**
- Modify: `fap/km_bridge_i.h`, `fap/km_cli.c`, `fap/km_bridge.c`

**Interfaces:**
- Consumes: everything from Task 5
- Produces: `KmApp` gains `FuriEventFlag* confirm_flags`, `FuriMutex* state_mutex`, `size_t pending_len`, `bool awaiting_confirm`; flags `KM_FLAG_CONFIRM = (1 << 0)` and `KM_FLAG_CANCEL = (1 << 1)`; `#define KM_CONFIRM_TIMEOUT_MS 60000`

- [ ] **Step 1: Extend the app state**

In `fap/km_bridge_i.h`, add above the struct:

```c
#define KM_CONFIRM_TIMEOUT_MS 60000
#define KM_FLAG_CONFIRM (1 << 0)
#define KM_FLAG_CANCEL (1 << 1)
```

and add these fields to `KmApp`:

```c
    FuriEventFlag* confirm_flags;
    FuriMutex* state_mutex;
    size_t pending_len;
    bool awaiting_confirm;
```

- [ ] **Step 2: Allocate and free them**

In `fap/km_bridge.c`, after `app->input_queue = ...`:

```c
    app->confirm_flags = furi_event_flag_alloc();
    app->state_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->awaiting_confirm = false;
    app->pending_len = 0;
```

In teardown, after `furi_message_queue_free(app->input_queue);`:

```c
    furi_event_flag_free(app->confirm_flags);
    furi_mutex_free(app->state_mutex);
```

- [ ] **Step 3: Turn OK and Back into confirm and cancel**

Replace the input handling inside the event loop in `fap/km_bridge.c`:

```c
        if(furi_message_queue_get(app->input_queue, &event, 100) == FuriStatusOk) {
            if(event.type == InputTypeShort) {
                furi_mutex_acquire(app->state_mutex, FuriWaitForever);
                bool awaiting = app->awaiting_confirm;
                furi_mutex_release(app->state_mutex);

                if(awaiting) {
                    if(event.key == InputKeyOk) {
                        furi_event_flag_set(app->confirm_flags, KM_FLAG_CONFIRM);
                    } else if(event.key == InputKeyBack) {
                        furi_event_flag_set(app->confirm_flags, KM_FLAG_CANCEL);
                    }
                } else if(event.key == InputKeyBack) {
                    app->running = false;
                }
            }
        }
```

Back now cancels a pending prompt rather than exiting; it only exits when nothing is pending.

- [ ] **Step 4: Show the prompt**

Replace `km_draw_callback` in `fap/km_bridge.c`:

```c
static void km_draw_callback(Canvas* canvas, void* ctx) {
    KmApp* app = ctx;

    furi_mutex_acquire(app->state_mutex, FuriWaitForever);
    bool awaiting = app->awaiting_confirm;
    size_t len = app->pending_len;
    furi_mutex_release(app->state_mutex);

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 12, "KM Bridge");
    canvas_set_font(canvas, FontSecondary);

    if(awaiting) {
        char line[32];
        snprintf(line, sizeof(line), "Type %u chars?", (unsigned)len);
        canvas_draw_str(canvas, 2, 28, line);
        canvas_draw_str(canvas, 2, 40, "OK = type   Back = cancel");
    } else {
        canvas_draw_str(
            canvas, 2, 28, furi_hal_hid_is_connected() ? "USB: ready" : "USB: no host");
        canvas_draw_str(canvas, 2, 40, "Waiting for phone");
    }
}
```

Note this draws only the character *count*, never the payload.

- [ ] **Step 5: Gate typing behind the confirmation**

In `fap/km_cli.c`, replace the block from `km_type_buffer(...)` through `km_reply(pipe, "OK\r\n");` with:

```c
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
```

The `memset` at `cleanup:` already covers every one of these paths.

- [ ] **Step 6: Build, deploy, and verify manually**

```bash
cd fap && ufbt launch
```

Verify all three outcomes over the USB CLI with `kmtype aGVsbG8=`:

1. Press OK → `hello` is typed, CLI prints `OK`.
2. Press Back → nothing typed, CLI prints `ERR cancelled`, app stays open.
3. Wait 60 seconds → nothing typed, CLI prints `ERR timeout`.

Then press Back with no prompt pending and confirm the app exits.

- [ ] **Step 7: Commit**

```bash
git add fap/km_bridge_i.h fap/km_cli.c fap/km_bridge.c
git commit -m "feat: require physical confirmation before typing, with 60s timeout"
```

---

### Task 7: Remaining error replies

**Files:**
- Modify: `fap/km_cli.c`

**Interfaces:**
- Consumes: everything from Task 6
- Produces: no new symbols; completes the error contract the web page depends on

- [ ] **Step 1: Reject when no USB host is present**

In `fap/km_cli.c`, insert at the very top of `km_cli_kmtype`, before decoding:

```c
    if(!furi_hal_hid_is_connected()) {
        km_reply(pipe, "ERR nohost\r\n");
        return;
    }
```

This returns before `payload` is populated, so no zeroing is needed on this path.

- [ ] **Step 2: Reject empty payloads**

Immediately after `size_t b64_len = furi_string_size(args);`:

```c
    if(b64_len == 0) {
        km_reply(pipe, "ERR badb64\r\n");
        return;
    }
```

- [ ] **Step 3: Reject unmappable characters**

After the decode succeeds and before the confirmation block:

```c
    {
        int bad_index = km_layout_first_unmappable(&app->layout, payload, payload_len);
        if(bad_index >= 0) {
            char msg[32];
            snprintf(msg, sizeof(msg), "ERR unmappable@%d\r\n", bad_index);
            km_reply(pipe, msg);
            goto cleanup;
        }
    }
```

The index is zero-based into the decoded text, per the spec. Note the message contains only the index, never the character.

The braces are load-bearing: the earlier `goto cleanup` statements jump forward past this point, and jumping over an initialized declaration at function scope draws a warning under some GCC configurations. Scoping it to a block avoids the question entirely.

- [ ] **Step 4: Build, deploy, and verify each path**

```bash
cd fap && ufbt launch
```

Over the USB CLI, with the app open:

| Input | Expected reply |
|---|---|
| `kmtype` (no argument) | `ERR badb64` |
| `kmtype QQ=` | `ERR badb64` |
| `kmtype w6k=` (UTF-8 `é`) | `ERR unmappable@0` |
| `kmtype YcOp` (`a` then `é`) | `ERR unmappable@1` |
| base64 over 1368 chars | `ERR toolong` |

For `ERR nohost`: unplug USB data (keep the Flipper powered by battery), connect over BLE instead, and send a `kmtype`. Expected `ERR nohost`. This path is easier to re-test after Task 9; note it and confirm then if BLE is not yet wired.

- [ ] **Step 5: Commit**

```bash
git add fap/km_cli.c
git commit -m "feat: add nohost, empty, and unmappable error replies"
```

---

### Task 8: Layout file loading and selection

**Files:**
- Create: `fap/km_layout_file.h`, `fap/km_layout_file.c`
- Modify: `fap/km_bridge.c`, `fap/km_bridge_i.h`

**Interfaces:**
- Consumes: `km_layout_load_bytes`, `KmApp`
- Produces:
  - `bool km_layout_file_load(KmLayout* layout, const char* path)` — returns false and leaves `layout` untouched if the file is missing or not exactly 256 bytes
  - `void km_layout_settings_save(const char* path)` / `bool km_layout_settings_load(char* path_out, size_t cap)`
  - `KmApp` gains `char layout_path[128]`
  - `#define KM_LAYOUT_DIR "/ext/badusb/assets/layouts"`, `#define KM_SETTINGS_PATH "/ext/apps_data/km_bridge/layout.txt"`

- [ ] **Step 1: Write the loader**

Create `fap/km_layout_file.h`:

```c
#pragma once
#include "km_layout.h"
#include <stdbool.h>
#include <stddef.h>

#define KM_LAYOUT_DIR "/ext/badusb/assets/layouts"
#define KM_SETTINGS_DIR "/ext/apps_data/km_bridge"
#define KM_SETTINGS_PATH KM_SETTINGS_DIR "/layout.txt"

bool km_layout_file_load(KmLayout* layout, const char* path);
void km_layout_settings_save(const char* path);
bool km_layout_settings_load(char* path_out, size_t cap);
```

Create `fap/km_layout_file.c`:

```c
#include "km_layout_file.h"

#include <furi.h>
#include <storage/storage.h>
#include <string.h>

bool km_layout_file_load(KmLayout* layout, const char* path) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    bool ok = false;

    if(storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        uint8_t data[KM_LAYOUT_FILE_BYTES];
        size_t read = storage_file_read(file, data, sizeof(data));
        if(read == KM_LAYOUT_FILE_BYTES) {
            ok = km_layout_load_bytes(layout, data, read);
        }
        storage_file_close(file);
    }

    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return ok;
}

void km_layout_settings_save(const char* path) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, KM_SETTINGS_DIR);

    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, KM_SETTINGS_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_write(file, path, strlen(path));
        storage_file_close(file);
    }
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}

bool km_layout_settings_load(char* path_out, size_t cap) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    bool ok = false;

    if(storage_file_open(file, KM_SETTINGS_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        size_t read = storage_file_read(file, path_out, cap - 1);
        path_out[read] = '\0';
        ok = read > 0;
        storage_file_close(file);
    }

    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return ok;
}
```

This file holds no secrets — the layout path is not sensitive.

- [ ] **Step 2: Add the field and restore on start**

In `fap/km_bridge_i.h`, add to `KmApp`:

```c
    char layout_path[128];
```

In `fap/km_bridge.c`, add `#include "km_layout_file.h"`, and replace the single `km_layout_set_default(...)` call with:

```c
    km_layout_set_default(&app->layout, hid_asciimap, sizeof(hid_asciimap) / sizeof(uint16_t));
    app->layout_path[0] = '\0';
    if(km_layout_settings_load(app->layout_path, sizeof(app->layout_path))) {
        if(!km_layout_file_load(&app->layout, app->layout_path)) {
            /* Bad or missing file: keep the US default. */
            app->layout_path[0] = '\0';
        }
    }
```

- [ ] **Step 3: Add layout selection to the UI**

In `fap/km_bridge.c`, add near the top:

```c
#include <dialogs/dialogs.h>
```

Add this helper above `km_bridge_app`:

```c
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
```

In the input handler's non-awaiting branch, add a case before the `InputKeyBack` check:

```c
                    if(event.key == InputKeyOk) {
                        km_pick_layout(app);
                    } else if(event.key == InputKeyBack) {
```

(so OK opens the layout picker when nothing is pending, and confirms when something is).

- [ ] **Step 4: Show the active layout**

In `km_draw_callback`, in the `else` branch, replace the "Waiting for phone" line with:

```c
        const char* name = app->layout_path[0] ? strrchr(app->layout_path, '/') : NULL;
        char line[32];
        snprintf(line, sizeof(line), "Layout: %s", name ? name + 1 : "US (default)");
        canvas_draw_str(canvas, 2, 40, line);
        canvas_draw_str(canvas, 2, 52, "OK = change layout");
```

- [ ] **Step 5: Build, deploy, and verify**

```bash
cd fap && ufbt launch
```

Verify: OK opens a browser listing `.kl` files under `/ext/badusb/assets/layouts`; picking one updates the on-screen layout name; exiting and reopening the app remembers the choice. Then confirm typing still works via `kmtype aGVsbG8=` over the USB CLI.

If `/ext/badusb/assets/layouts` is empty on the device, install the Bad USB app's assets via qFlipper, or skip the picker check and verify only that the US default still types correctly.

- [ ] **Step 6: Commit**

```bash
git add fap/km_layout_file.h fap/km_layout_file.c fap/km_bridge.c fap/km_bridge_i.h
git commit -m "feat: load and remember Bad USB keyboard layout files"
```

---

### Task 9: Web page

**Files:**
- Create: `web/framing.js`, `web/framing.test.mjs`, `web/index.html`

**Interfaces:**
- Consumes: the `kmtype <base64>\r` command contract and the `OK` / `ERR …` reply contract
- Produces:
  - `export function buildCommand(text)` → the full `kmtype <base64>\r` string
  - `export function chunk(bytes, size)` → array of `Uint8Array`
  - `export function toBase64(text)` → base64 of the UTF-8 bytes

- [ ] **Step 1: Write the failing test**

Create `web/framing.test.mjs`:

```js
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { buildCommand, chunk, toBase64 } from './framing.js';

test('toBase64 encodes ASCII', () => {
  assert.equal(toBase64('hello'), 'aGVsbG8=');
});

test('toBase64 encodes UTF-8 multi-byte input', () => {
  // The Flipper will reject this with ERR unmappable, but encoding must
  // still be byte-correct rather than throwing.
  assert.equal(toBase64('é'), 'w6k=');
});

test('buildCommand produces a CR-terminated kmtype line', () => {
  assert.equal(buildCommand('hello'), 'kmtype aGVsbG8=\r');
});

test('chunk splits to the given size', () => {
  const bytes = new Uint8Array([1, 2, 3, 4, 5]);
  const parts = chunk(bytes, 2);
  assert.equal(parts.length, 3);
  assert.deepEqual([...parts[0]], [1, 2]);
  assert.deepEqual([...parts[2]], [5]);
});

test('chunk returns one part when input fits', () => {
  const parts = chunk(new Uint8Array([1, 2]), 20);
  assert.equal(parts.length, 1);
});

test('chunk returns nothing for empty input', () => {
  assert.equal(chunk(new Uint8Array([]), 20).length, 0);
});
```

- [ ] **Step 2: Run to verify it fails**

```bash
node --test "web/*.test.mjs"
```

Expected: FAIL — cannot find module `./framing.js`.

- [ ] **Step 3: Write the implementation**

Create `web/framing.js`:

```js
export function toBase64(text) {
  const bytes = new TextEncoder().encode(text);
  let binary = '';
  for (const b of bytes) binary += String.fromCharCode(b);
  return btoa(binary);
}

export function buildCommand(text) {
  return `kmtype ${toBase64(text)}\r`;
}

export function chunk(bytes, size) {
  const parts = [];
  for (let i = 0; i < bytes.length; i += size) {
    parts.push(bytes.slice(i, i + size));
  }
  return parts;
}
```

- [ ] **Step 4: Run to verify it passes**

```bash
node --test "web/*.test.mjs"
```

Expected: 6 tests pass.

- [ ] **Step 5: Write the page**

Create `web/index.html`:

```html
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<meta name="apple-mobile-web-app-capable" content="yes">
<title>KM Bridge</title>
<style>
  :root { color-scheme: light dark; }
  body { font: 16px/1.4 system-ui, sans-serif; margin: 0; padding: 1rem;
         display: flex; flex-direction: column; gap: .75rem; min-height: 100vh; }
  textarea { width: 100%; min-height: 8rem; font: 16px ui-monospace, monospace;
             padding: .5rem; box-sizing: border-box; }
  button { font-size: 1.1rem; padding: .9rem; border-radius: .5rem; border: 0; }
  #connect { background: #2d6cdf; color: #fff; }
  #send { background: #1a8a3a; color: #fff; }
  button:disabled { opacity: .45; }
  #status { font-size: .9rem; opacity: .8; min-height: 1.4em; }
</style>
</head>
<body>
  <button id="connect">Connect to Flipper</button>
  <textarea id="text" placeholder="Paste here" autocomplete="off"
            autocorrect="off" autocapitalize="off" spellcheck="false"></textarea>
  <button id="send" disabled>Send</button>
  <div id="status">Not connected</div>

<script type="module">
import { buildCommand, chunk } from './framing.js';

const SERVICE = '8fe5b3d5-2e7f-4a98-2a48-7acc60fe0000';
const TX_CHAR = '19ed82ae-ed21-4c9d-4145-228e62fe0000';
const RX_CHAR = '19ed82ae-ed21-4c9d-4145-228e61fe0000';
const CHUNK = 20; // safe under the default 23-byte ATT MTU

const $ = (id) => document.getElementById(id);
const status = (msg) => { $('status').textContent = msg; };

let tx = null;

$('connect').onclick = async () => {
  try {
    status('Requesting device…');
    const device = await navigator.bluetooth.requestDevice({
      filters: [{ services: [SERVICE] }],
    });
    device.addEventListener('gattserverdisconnected', () => {
      tx = null;
      $('send').disabled = true;
      status('Disconnected');
    });

    const server = await device.gatt.connect();
    const service = await server.getPrimaryService(SERVICE);
    tx = await service.getCharacteristic(TX_CHAR);

    const rx = await service.getCharacteristic(RX_CHAR);
    await rx.startNotifications();
    rx.addEventListener('characteristicvaluechanged', (e) => {
      const reply = new TextDecoder().decode(e.target.value).trim();
      if (reply) status(reply);
    });

    // Deliberately NOT sending start_rpc_session — we want the CLI shell.
    $('send').disabled = false;
    status(`Connected to ${device.name || 'Flipper'}`);
  } catch (err) {
    status(`Error: ${err.message}`);
  }
};

$('send').onclick = async () => {
  if (!tx) return;
  const text = $('text').value;
  if (!text) { status('Nothing to send'); return; }

  try {
    $('send').disabled = true;
    status('Sending — press OK on the Flipper');
    const bytes = new TextEncoder().encode(buildCommand(text));
    for (const part of chunk(bytes, CHUNK)) {
      await tx.writeValue(part);
    }
    $('text').value = '';
  } catch (err) {
    status(`Error: ${err.message}`);
  } finally {
    $('send').disabled = false;
  }
};

if (!navigator.bluetooth) {
  status('Web Bluetooth unavailable — use Bluefy on iOS or Chrome on Android');
  $('connect').disabled = true;
}
</script>
</body>
</html>
```

Note: the textarea is cleared after sending, and nothing is written to `localStorage`.

- [ ] **Step 6: Commit**

```bash
git add web/
git commit -m "feat: add Web Bluetooth page for sending text to the Flipper"
```

---

### Task 10: Deploy and end-to-end verification

**Files:**
- Create: `docs/manual-test-checklist.md`
- Create: `README.md`

- [ ] **Step 1: Publish to GitHub Pages**

Create a GitHub repository, push, then enable Pages:

```bash
gh repo create flipper-km --private --source=. --push
gh api -X POST repos/:owner/flipper-km/pages -f "source[branch]=main" -f "source[path]=/" 2>/dev/null || true
```

Enable Pages in repository Settings → Pages if the API call did not take, serving from the branch root. The page will be at `https://<user>.github.io/flipper-km/web/`.

Web Bluetooth requires HTTPS; GitHub Pages provides it.

- [ ] **Step 2: Add to the iPhone home screen**

Open the Pages URL in Bluefy, then use Share → Add to Home Screen.

- [ ] **Step 3: Write the manual test checklist**

Create `docs/manual-test-checklist.md`:

```markdown
# Manual test checklist

Hardware paths that cannot be unit-tested. Run before any release.

## Setup
- [ ] Flipper connected to the PC by USB, Bluetooth on, KM Bridge app open
- [ ] Phone paired, page open, "Connected" shown

## Happy path
- [ ] Paste `hello world`, tap Send, press OK on the Flipper → typed correctly, page shows `OK`
- [ ] Paste a password containing `#$%^&*~[]{}|\` → every character typed correctly
- [ ] Paste a 1000-character string → typed completely, nothing dropped

## Safety
- [ ] Send, then press Back on the Flipper → nothing typed, page shows `ERR cancelled`
- [ ] Send, wait 60 s → nothing typed, page shows `ERR timeout`
- [ ] Send, then exit the app before confirming → nothing typed, no crash
- [ ] Exit the app, then invoke `kmtype` over the USB CLI → unknown command, no crash

## Error paths
- [ ] Paste text containing an emoji → `ERR unmappable@<n>`, nothing typed
- [ ] Paste text over 1024 characters → `ERR toolong`, nothing typed
- [ ] Unplug USB, send over BLE → `ERR nohost`

## Layout
- [ ] Select a non-US `.kl` layout, confirm it persists across an app restart
- [ ] With the PC set to the matching layout, confirm symbols type correctly

## Hygiene
- [ ] `grep -rn "FURI_LOG" fap/` shows no call taking the payload buffer
- [ ] After a send, nothing resembling the payload appears under `/ext/apps_data/km_bridge/`
```

- [ ] **Step 4: Run the full checklist**

Work through every item. Record any failures as issues before declaring the project done.

- [ ] **Step 5: Write the README**

Create `README.md` covering: what the project does, the two components, how to build (`ufbt`), how to run tests (`bash tests/run.sh` and `node --test "web/*.test.mjs"`), how to pair the phone, and the security properties from the spec (nothing at rest, confirm-to-type, ASCII only).

- [ ] **Step 6: Commit**

```bash
git add README.md docs/manual-test-checklist.md
git commit -m "docs: add README and manual test checklist"
```

---

## Notes for the implementer

**The teardown ordering in Task 5 Step 3 is the single most dangerous line in this project.** If `cli_registry_delete_command` is not called before the app frees itself, the next `kmtype` invocation dereferences a dangling context pointer and crashes the Flipper. Task 5 Step 6 exists specifically to catch that.

**Do not add convenience features that persist the payload.** A "recent sends" list, a draft autosave, or a debug log of what was typed would each defeat the entire security model. If one seems useful, raise it rather than adding it.

**`furi_hal_hid_is_connected()` reports USB HID readiness, not BLE.** It is the correct check for `ERR nohost`.

**Two spec requirements are satisfied without a dedicated task — verify rather than re-implement:**

- *"BLE disconnects mid-transfer → zero partial buffer."* The payload lives in a stack array inside `km_cli_kmtype`. If the link drops while awaiting confirmation, `furi_event_flag_wait` runs to its 60-second timeout, falls through to `cleanup:`, and the `memset` executes. The requirement is met by the existing control flow; the reply write simply goes nowhere. Confirm this by disconnecting the phone mid-prompt and checking the Flipper does not crash.
- *"Command parsing, including missing and empty arguments."* The spec lists this as a host-testable unit, but the implementation reduces to `furi_string_trim` plus an empty check — two `FuriString` calls with no host-testable surface, since `FuriString` is firmware-only. It is covered by the Task 7 Step 4 verification matrix (`kmtype` with no argument, and malformed input) rather than by a unit test. This is a deliberate deviation from the spec's testing section, recorded here so it is not mistaken for an oversight.
