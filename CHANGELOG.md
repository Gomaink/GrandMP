# Changelog

Milestone dates refer to development validation, not packaged releases. Game binaries and private smoke records are not distributed.

## Alpha 0.11.2 — 2026-11-02

### Validated physically

- One independent, pool-owned CPlayerPed walked, changed direction and stopped through the native control path in a single GTA, using a deterministic local source.
- The bounded log recorded 339 consecutive completed dispatches, approximately 4.53 m net XY displacement and twelve unchanged local-CJ sample pairs.
- The operator confirmed GTA A rollback and that GTA B was untouched.

### Contract and limits

- Semantic recognition is restricted to a full clock-matched LastTimeTouched update; the other 208 tail bytes, 96-byte pad writer/restoration and terminal guards remain protected.
- No UDP, physical Scene presentation, cooperative player registration, new global hook or direct pose/velocity write is introduced by this proof. Experimental options remain OFF by default.
- Independent removal was not demonstrated by this collection. Alpha 0.12 prioritizes lifecycle and authenticated cleanup before two-client integration. See the [closeout](docs/alpha-0.11.2-closeout.md).

## Alpha 0.8 — 2026-10-22

### Added

- Portable on-foot controls and snapshots carrying position, velocity and orientation through the existing UDP server and ObserverNetwork pipeline.
- RemotePlayerController with player/generation/sequence validation, bounded copied frames, stale-input neutralization and explicit session end reasons.
- Native binding and non-destructive player factory for one owned CPlayerPed in cooperative slot 1, with pool/reference identity and authenticated cleanup.
- Scoped remote pad replay around the original ProcessControl dispatch, including context restoration and local-player isolation checks.
- Closed lifecycle, replay, code-identity, protocol and real loopback pipeline regressions; isolated build gates for native experiments.
- Bounded deferred diagnostics and closed task/pose research components, kept separate from physical player replay.

### Validated physically

- A persistent second CPlayerPed walked forward and laterally, stopped, and was removed after the synthetic client disconnected with Ctrl+C.
- The operator confirmed a functioning game and successful rollback with both original ASIs restored.

### Limitations

- Exact admitted GTA SA 1.0 US Hoodlum executable; one remote, synthetic input, loopback server and limited unarmed on-foot context.
- No bidirectional synchronization between two real GTA clients, physical snapshot reconciliation, slot expansion, vehicles, combat, Pawn or functional Android adapter.
- Physical build options remain OFF by default. The package version remains unchanged.

## Earlier physically validated milestones

- **Alpha 0.7 FU6:** one UDP-commanded relative native walk and observed arrival.
- **Alpha 0.6:** remote join/leave mapped to native PED spawn/removal.
- **Alpha 0.5:** native task walking and safe removal, including during movement.
- **Alpha 0.4:** first visible native PED and owned lifecycle.

See [status](docs/STATUS.md) for evidence boundaries and [roadmap](docs/ROADMAP.md) for future work.
