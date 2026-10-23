# GrandMP roadmap

Updated **October 22, 2026**. Milestones describe demonstrated behavior; future items have no promised dates. This replaces the earlier proposed version-to-feature schedule. In particular, Alpha 0.8 closes native remote input replay, **not** a SA-MP API compatibility release.

## Completed experimental milestones

| Milestone | Delivered behavior | Boundary |
| --- | --- | --- |
| Alpha 0.4 | One visible native PED, controlled removal and ownership lifecycle | Isolated Windows smoke |
| Alpha 0.5 | Native GoToPoint walking, arrival and removal during movement | Local experimental PED |
| Alpha 0.6 | UDP remote join creates a PED; leave removes it | One synthetic remote, loopback |
| Alpha 0.7 FU6 | One relative UDP-commanded native walk with observed arrival | Single task attempt; retained as legacy |
| Alpha 0.8 | Persistent CPlayerPed slot 1, remote input replay, forward/lateral walking, stop and disconnect removal | Operator-confirmed October 22, 2026; synthetic source and exact Hoodlum build |

The portable continuous-task coordinator and pose model were tested offline. They do not represent physically enabled continuous task reassignment or pose correction. The main architecture is now **remote CPlayerPed + input replay + native processing + snapshots for future reconciliation**.

## Next functional increments

1. **Real local input source.** Capture and transmit the supported on-foot controls from a real client without disturbing local input. Preserve the existing controller, generation and lifecycle contracts. Completion requires a real source, not another synthetic demonstration.
2. **Two real GTA clients.** Demonstrate both players seeing each other walk, turn, stop and leave. Keep initial connectivity explicit and constrained; LAN/public access requires a separate networking/security scope. Test loss, stale input, reconnect and local-player independence before claiming general synchronization.
3. **Snapshot reconciliation.** Establish a verified native correction contract and bounded policy for divergence. Do not silently introduce position/matrix/velocity writes or treat simulated postconditions as engine guarantees. Measure results before choosing correction thresholds.
4. **More remote players.** Authenticate capacity, allocation, per-player control contexts and cleanup before expanding beyond the original cooperative slot. Slot arithmetic from another project is not evidence of native capacity.

Each increment needs focused regressions, explicit supported executable/ABI, failure behavior, separate physical approval and reversible installation. A native guard failure blocks the affected operation; retries or unknown entity deletion are not substitutes for a contract.

## Product capabilities still ahead

| Track | Intended outcome | Acceptance dependency |
| --- | --- | --- |
| Gameplay authority | Server validates ownership, movement rules and gameplay actions | Real clients and an explicit authority/security model |
| Vehicles and combat | Driver/passenger lifecycle, weapons, damage, death and respawn | Per-capability native contracts and authoritative state |
| Pawn / AMX | Server scripting, scheduler, bindings and a demonstrable gamemode | Licensed toolchain/VM, domain APIs and bounded execution |
| SA-MP-inspired API | Published, tested compatibility profiles | Verified behavior per item; no implied wire or plugin ABI compatibility |
| Modding / assets | Owned/licensed resources, manifests, cache and streaming | Integrity, quotas, format/platform contracts and lifecycle |
| Android | Native Android client using the same portable core/server | Edition/ABI-specific adapter and real device validation |
| Stable release | Reproducible installation, documented scope and supported configurations | End-to-end tests, recovery, performance and security evidence |

Windows and Android remain first-class design targets for shared APIs and data. Windows offsets, layouts and loaders are never assumed valid on Android. A functional Android client is not delivered by the current milestone.

## Release gates

- Keep portable protocol/domain logic in `shared/` and native behavior in the platform adapter.
- Publish supported capabilities and limitations without promoting stubs or fake backends to gameplay support.
- Keep experiments OFF by default and never co-install variants sharing native hook sites.
- Validate code identity, calling convention, thread affinity, ownership and cleanup for every new native operation.
- Preserve isolated rollback and keep game files, private session records and binaries out of source control.
- Treat public networking, scripting compatibility and stable release as separate acceptance decisions.

See [current status](STATUS.md), [architecture](architecture.md) and the [compatibility policy](compatibility/README.md). No future slice is started automatically by this roadmap.
