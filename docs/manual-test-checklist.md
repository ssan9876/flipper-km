# Manual test checklist

Hardware paths that cannot be unit-tested. Run before any release.

Base64 vectors below are pre-verified, so a mismatch means a real defect
rather than a bad test value.

## Setup

Order matters: the app restarts the BLE core when it launches, dropping any
existing connection.

- [ ] Flipper connected to the PC by USB, Bluetooth on
- [ ] **Open KM Bridge on the Flipper first**
- [ ] *Then* connect from the phone; Flipper shows `BLE: phone connected`

## Happy path
- [ ] Paste `hello world`, tap Send, press OK on the Flipper → typed correctly, page shows `OK`
- [ ] Paste a password containing `` #$%^&*~[]{}|\ `` → every character typed correctly
- [ ] Paste a 1000-character string → typed completely, nothing dropped

## Safety
- [ ] Send, then press Back on the Flipper → nothing typed, page shows `ERR cancelled`
- [ ] Send, wait 60 s → nothing typed, page shows `ERR timeout`
- [ ] Send, then exit the app before confirming → nothing typed, no crash
- [ ] Exit the app → USB returns to serial (a COM port reappears on the PC)
- [ ] Disconnect the phone mid-prompt → Flipper does not crash, buffer discarded at timeout
- [ ] Reconnect the phone after a disconnect → `BLE: phone connected` again, and a send
      still works. This exercises the per-connection re-claim; without it the firmware
      silently takes the link back and nothing arrives.

## Error paths

**There is no USB CLI while the app is running.** `usb_cdc_single` and `usb_hid`
are mutually exclusive USB modes, and the app claims HID at startup. Everything
below goes over BLE from the phone.

Reachable through the page's normal textarea:

| What you paste | Expected reply on the phone |
|---|---|
| Text containing an emoji or accented character | `ERR unmappable@<n>`, nothing typed |
| More than 1024 characters | `ERR toolong`, nothing typed |
| Anything, with USB unplugged | `ERR nohost` |
| A second send while a prompt is already pending | `ERR busy` |

- [ ] All four rows produce the expected reply and type nothing

`ERR badb64` and `ERR badcmd` are not reachable from the page, which only ever
emits well-formed `kmtype <valid base64>`. They guard against corruption on the
wire rather than user input, and are covered by the host-side base64 tests.

## Layout
- [ ] Select a non-US `.kl` layout, confirm it persists across an app restart
- [ ] With the PC set to the matching layout, confirm symbols type correctly

## Hygiene
- [ ] `grep -rn "FURI_LOG" fap/` shows no call taking the payload buffer
- [ ] After a send, nothing resembling the payload appears under `/ext/apps_data/km_bridge/`
