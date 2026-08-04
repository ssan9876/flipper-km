# Flipper KM Bridge — Design

**Date:** 2026-08-03
**Status:** Approved for planning

## Problem

The author uses 1Password (and similar managers) on their phone, but not on their
computers. Getting a password from phone to PC currently means reading it off a
screen and retyping it by hand.

This project bridges that gap: paste text on the phone, have it typed into the
focused field on the PC. A Flipper Zero sits between them, connected to the PC by
USB where it presents itself as a keyboard.

Primary phone is iPhone. Android support is desirable and comes free with the
chosen approach.

## Scope

**In scope:** sending text from phone to PC as keystrokes.

**Out of scope:** mouse control, trackpad input, key combinations, media keys,
PC-to-phone communication, non-ASCII text. These were considered and cut. The
tool solves one problem well.

## Architecture

```
┌─────────────────┐                    ┌──────────────┐              ┌─────┐
│  iPhone/Android │   BLE (bonded,     │ Flipper Zero │  USB HID     │ PC  │
│  Bluefy/Chrome  │───encrypted)──────▶│ km_bridge.fap│──keystrokes─▶│     │
│  one web page   │  serial service    │              │              │     │
└─────────────────┘                    └──────────────┘              └─────┘
```

Two components. No server, no accounts, no network service.

### Component: `km_bridge.fap`

A Flipper external application, written in C against the stock-firmware SDK.
Three responsibilities:

1. Place USB into HID-keyboard mode on start; restore the previous config on exit.
2. Register a `kmtype` CLI command with `CliCommandFlagParallelSafe`.
3. Display a status and confirmation screen.

### Component: web page

A single self-contained HTML file served over HTTPS from GitHub Pages (Web
Bluetooth requires a secure context). Contains a textarea, a Send button, and a
connection indicator. Intended to be added to the phone's home screen.

## Key technical decision: the CLI, not the raw serial callback

The Flipper's BLE serial service is, by default, wired to the Flipper's CLI
shell. The known-working Web Bluetooth proof of concept
(`EstebanFuentealba/flipper-zero-bluetooth-serial-poc`) demonstrates this: it
writes the literal string `start_rpc_session\r` to the TX characteristic to
switch the link out of shell mode and into protobuf RPC mode. If a client never
sends that, it is talking to a shell.

This design uses that shell.

The considered alternative — taking over the raw serial callback with
`furi_hal_bt_serial_set_event_callback`, as `maybe-hello-world/fbs` does — was
rejected. That repository carries a warning that it may no longer work against
current firmware due to API changes, and a Flipper forum thread documents someone
stuck at exactly that point: the firmware logs `Received 14 bytes` while the
application callback never fires for raw GATT writes. Registering a CLI command
is a documented, supported extension point and avoids that failure mode entirely.

### BLE identifiers

| Purpose | UUID |
|---|---|
| Serial service | `8fe5b3d5-2e7f-4a98-2a48-7acc60fe0000` |
| TX (write) | `19ed82ae-ed21-4c9d-4145-228e62fe0000` |
| RX (notify) | `19ed82ae-ed21-4c9d-4145-228e61fe0000` |
| RPC state | `19ed82ae-ed21-4c9d-4145-228e64fe0000` |

The page enables notifications on RX only, and must **not** send
`start_rpc_session`.

## Data flow

1. User copies a password in their password manager, switches to the page,
   pastes into the textarea, taps **Send**.
2. The page base64-encodes the text and writes `kmtype <base64>\r` to the TX
   characteristic, chunked to fit the negotiated BLE MTU.
3. The Flipper's CLI thread dispatches to the `kmtype` handler, which decodes
   into a heap buffer.
4. The Flipper screen prompts: `Type 24 chars? [OK]`. The user focuses the target
   field on the PC and presses OK.
5. The app types the text over USB HID, zeroes the buffer, and writes `OK` back
   over the serial link.

### Why base64

The CLI is line-oriented. A payload containing a space, `\r`, or a control
character would be mangled or truncated by the command parser. Base64 lets any
byte sequence survive intact, at a 33% size cost that is irrelevant at these
lengths.

### Why a confirmation press

USB HID types into whatever window currently has focus. Without a confirmation
step, a mistimed send puts a password into a chat window or a public document.
The confirm press also means a stray or malicious BLE write cannot cause typing
on its own — it can only queue a prompt the user must physically accept.

### Why it refuses rather than approximates

USB HID transmits key *positions*, not characters; the resulting character
depends on the PC's active keyboard layout. If any character in the payload
cannot be mapped under the selected layout, the app aborts the entire string and
reports the offending index. Typing a partially-correct password into a masked
field produces a failure the user cannot see or diagnose.

## Keyboard layout handling

The app reads Bad USB's existing `.kl` layout files from
`/ext/badusb/assets/layouts/`, defaulting to US. The selection is persisted in an
app settings file (this contains no secrets).

Scope is ASCII printable characters only. Accented characters, emoji, and other
non-ASCII input are rejected at decode time.

## Security

The payload is passwords. Design decisions follow from that.

**At rest: nowhere.** The decoded text exists only in a heap buffer on the
Flipper. It is explicitly zeroed after typing, on abort, on timeout, on BLE
disconnect, and on app exit. It is never written to the SD card.

**Never logged.** The payload must never be passed to `FURI_LOG_*`. Flipper logs
are emitted over the serial console in plaintext. This is a discipline the code
holds deliberately, and a specific thing to check in review.

**In flight: encrypted.** Flipper BLE serial requires bonding, with a six-digit
passkey displayed on the Flipper at pair time. Unbonded devices cannot write to
the characteristic.

**Page holds nothing.** No `localStorage`, no history feature, no service-worker
caching of content. The textarea is cleared after a successful send.

**Confirm timeout: 30 seconds.** After that the buffer is zeroed and discarded,
so a password sent to an unattended Flipper does not persist in RAM.

### Accepted limitations

Once bonded, the phone can reach the Flipper's CLI whenever it is in range. This
is a property of stock firmware, not something this design introduces. The
confirmation press is what prevents it from meaning "any bonded device can
silently type into the PC."

The Flipper bonds with one phone at a time. Pairing a second phone is expected to
displace the first; using both means re-pairing.

## Error handling

Every failure reports back over the serial link rather than failing silently.

| Condition | Behavior |
|---|---|
| No USB host connected | Refuse, reply `ERR nohost` |
| Malformed base64 | Discard, reply `ERR badb64` |
| Character unmappable in active layout | Abort entire string, reply `ERR unmappable@<index>` (zero-based index into the decoded text) |
| Payload exceeds 1 KB | Refuse, reply `ERR toolong`. The limit is 1024 bytes of **decoded** text; the base64 line may therefore reach ~1368 bytes |
| No confirmation within 30 s | Zero buffer, reply `ERR timeout` |
| BLE disconnects mid-transfer | Zero partial buffer, no reply |
| Application exits | `cli_registry_delete_command` **before** teardown, then restore USB config |

The final row is load-bearing. Firmware documentation states that leaving a CLI
command registered after its owning app exits causes a null pointer dereference
and crashes the Flipper when the command is next invoked. Teardown ordering is a
correctness requirement, not cleanup hygiene.

## Testing strategy

BLE stacks and USB enumeration cannot be meaningfully unit-tested on device. The
components most likely to contain defects, however, are pure functions that
compile and run on the development machine with no hardware:

- **base64 decode** — malformed input, padding variants, embedded nulls, length limits
- **layout mapping** — every printable ASCII character to keycode plus modifier, and correct rejection of everything outside that set
- **command parsing** — the `kmtype <payload>` line, including missing and empty arguments

These are built as a host-compiled test target and developed test-first.

The web page's framing and chunking logic gets node-based unit tests. The BLE
connection itself is verified manually.

Hardware behavior is covered by a written manual integration checklist, labeled
as manual rather than counted as automated coverage.

## Milestones

Ordered so that the least-proven assumption is tested first.

| # | Milestone | Retires |
|---|---|---|
| 0 | Bluefy on iPhone pairs with the Flipper and runs `device_info` over the BLE CLI | The one unproven assumption; no C written yet |
| 1 | base64 and layout core, host-tested | Correctness of the typing path |
| 2 | FAP: USB HID mode plus `kmtype` typing a fixed string, driven over **USB** CLI | Decouples HID from BLE |
| 3 | Same command driven over BLE from the web page | End-to-end integration |
| 4 | Confirmation screen, error paths, buffer zeroing | Safety properties |
| 5 | Page polish, GitHub Pages deployment, home-screen icon | Daily usability |

Milestone 0 is a spike costing roughly an hour. If it fails, the fallback is a
sideloaded native app or a paid Web Bluetooth browser (WebBLE), and that is known
before any code depends on the assumption.

## Repository layout

```
D:\flipper-km\
  fap/     application.fam, km_bridge.c, cli_command.c, layout.c, base64.c, ui.c
  tests/   host-compiled tests for the pure core
  web/     index.html
  docs/superpowers/specs/
```

## Build tooling

`pip install ufbt`, then `ufbt update` to fetch the SDK and ARM toolchain matching
stock firmware. `ufbt` builds; `ufbt launch` deploys over USB.

Target firmware is official/stock. The CLI registration API is
`cli_registry_add_command` / `cli_registry_delete_command` (renamed from
`cli_add_command` / `cli_delete_command` in earlier versions); the exact SDK API
version is pinned during implementation.

## References

- Web Bluetooth to Flipper serial, working proof of concept — https://github.com/EstebanFuentealba/flipper-zero-bluetooth-serial-poc
- BLE serial callback approach, rejected — https://github.com/maybe-hello-world/fbs
- Forum thread documenting the raw-callback failure — https://forum.flipper.net/t/trying-to-send-some-bytes-via-ble-to-an-app-on-the-flipper/15746
- CLI command registration from an app — https://resmer.co.za/ch/posts/flipper-app-tamagometer/
- Flipper CLI documentation — https://docs.flipper.net/zero/development/cli
- `furi_hal_bt.h` reference — https://developer.flipper.net/flipperzero/doxygen/furi__hal__bt_8h.html
- HID typing over USB and BLE, prior art — https://github.com/DangerousThings/flipper-wedge
