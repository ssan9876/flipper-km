# Manual test checklist

Hardware paths that cannot be unit-tested. Run before any release.

Base64 vectors below are pre-verified, so a mismatch means a real defect
rather than a bad test value.

## Setup
- [ ] Flipper connected to the PC by USB, Bluetooth on, KM Bridge app open
- [ ] Phone paired, page open, "Connected" shown

## Happy path
- [ ] Paste `hello world`, tap Send, press OK on the Flipper → typed correctly, page shows `OK`
- [ ] Paste a password containing `` #$%^&*~[]{}|\ `` → every character typed correctly
- [ ] Paste a 1000-character string → typed completely, nothing dropped

## Safety
- [ ] Send, then press Back on the Flipper → nothing typed, page shows `ERR cancelled`
- [ ] Send, wait 60 s → nothing typed, page shows `ERR timeout`
- [ ] Send, then exit the app before confirming → nothing typed, no crash
- [ ] Exit the app, then invoke `kmtype` over the USB CLI → unknown command, **no crash**
      (this is the dangling-CLI-command bug the teardown ordering prevents)
- [ ] Disconnect the phone mid-prompt → Flipper does not crash, buffer discarded at timeout

## Error paths
Run these over the USB CLI with the app open:

| Input | Expected reply |
|---|---|
| `kmtype` (no argument) | `ERR badb64` |
| `kmtype QQ=` | `ERR badb64` |
| `kmtype w6k=` (UTF-8 `é`) | `ERR unmappable@0` |
| `kmtype YcOp` (`a` then `é`) | `ERR unmappable@1` |
| base64 over 1368 chars | `ERR toolong` |

- [ ] All five rows produce the expected reply and type nothing
- [ ] Unplug USB data, send over BLE → `ERR nohost`

## Layout
- [ ] Select a non-US `.kl` layout, confirm it persists across an app restart
- [ ] With the PC set to the matching layout, confirm symbols type correctly

## Hygiene
- [ ] `grep -rn "FURI_LOG" fap/` shows no call taking the payload buffer
- [ ] After a send, nothing resembling the payload appears under `/ext/apps_data/km_bridge/`
