# Native remote player input contract

This is the public implementation summary for Alpha 0.8. It does not replace executable-specific admission or authorize another physical run. Private binary fingerprints, installation receipts and smoke captures are not distributed.

## Implemented path

`RemotePlayerController` publishes portable frames. `remote_player_binding` correlates one frame identity/generation with an owned native record. `native_second_player` implements the slot-1 lifecycle; `native_player_runtime.inl` consumes frames in the admitted phase. `native_player_input_runtime.inl` scopes replay around the actual engine dispatch. The old civil PED and GoToPoint paths are separate.

The factory authenticates slot vacancy, model/streaming, pool capacity, local-player witnesses and the candidate player identity before declaring construction complete. It uses the required native setup, deactivation, positioning, activation and default player-on-foot initialization sequence without SCM execution or destructive space clearing. Unknown partial construction is terminal. Removal requires the matching owned record and cleanup postconditions, not an untrusted player-slot pointer.

## Supported dispatch and input scope

The instruction contract is restricted to the admitted Hoodlum executable; these are not cross-version addresses:

| Contract | Source-level native anchor |
| --- | --- |
| Pre-world lifecycle phase / forwarded original | `0x53C05C` / `0x73A360` |
| Player virtual ProcessControl cell / original | `0x86D190` / `0x60EA90` |
| World dispatch return sites admitted for replay | `0x568561`, `0x568642`, `0x5686F4` |
| Original calling convention | x86 thiscall, entity in ECX, no stack arguments |
| Pad selection | Player type selects native pad 1; pad 0 remains local |

The replay variant publishes only its authenticated virtual dispatch cell and reconciles that owned change during code checks. It leaves the factory-only variant unchanged. The wrapper calls the original at most once per admitted invocation; a legitimate later world dispatch is not treated as a second network task. No original invocation is scheduled from the lifecycle callback.

For the remote only, it captures pad/context witnesses, borrows the 96-byte current/previous pad state, calls the original and restores/verifies the borrowed state. C++ and SEH unwind paths use the restoration scope. Local/foreign entities pass directly to the original. A refused remote scope may forward only with the required live identity/code and restoration checks; failed identity or restore does not authorize an unsafe fallback.

Controls are limited to walking axes in a view-heading basis. Axis conversion uses the current camera orientation instead of writing camera/focus/aim state. The input scope requires the admitted unarmed, untargeted, non-vehicle, non-duck context and the expected task state. It does not synchronize all player contexts or enable combat.

## Concurrency and lifetime

Only copied protocol/controller values cross the network boundary. Creation/removal and scoped input access run in their respective authenticated game-thread phases. Lifecycle operations cannot borrow a nested input scope. Code, state and module residency remain live for outstanding native dispatches; there is no supported hot unload.

Stale controls become neutral. A disconnect ends the remote representation and requests owned cleanup. Unknown identity, ownership, context, reentrancy or restoration is terminal. There is no speculative retry, task substitution or deletion of third-party entities.

## Validation boundary

The October 22, 2026 operator-confirmed smoke demonstrated a persistent second CPlayerPed walking forward and laterally, stopping and being removed on synthetic disconnect while local CJ retained its separate control path. Both original ASIs were restored afterwards.

Closed tests cover transport, ordering, generation, lifetime, context restoration, dispatch count and host memory publication. They do not prove all gameplay states. Snapshots remain references: direct physical reconciliation, multiple remote slots, real GTA input capture, full aim/camera synchronization and two-real-client gameplay remain outside this milestone.

See [architecture](architecture.md), [status](STATUS.md) and [build instructions](BUILDING.md). CoopAndreas and the historical SA-MP client informed the design; their code, hooks and offsets were not imported as GrandMP compatibility contracts.
