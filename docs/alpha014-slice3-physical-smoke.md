# GrandMP Alpha 0.14 — Slice 3: Physical Smoke

## Status

**Physical smoke: PASS**

Date: 2026-11-04 (local session).

Branch: `feat/alpha014-slice3-input-fidelity`

Baseline: `20a33b7ddb52f4a6893aaf43a1322ffb7f20fba4`

The physical test confirmed that real controls captured from GTA B
reached GTA A and produced movement of an independent remote CPlayerPed.

This is a control-continuity milestone, not position synchronization.

## Implementation

The Slice 3 candidate addresses Controller mutex contention that
previously could produce neutral controls despite fresh input.

The fix retains a revocable copy of the last valid controls when
contention prevents normal consumption.

The original 500 ms freshness deadline is not renewed by retention.

The input-fidelity profile uses capture intervals of at least 100 ms
and send intervals of at least 120 ms.

`GRANDMP_ENABLE_INPUT_FIDELITY` remains OFF by default.

The existing native player factory, ProcessControl, ownership,
readiness, and UDP lifecycle restrictions remain unchanged.

## Offline evidence

Reported selected tests: 147/147 PASS.

Gate-OFF comparison: 22/22 PASS on both baseline and candidate.

Real UDP transport with simulated native reads:

| Metric | Before | After |
| --- | ---: | ---: |
| Published snapshots | 26 | 44 |
| Controller sequences observed | 25 | 42 |
| Active Binding consumptions | 168/171 | 171/171 |
| Contention-generated neutral frames | 3 | 0 |

These are offline measurements, not physical GTA frame measurements.

## Physical environment

Receiver A: GTA San Andreas Hoodlum 1.0 US, PID 19172.

Sender B: separate GTA instance, Sandboxie Classic `GrandMPB`,
PID 19144.

GrandMP UDP server: PID 20064, endpoint `127.0.0.1:7777`.

WindowedMode ASI allowed simultaneous game execution.

Receiver A and Sender B used the separately authenticated
Slice 3 Win32 CLEO candidates.

## Physical observations

1. Both GTA processes started successfully.
2. Both clients established distinct UDP sessions.
3. F6 was armed on the receiver and sender.
4. GTA A created the independent remote CJ.
5. GTA B transmitted actual local controls.
6. The remote CJ visibly walked, turned, and stopped.
7. The observed movement was more consistent than in Slice 2.
8. Both game processes exited normally.
9. No network exceptions were recorded in either diagnostic.

The user confirmed the physical test worked. A short gameplay
recording was reviewed visually.

The recording does not independently establish precise five-second
continuous-input timing or spatial trajectory correspondence.

## Receiver diagnostic

- `network.receiver_session`: 1
- `tick.F6_armed`: 1
- `network.remote_controls_seen`: 1
- `tick.remote_created_before_replay`: 1
- `tick.live`: 1
- `network.exception`: 0

Preserved evidence:

`build/alpha014-slice3-session-20261104-210349/client-logs-20261104-211530/Receiver-A.log`

SHA-256:

`A6ED602DDCA08F14059A1AFD49FC0DFBFBD497A436A51ED0CD929A0C817EC775`

## Sender diagnostic

- `network.receiver_session`: 1
- `sender.F6_armed`: 1
- `network.local_sender`: 1
- `network.exception`: 0

Preserved evidence:

`build/alpha014-slice3-session-20261104-210349/client-logs-20261104-211530/Sender-B.log`

SHA-256:

`EE41DB90F6BE251BCEE5E6B9F8A55E5B9E22FA267337B497DDAB30E6EE6D6A46`

## Rollback

Both GTA processes exited normally.

The UDP server was terminated after evidence collection.

Authenticated rollback backup:

`build/alpha014-slice3-dual-backup-20261104-210227-9004937f`

Original game files restored and verified: 8/8 PASS.

No conflicting Sandboxie CLEO overlay was present.

Git HEAD and index remained unchanged by the physical test.

## Limits and next milestone

This milestone establishes working GTA-to-GTA input transmission
and more consistent native remote control.

It does not establish exact position, heading, animation, or
trajectory synchronization.

No packet-driven teleportation or spatial reconciliation was
implemented.

A subsequent independently gated milestone should investigate
authoritative spatial state and controlled position reconciliation
without weakening native player lifecycle safety.
