# GrandMP Alpha 0.14 - Slice 1: Physical UDP Smoke

Date: 2026-11-04
Baseline: 271f6aa (Alpha 0.13.1)
Branch: feat/alpha014-network-sync

## Environment

- GTA San Andreas 1.0 US Hoodlum
- GTA A receiver PID: 8024
- UDP server PID: 18876
- Synthetic client PID: 18820
- Endpoint: 127.0.0.1:7777
- UDP feature gate enabled for candidate build
- F8 and destructive remote removal disabled

## Physical observations

1. GTA A connected to the UDP server.
2. F6 armed the remote receiver.
3. Without remote input, the second CJ did not appear.
4. Synthetic client sent walk, turn and stop_connected.
5. Second CJ appeared and performed walk, turn and stop.
6. Original CJ remained functional during the test.
7. GTA closed normally.

## Recorded diagnostic markers

- network.receiver_session
- tick.F6_armed
- tick.consume_no_input
- network.remote_controls_seen
- tick.remote_created_before_replay
- tick.live

Recorded network.exception count: 0.

## Evidence

GTA diagnostic:
GrandMPPlayerBootstrap.Diagnostic.8024.01DD6C0A8257C82C.log

SHA-256:
17C77422B8D692E8E310D0C09B9D557E4DA6D5530D6745468151592EE9A9DFBC

Local evidence directory:
build/alpha014-s1-udp-session-20261104-001138/

The synthetic client log records walk, turn and stop_connected.
The physical observations matched this sequence.

## Cleanup

- GTA A closed normally.
- UDP server and synthetic client terminated.
- Four original GTA A files restored from authenticated backup.
- GTA B verified unchanged.
- Evidence preserved before rollback.

Backup directory:
build/alpha014-s1-udp-backup-20261104-000919-e34dc0a3/

## Limitations

This smoke validates one physical GTA receiver and one
synthetic UDP sender.

Two real GTA clients were not synchronized.
No physical F8 removal was tested in UDP mode.
The remote object remains reserved after leave by design.

Long-term stability, leak freedom and arbitrary movement
patterns were not established.
