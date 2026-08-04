# Spike 01: BLE reachability from iPhone

**Date:** 2026-08-04
**Question:** Can Bluefy on an iPhone discover, pair with, and reach the BLE
serial service on a Flipper Zero?
**Result: PASSED** — but not on the first attempt.

## Why this was a gate

Every other assumption in the project had prior working evidence somewhere
public. This one did not, and it had no cheap fallback: if iOS could not reach
the Flipper from a web page, the phone-side component would have needed to be a
sideloaded native app, which changes the spec rather than the code.

## What failed first

The device picker came up **empty** — the Flipper never appeared.

Cause: the page requested devices with `filters: [{ services: [SERVICE] }]`.
Web Bluetooth matches that filter against the device's **advertising packet**,
and the Flipper does not advertise its serial service UUID. The service is only
discoverable after connecting. So the filter could never match, no matter how
close the devices were.

## The fix

Two changes, both in `web/index.html`:

1. **Filter by `namePrefix: 'Flipper'`** instead of by service. A
   "Show all devices" fallback using `acceptAllDevices` covers Flippers renamed
   to something else.
2. **Declare `optionalServices: [SERVICE]`.** This was a latent second bug that
   had not yet been reached: when a service is not named in the filter, Web
   Bluetooth blocks access to it unless it is listed in `optionalServices`, and
   `getPrimaryService` throws a `SecurityError` *after* an otherwise successful
   connect. Fixing only the picker would have surfaced this immediately.

## Confirmed working

- Bluefy (free, App Store) discovers the Flipper by name
- Pairing completes with the six-digit passkey shown on the Flipper's screen
- `getPrimaryService` on `8fe5b3d5-2e7f-4a98-2a48-7acc60fe0000` succeeds
- TX and RX characteristics resolve; the page reports `Connected to Flipper …`

## Consequence for the design

The core architectural bet holds: a web page on an unmodified iPhone can reach
the Flipper's BLE serial link, so no native app is required. The spec needs no
revision.

Still unproven at the time of writing: whether commands written to TX are
actually executed by the CLI shell. That is Task 10b.
