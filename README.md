# DPS Field

Pebble watch companion for a DPS field controller that is already running on the same phone.

This repository is original work: the Emery watch app, plus `FieldSnapshot` and `LocalFeed`. It does not contain the Dead Packet Society firmware or Android application.

## Upstream projects

These repositories are public and have no license file:

- https://github.com/Dead-Packet-Society/DPS-DIY-CLUSTER
- https://github.com/Dead-Packet-Society/DPS-DIY-CLUSTER-APP

GitHub's Terms of Service allow other GitHub users to view those repositories and to fork them on GitHub. That is not a license to modify the code or to redistribute it. This project does not fork or republish either repository.

Build the phone app from your own local copy. Copy `phone/FieldSnapshot.kt` and `phone/LocalFeed.kt` into `com.deadpacketsociety.fieldcontroller`. Those two files are covered by the MIT license in this repository. The rest of the phone app stays under whatever terms apply to the upstream project.

## Phone feed

`LocalFeed` listens on `127.0.0.1:4774` and serves JSON. The watch polls that address every 5 seconds. The feed stays on loopback.

Wire the snapshot from the field app's own session logic:

- `beginSession()` when a capture starts
- `setScanning(false)` when capture stops or the controller link drops
- `noteWifi(...)` or `noteBle(...)` when a new unique observation is kept
- `noteNodeReport(...)` when a node status object arrives
- `syncNode(...)` when a node's id and online flag are refreshed
- `noteLog(...)` for each communication-monitor line
- `LocalFeed.start()` when the activity starts, and `stop()` when it is destroyed

The watch shows unique units, time since the last new unit without seconds, the session average per minute, unique 2.4 GHz and 5 GHz Wi-Fi, and unique Wi-Fi versus Bluetooth. Hold select on the node list for one node's detail. Back returns to page changes.

## Build

The watch app targets Emery (Pebble Time 2), same SDK layout as other Pebble packages in this setup:

```bash
pebble build
```

The bundle is `build/dps-field.pbw`.

## License

The MIT license in `LICENSE` applies only to the code in this repository. It does not license the Dead Packet Society firmware or field-controller application.
