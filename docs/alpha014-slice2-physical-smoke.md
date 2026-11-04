# GrandMP Alpha 0.14 - Slice 2: Real GTA-to-GTA Input Smoke

Date: 2026-11-04
Baseline: e60bfca (Alpha 0.14 Slice 1)
Branch: feat/alpha014-slice2-real-client

## Environment

- GTA San Andreas 1.0 US Hoodlum
- GTA A: UDP Receiver, PID 6000
- GTA B: Real Input Sender, PID 17288
- GrandMP UDP server: PID 15944
- Endpoint: 127.0.0.1:7777
- GTA B isolated using Sandboxie Classic (GrandMPB)
- WindowedMode ASI retained for simultaneous execution

## Physical observations

1. Both GTA processes ran simultaneously.
2. GTA A established its receiver session.
3. GTA B established a separate UDP session.
4. F6 armed the receiver in GTA A.
5. F6 armed local input capture in GTA B.
6. GTA B transmitted real player input.
7. GTA A received remote controls and created a remote CJ.
8. Remote CJ movement was observed in the video recording.
9. The remote movement did not accurately reproduce the full
   sender trajectory or world position.
10. Both games closed normally and were restored.

## Recorded events

Receiver A:
- network.receiver_session
- tick.F6_armed
- tick.consume_no_input
- network.remote_controls_seen
- tick.remote_created_before_replay
- tick.live
- network.exception: 0

Sender B:
- network.receiver_session
- sender.F6_armed
- network.local_sender
- network.exception: 0

## Authenticated evidence

Receiver A diagnostic SHA-256:
89EBA4F00F9B1DF355D64218AC10637F2B489AADD9DEA3878B40EA9C9FD026D9

Sender B diagnostic SHA-256:
BF019764C5D8F4F684EED7C24D6300BF33D2FE4FC60DC4A6DF911BB4BA75625A

Local evidence directory:
build/alpha014-slice2-session-20261104-194034/

A video recording was reviewed separately in the development
conversation; it is not part of this commit.

## Cleanup

- GTA A and GTA B closed normally.
- UDP server terminated.
- Both original CLEO binaries restored.
- All eight baseline GTA files verified.
- No conflicting Sandboxie CLEO overlay found.

Authenticated backup:
build/alpha014-slice2-dual-backup-20261104-192801-6de5524d/

## Limitations

- One physical two-client smoke.
- Input synchronization was demonstrated, not accurate
  world-space position synchronization.
- No bidirectional synchronization, vehicles or combat.
- F8 and destructive remote removal remain disabled.
- No proof of prolonged stability or leak freedom.
- Receiver remote object remains reserved after leave.
