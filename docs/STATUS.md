# GrandMP status

As of **November 2, 2026**. Physical results below are operator-confirmed isolated smoke results, distinct from automated tests.

## Alpha 0.11.2 native control milestone closed

One independent, pool-owned `CPlayerPed` walked, changed direction and stopped in a single GTA using deterministic local controls and native processing. It does not occupy `Players[1]`. The smoke recorded 339 consecutive completed dispatches, approximately 4.53 m net XY displacement and twelve unchanged local-CJ sample pairs. Semantic recognition of the engine's `LastTimeTouched` update preserved the 96-byte pad writer and all other context comparisons.

GTA A rollback passed and GTA B was untouched, as confirmed by the operator. Removal of the independent entity is **not** established by this collection or by file rollback. Alpha 0.12 prioritizes authenticated lifecycle/removal before reconnecting UDP. See the [Alpha 0.11.2 closeout and plan](alpha-0.11.2-closeout.md).

## Alpha 0.8 milestone closed

The supervised smoke demonstrated local CJ and one additional, persistent `CPlayerPed` simultaneously. The synthetic remote sent controls over UDP. The same remote player walked forward, moved laterally, stopped and remained present until the synthetic client disconnected with Ctrl+C. Removal affected the remote player, the game remained functional, and rollback completed with **Restore=True, OriginalASIs=2/2**.

This validates the narrow path:

`UDP → ObserverNetwork → RemotePlayerController → Native Binding → CPlayerPed → ProcessControl`

The replay uses normal GTA player processing and scoped pad restoration, rather than repeated GoToPoint assignment or direct network pose writes. Physical validation applies to the reviewed experimental variant on the specific admitted Hoodlum executable, not to `GrandMP.asi` generally or arbitrary installations.

## Evidence boundaries

| Capability | Current evidence | Remaining limit |
| --- | --- | --- |
| Earlier native entity lifecycle | Civil PED and cooperative CPlayerPed creation/removal physically observed | Historical variants; their cleanup validation does not transfer to the independent backend |
| Independent native player control | Alpha 0.11.2 creation, forward/lateral walking and stop physically observed | One remote; local deterministic source; independent removal still pending |
| Native task walking | One local and one UDP-commanded walk physically observed in earlier milestones | Legacy experiment, not the main replay controller |
| Persistent on-foot control replay | Forward/lateral movement, stop and disconnect removal physically observed | Synthetic input; narrow unarmed on-foot context |
| Local player isolation | Separate local CJ path and guarded pad restoration | Not proof of every camera, aim or gameplay mode |
| Controller / protocol / lifecycle | Automated value, ordering, failure and loopback coverage | Passing tests do not prove GTA behavior |
| Remote snapshots and Scene interpolation | Implemented as reference/presentation state | Physical correction/reconciliation is not enabled |
| Two real GTA clients | Not demonstrated | Existing capture/relay work still needs integration with the accepted independent lifecycle and physical validation |
| Public networking / gameplay authority | Not implemented | Experimental relay support is not production authentication or authority |
| Android, vehicles, combat, Pawn, modding | Future work | No claim of native or API compatibility |

## Build and release status

Physical options are OFF by default. Experimental ASI/CLEO targets are explicitly selected and isolated; they are not an installable public multiplayer release. Debug builds require matching x86 MSVC Debug dependencies. Public source does not distribute game files, experimental binaries, session evidence, backups or machine-specific installation receipts.

The Alpha 0.11.2 closeout reran **115/115 selected tests** serially in its closed Debug/Win32 configuration, including a read-only audit of the explicitly supplied executable file. Candidate source/artifact pins were verified; no functional code or build was changed for closeout. This is focused regression coverage, not the entire suite or another physical smoke.

The earlier Alpha 0.8 closeout passed 424/424 selected tests in its separate configuration. That dated count is not the Alpha 0.11.2 test selection.

Alpha milestone names describe delivered experiments. The top-level CMake package version remains `0.2.0`; this closeout does not renumber the package or change UDP protocol version identifiers.

See [architecture](architecture.md), [build instructions](BUILDING.md), [roadmap](ROADMAP.md) and [changelog](../CHANGELOG.md). Earlier research notes describe their historical slices; they do not override this current status.
