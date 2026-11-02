# GrandMP architecture

Current native proof: **Alpha 0.11.2**, November 2, 2026. This describes limited experimental paths, not a production multiplayer guarantee.

## Independent native control proof

The current pool-owned backend creates one independent CPlayerPed with private
player data and a per-object virtual table, without registering Players[1]. Its
physically validated single-GTA path is:

`local deterministic source → RemotePlayerController → Native Binding → independent CPlayerPed → private dispatch → original CPed::ProcessControl → semantic pad-context validation`.

CLEO's authenticated pre-world scheduler handles creation; the world's normal
owned-object dispatch handles controls. This path does not use the legacy inline
pre-world CALL, a global player-vtable hook, UDP or physical Scene presentation.
Only the engine's clock-matched LastTimeTouched update is exempted from exact tail
equality. Local witnesses and all remaining guards stay fail-closed. Native
walking/direction/stop passed physically; independent cleanup remains the next
milestone. See the [closeout and Alpha 0.12 plan](alpha-0.11.2-closeout.md).

## Retained Alpha 0.8 network replay architecture

The sections below describe the historical cooperative-slot implementation. Its
creation/removal and synthetic UDP validation do not transfer automatically to
the independent backend. The portable transport/controller are retained for later
integration after the new lifecycle is accepted.

### Portable state and transport

The C++20 core in `shared/` owns protocol messages, roster identity, the remote Scene and `RemotePlayerController`. The loopback UDP server relays validated on-foot samples containing controls, position, velocity and orientation. Position-only messages remain supported. The server is not an authoritative simulation, and endpoint/session checks are not cryptographic authentication.

```text
Synthetic client → UDP server → ObserverNetwork → roster / RemotePlayerScene
                                                    ↓
                                          RemotePlayerController
                                                    ↓ copied frames
                                             Native Binding
                                                    ↓
                                          Native Player Factory
                                                    ↓
                                          CPlayerPed in slot 1
                                                    ↓
                                scoped pad replay → original ProcessControl
```

`ObserverNetwork` runs in the experimental ASI as well as in the closed inspection host. No inter-process bridge is required for this path. The controller validates recipient session, player identity, generation, sequence and finite/bounded values. It exposes copied frames through a nonblocking read. Input expires after 500 ms and becomes neutral; session loss and roster removal invalidate the representation. Scene interpolation remains separate from replayed controls.

### Windows native boundary (Alpha 0.8)

The binding admits one remote player and keeps network identity separate from native slot, pool handle, registered reference and ownership. The non-destructive factory uses the original cooperative **slot 1**, verifies its availability and completes model loading, player setup, world/lifecycle initialization and the default on-foot task. It does not execute SCM bytecode or call `ClearSpaceForMissionEntity`. Slot 0 belongs to local CJ; slot expansion is not implemented.

Creation and removal use the admitted pre-world phase at `0x53C05C`, forwarding the original `0x73A360`. These addresses are specific to the supported executable, not portable APIs. Engine access requires exact executable/code identity, thread/phase checks, valid pool/reference ownership, module residency and explicit experimental admission. Network workers never call the GTA engine.

Input is consumed around the remote entity's **normal** `CPlayerPed::ProcessControl` dispatch. The wrapper forwards the original at most once per admitted invocation; it does not schedule an extra control tick from the pre-world callback. Local and unrelated entities retain their original dispatch. The remote scope saves, applies and restores pad 1 state, including failure paths. Pad 0, player focus and camera state are not written by replay; relevant local context is checked for consistency. Movement is limited to the authenticated unarmed on-foot context.

The game performs locomotion, animation and physics. Network position, velocity and heading are reconciliation references, not native write commands. There is no per-packet `SetPosn`, `SetMatrix`, velocity write or sequence of GoToPoint tasks in the player replay path. See the [native input contract](native-player-input.md) for dispatch and restoration boundaries.

### Lifecycle and failure policy (Alpha 0.8)

The same remote identity/generation remains bound while snapshots continue, including neutral input after stopping. Leave requests removal of that owned entity on the game thread. Unknown ownership, partial construction or identity failure is terminal and must not trigger speculative deletion or automatic recreation. Removal is correlated with the owned native record and verified cleanup; a populated player slot alone is not proof of successful construction.

Physical targets are separate, `EXCLUDE_FROM_ALL` and disabled by default. Environment variables alone cannot enable them. Experimental variants sharing native dispatch sites must never be co-installed. Module residency is intentional; shutdown and file restoration require the game to be closed, not a hot unload.

## Legacy and closed research

Civil PED spawn, GoToPoint walking and one-walk remote experiments remain isolated for regression and diagnosis. The portable continuous-task coordinator, reassignment guards and pose-presentation model are closed research components. Their fake-backend results do not establish safe native task replacement or an authenticated physical `applyPose` backend. They are not the main remote-player architecture.

## Platform scope

The Windows adapter is limited to the exact admitted GTA SA 1.0 US Hoodlum executable. No compatibility is inferred for Compact, other PC revisions or Android. Windows and Android share portable domain/wire contracts; Android requires its own native adapter and device validation. See [status](STATUS.md), [build instructions](BUILDING.md) and [roadmap](ROADMAP.md).
