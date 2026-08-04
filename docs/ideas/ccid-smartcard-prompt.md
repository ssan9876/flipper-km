# Prompt: Flipper Zero CCID smartcard reader

Paste everything below the line into a fresh Claude Code session, in an empty
directory.

---

I want to build a Flipper Zero application that makes the Flipper appear to a
host computer as a **USB smartcard reader with a card inserted** (USB CCID
class), using the operating system's built-in CCID driver so nothing has to be
installed on the host.

## Why this is worth building

The Flipper firmware exports a complete CCID interface to external apps, and as
far as I can tell **nobody has ever used it**. A GitHub code search for
`furi_hal_usb_ccid_insert_smartcard` returns ~173 results, and every one of them
is the header or its implementation inside a firmware fork — there is no
consumer anywhere. There is no in-tree demo app either. So this is an entire USB
device class the hardware supports and no app exploits.

Treat that as motivation, not as license to skip verification.

## Verified API surface

I confirmed these against the ufbt release SDK (firmware 1.4.3, API 87.1) by
reading `targets/furi_hal_include/furi_hal_usb_ccid.h` and checking
`targets/f7/api_symbols.csv`. **Re-verify them against whatever SDK you end up
with before writing code** — the API version differs between the `dev` and
`release` channels, and I want you to catch that rather than assume.

```c
#define CCID_SHORT_APDU_SIZE (0xFF)   /* 255-byte max APDU */

typedef struct {
    uint16_t vid;
    uint16_t pid;
    char manuf[32];
    char product[32];
} FuriHalUsbCcidConfig;

typedef struct {
    /* Return the card's ATR (Answer To Reset) when the host powers the card. */
    void (*icc_power_on_callback)(uint8_t* dataBlock, uint32_t* dataBlockLen, void* context);

    /* Handle one APDU and write the response, including the status word. */
    void (*xfr_datablock_callback)(
        const uint8_t* pcToReaderDataBlock,
        uint32_t pcToReaderDataBlockLen,
        uint8_t* readerToPcDataBlock,
        uint32_t* readerToPcDataBlockLen,
        void* context);
} CcidCallbacks;

void furi_hal_usb_ccid_set_callbacks(CcidCallbacks* cb, void* context);
void furi_hal_usb_ccid_insert_smartcard(void);
void furi_hal_usb_ccid_remove_smartcard(void);

extern FuriHalUsbInterface usb_ccid;          /* pass to furi_hal_usb_set_config */
```

All of the above are exported to external apps (`.fap`). The USB layer handles
CCID protocol framing, so the app only deals with the ATR and APDUs — this is a
much smaller job than implementing CCID from scratch.

## Critical gotcha: USB modes are mutually exclusive

`furi_hal_usb_set_config()` sets **the** USB device mode. `usb_cdc_single` (the
serial CLI) and `usb_ccid` cannot both be active. The moment your app claims
CCID:

- the Flipper's USB serial port **disappears** from the host,
- `ufbt launch` reports `ClearCommError failed` — **this is expected, not a
  failure**; the app uploaded and started, then killed the serial link ufbt was
  talking over,
- you cannot debug over the USB CLI while the app runs,
- and you must restore the previous config on exit (save
  `furi_hal_usb_get_config()` first) or the user loses their serial port.

I lost real time to this on a previous Flipper project by misreading the
disconnect as a crash. Do not repeat it.

Related: `furi_hal_usb_set_config` returns `false` when the mode switch is
locked (e.g. an RPC session holds USB). **Check the return value** and surface
the failure on the Flipper's screen rather than assuming success.

## Scope

**Build:**
1. A `.fap` that claims `usb_ccid`, inserts a virtual card, and answers APDUs.
2. A software-emulated card to start with — something that responds correctly to
   `SELECT` and returns sensible status words. Correctness of the plumbing
   matters more than which applet it pretends to be.
3. A Flipper screen showing connection state, card inserted/removed, and a
   counter of APDUs handled, so behaviour is observable without a debugger.

**Explicitly out of scope for v1** (raise them, do not build them):
- proxying APDUs to a physical NFC card held to the Flipper — this is the
  eventual goal, but it depends on v1 working and doubles the unknowns
- Windows smartcard logon / PIV emulation — a large spec, worth a separate cycle
- any secret storage on the Flipper (it has no secure element; be honest about
  that in the README rather than implying security it does not have)

## How I want you to work

**Verify, do not recall.** Every firmware symbol, signature and constant must be
checked against the actual SDK headers or `api_symbols.csv` before you use it.
If you cannot confirm something, say so rather than guessing. Most of the pain in
my last Flipper project came from an assumption I inferred from a code sample
instead of reading the firmware.

**Risk-first milestones.** Order the work so the least-proven thing is tested
earliest, and stop at each gate:

- **M0 (gate, no real logic):** app claims `usb_ccid` and calls
  `insert_smartcard`. Does the host enumerate a smartcard reader at all? On
  Windows check `certutil -scinfo` or Device Manager; on Linux `pcsc_scan`. If
  the OS never sees a reader, **stop** — nothing downstream matters.
- **M1:** return a valid ATR on power-on and confirm the host reads it.
- **M2:** answer one trivial APDU (`SELECT` → `0x9000`) and confirm round-trip.
- **M3:** a coherent emulated applet with several commands.
- **M4:** UI, error handling, teardown, docs.

**Make it testable from the host.** Write a Python test harness using `pyscard`
that connects to the reader, sends APDUs and asserts on responses. That turns
M1–M3 into automated checks instead of manual clicking, which matters because
the USB CLI is unavailable while the app runs. Pure logic (APDU parsing, status
words, ATR construction) should be plain C compiled and unit-tested on the host,
separately from anything touching the Flipper HAL.

**Tell me plainly when something does not work.** If a milestone fails, say so
with the evidence. Do not report partial work as complete.

## Environment

- Windows, PowerShell and Git Bash both available
- Build with `ufbt` (`pip install ufbt`, then `ufbt update`). If `ufbt` is not on
  PATH, `python -m ufbt` works.
- For host-side C unit tests, use `zig cc` (`winget install zig.zig`). Plain
  clang on Windows does **not** work standalone — LLVM's Windows build targets
  MSVC and cannot find `stdio.h` or the C runtime without a multi-gigabyte
  Windows SDK install. `zig cc` ships its own libc headers.
- Confirm the SDK's API version early:
  `grep -m1 "^Version" ~/.ufbt/current/sdk_headers/f7_sdk/targets/f7/api_symbols.csv`

## Start here

Before writing any code, read the relevant firmware sources, confirm the API
surface above, and tell me:

1. what the host actually needs from a CCID device before it will bind its
   driver and expose the reader (descriptors, ATR format, mandatory commands),
2. whether M0 looks achievable and what could prevent it,
3. what you would emulate first, and why.

Then propose a plan. Do not start implementing until I have agreed to it.
