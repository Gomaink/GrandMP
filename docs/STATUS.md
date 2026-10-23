# GrandMP status

As of **October 22, 2026**. Physical results below are operator-confirmed isolated smoke results, distinct from automated tests.

## Alpha 0.8 milestone closed

The supervised smoke demonstrated local CJ and one additional, persistent `CPlayerPed` simultaneously. The synthetic remote sent controls over UDP. The same remote player walked forward, moved laterally, stopped and remained present until the synthetic client disconnected with Ctrl+C. Removal affected the remote player, the game remained functional, and rollback completed with **Restore=True, OriginalASIs=2/2**.

This validates the narrow path:

`UDP → ObserverNetwork → RemotePlayerController → Native Binding → CPlayerPed → ProcessControl`

The replay uses normal GTA player processing and scoped pad restoration, rather than repeated GoToPoint assignment or direct network pose writes. Physical validation applies to the reviewed experimental variant on the specific admitted Hoodlum executable, not to `GrandMP.asi` generally or arbitrary installations.

## Evidence boundaries

| Capability | Current evidence | Remaining limit |
| --- | --- | --- |
| Native entity lifecycle | Civil PED and additional CPlayerPed creation/removal physically observed | One additional player, native slot 1 only |
| Native task walking | One local and one UDP-commanded walk physically observed in earlier milestones | Legacy experiment, not the main replay controller |
| Persistent on-foot control replay | Forward/lateral movement, stop and disconnect removal physically observed | Synthetic input; narrow unarmed on-foot context |
| Local player isolation | Separate local CJ path and guarded pad restoration | Not proof of every camera, aim or gameplay mode |
| Controller / protocol / lifecycle | Automated value, ordering, failure and loopback coverage | Passing tests do not prove GTA behavior |
| Remote snapshots and Scene interpolation | Implemented as reference/presentation state | Physical correction/reconciliation is not enabled |
| Two real GTA clients | Not demonstrated | Real local control capture and bidirectional validation needed |
| Public networking / gameplay authority | Not implemented | Server is loopback-only relay; no production authentication |
| Android, vehicles, combat, Pawn, modding | Future work | No claim of native or API compatibility |

## Build and release status

Physical options are OFF by default. The replay ASI is an explicitly selected, isolated experiment; it is not an installable public multiplayer release. Debug builds require matching x86 MSVC Debug dependencies. Public source does not distribute game files, experimental binaries, session evidence, backups or machine-specific installation receipts.

The milestone closeout passed **424/424 selected tests** in a fresh Debug/Win32 build with physical execution gates closed: 86 controller/binding/player/replay/identity tests and 338 supporting regressions. Optional proprietary-image file audits were not configured. This is focused regression coverage, not the entire repository suite or another physical smoke.

Alpha milestone names describe delivered experiments. The top-level CMake package version remains `0.2.0`; this closeout does not renumber the package or change UDP protocol version identifiers.

See [architecture](architecture.md), [build instructions](BUILDING.md), [roadmap](ROADMAP.md) and [changelog](../CHANGELOG.md). Earlier research notes describe their historical slices; they do not override this current status.
