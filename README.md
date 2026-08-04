# KM Bridge

Paste text on your phone, have a Flipper Zero type it into the focused field on a
USB-connected computer.

Built for one specific annoyance: password managers live on the phone, but the
password is needed on a computer that doesn't have one installed. Reading a
30-character generated password off a screen and retyping it is miserable.

```
┌─────────────────┐                    ┌──────────────┐              ┌─────┐
│  iPhone/Android │   BLE (bonded,     │ Flipper Zero │  USB HID     │ PC  │
│  Bluefy/Chrome  │───encrypted)──────▶│ km_bridge.fap│──keystrokes─▶│     │
│  one web page   │  serial service    │              │              │     │
└─────────────────┘                    └──────────────┘              └─────┘
```

No server, no accounts, nothing leaves your two devices.

## How it works

`km_bridge.fap` puts USB into HID-keyboard mode and takes over the Flipper's BLE
serial link. The web page connects over BLE and sends the line
`kmtype <base64>`. The Flipper decodes it, waits for you to physically press OK,
then types it as keystrokes.

Base64 is used because the protocol is line-oriented — a password containing a
space or a control character would otherwise be mangled by the parser.

### Why the BLE link has to be taken over

The Flipper's BLE serial link is **not** a CLI shell. The moment a phone
connects, `bt.c` opens a protobuf RPC session and feeds every incoming byte to
`rpc_session_feed()`. Plain text written to it is handed to a protobuf parser and
silently discarded.

Receiving raw bytes therefore requires two calls, in this order:

```c
ble_profile_serial_set_rpc_active(profile, false);   /* stop RPC consuming it */
ble_profile_serial_set_event_callback(profile, ...); /* then claim it */
```

Installing the callback without disabling RPC first is the documented failure
mode where the firmware logs `Received N bytes` but the callback never fires.

**Claiming it once is not enough.** `bt.c`'s connect handler reinstalls its own
callback and re-enables RPC on *every* connection, so the app registers
`bt_set_status_changed_callback` and re-claims the link each time a phone
connects. Without that, the takeover is silently undone before any data arrives.

### Constraints on the BLE callback

It runs inside the BLE core event handler, on the BLE stack's own thread, while
that handler holds a mutex. It must not block, must return promptly, and **must
not put large objects on its stack** — that thread's stack is sized for the BLE
stack's own use, and a 1.4 KB local overflows it and hangs the device. All line
buffers live in the heap-allocated `KmApp`; handoff to the main thread is a
single publish flag, not a queue.

## Components

| Path | What it is |
|---|---|
| `fap/` | The Flipper app (C, built with `ufbt`) — `km_ble.c` owns the link takeover |
| `web/` | The phone-side page (Web Bluetooth, no dependencies) |
| `tests/` | Host-compiled unit tests for the pure C core |
| `docs/superpowers/specs/` | Design spec |
| `docs/superpowers/plans/` | Implementation plan |

## Building

The Flipper app:

```bash
pip install ufbt
ufbt update          # fetches SDK + ARM toolchain
cd fap && ufbt       # build
cd fap && ufbt launch  # build, install, and open on a connected Flipper
```

Tests:

```bash
bash tests/run.sh              # C core (base64, layout mapping)
node --test "web/*.test.mjs"   # page framing/encoding
```

`tests/run.sh` auto-detects a compiler. On Windows install one with
`winget install zig.zig` — note that plain `clang` does **not** work standalone
there, as LLVM's Windows build targets MSVC and needs a separate Windows SDK.

## Phone setup

**The page is live at https://ssan9876.github.io/flipper-km/web/**

**iOS:** Safari has no Web Bluetooth. Install [Bluefy](https://apps.apple.com/app/bluefy-web-ble-browser/id1492822055)
(free), open the page in it, then Share → Add to Home Screen.

**Android:** Chrome supports Web Bluetooth directly — open the same URL.

Pairing shows a six-digit passkey on the Flipper's screen. Note the Flipper bonds
with **one phone at a time** — pairing a second displaces the first.

## Security properties

The payload is passwords, so:

- **Never stored.** It lives only in a stack buffer on the Flipper, zeroed after
  typing, on cancel, on timeout, and on disconnect. Never written to the SD card.
- **Never logged.** Flipper logs go out the serial console in plaintext, so the
  payload is never passed to `FURI_LOG_*`. There is a checklist item for this.
- **Encrypted in flight.** Flipper BLE serial requires bonding; unbonded devices
  cannot write to the characteristic.
- **Requires a physical button press.** Nothing is typed without you pressing OK
  on the Flipper. This is what stops a mistimed send putting a password into a
  chat window, and means a stray BLE write can never type on its own.
- **60-second timeout**, after which the buffer is discarded.
- **The page keeps nothing** — no history, no `localStorage`, and the textarea is
  cleared after sending.

### Limitations

- **ASCII only.** USB HID sends key *positions*, not characters. Anything that
  can't be mapped under the active layout aborts the whole string with
  `ERR unmappable@<index>` rather than typing a subtly wrong password into a
  field that shows you dots.
- **Keyboard layout must match the PC.** Defaults to US; select a Bad USB `.kl`
  layout in the app if yours differs.
- **Once bonded, the phone can reach the Flipper's CLI whenever it's in range.**
  That's true of stock firmware generally, not something this app introduces —
  the confirm press is what keeps it from mattering.
