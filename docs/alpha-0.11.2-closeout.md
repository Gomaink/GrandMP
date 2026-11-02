# Alpha 0.11.2 — Independent native control closeout

Closed on **November 2, 2026**, after an operator-approved, single-GTA smoke on
the admitted GTA SA 1.0 US Hoodlum executable. This is a native control milestone,
not a two-client multiplayer release. Private receipts, binaries and raw evidence
remain outside public source control.

## Physically validated scope

- One pool-owned, independent `CPlayerPed` with the CJ model, alongside local CJ.
  The remote is not registered in cooperative `Players[1]`.
- Authenticated CLEO scheduling for creation and the owned object's private
  virtual dispatch for native processing. No inline CALL publication or shared
  player-vtable replacement is used by this candidate.
- A deterministic local source commands forward walking, lateral input and then
  neutral input. The operator observed walking, a direction change and stopping.
- The sealed observation window contains **339 consecutive completed native
  dispatches**, first serial 1 through 339, and all three control phases.
- Twelve remote/local sample pairs show approximately **4.53 m net XY displacement**
  between the first and last rounded remote positions. This is displacement,
  not total distance travelled or a general movement accuracy guarantee.
- All twelve local pairs report unchanged local state during the sampled remote
  calls and completed pad apply/restore. Local CJ remained stable in the samples.
- Neutral input began at about 4 seconds. The last three logged remote positions,
  from approximately 4.53 through 5.55 seconds, are equal at log precision, with
  zero reported moving shift. The operator independently confirmed the stop.
- GTA A rollback passed; GTA B was not modified by this smoke, as confirmed by
  the operator.

The source XYZ is a fixed reference. UDP, physical Scene presentation, direct
pose/velocity writes, repeated GoToPoint assignment and cooperative player
registration are absent from the enabled proof path. Position changes between
samples and visual confirmation establish this result; per-call `dxy=0` does not
mean that the character remained stationary between frames.

## Semantic pad contract

The candidate still borrows/restores only the 96 NewState/OldState bytes of pad1.
All other 208 tail bytes must remain identical. Only `LastTimeTouched` may change,
after a normal original return, with complete error-free clock reads on the same
native thread, equal clocks, and the full new uint32 matching that clock without
regression. No new write permission, protection change or timestamp restoration
is introduced. The engine retains ownership of this activity history.

Unchanged tail state keeps the exact-equality path. Unknown changes, partial
reads, inconsistent clocks, reset/wrap in the exception, ownership loss and failed
restoration remain terminal. `Binding` returns to `live` only after the remaining
post-original identity checks succeed. No retry or automatic recreation is added.

The bounded worker emits copied values after the batch is sealed. Capture thread
and emission thread are distinct; log write times are not gameplay sample times.
Counts and local witnesses support this narrow test, not every camera/aim/global
state, collision case or long-running session.

## Limits and unclosed acceptance

- One GTA, one remote lifetime, local deterministic controls, unarmed on foot,
  exact executable/CLEO/runtime dependencies. No general compatibility claim.
- No physical UDP integration or two-real-client synchronization in this branch.
  Prior portable/transport tests and the Alpha 0.8 result do not establish that.
- **Removal of this independent player was not demonstrated by this collection.**
  There is no removal event in its log. Successful file rollback after game exit
  is not proof of engine destruction, reference cleanup or pool release.
- Terminal processing stops GrandMP dispatch, not necessarily all native effects
  of an entity still published in the world. Do not force cleanup after unknown
  ownership/restoration or assume that F8 remains admitted after a terminal.
- No reconnect/readmission, multiple remotes, jumping/running acceptance, combat,
  vehicles, reconciliation, Android native validation or production networking.
- Snapshot comparisons are time-local. They do not prove exclusion of external
  writers, complete task destruction or coherent presentation in all engine phases.

Physical options remain OFF by default. The selected target is the existing
`grandmp_independent_native_control_proof`; semantic context is enabled only with
the independent proof, authenticated pad COW and remote-tail diagnostics. No
package/protocol version change follows from the milestone name.

## Alpha 0.12 plan — lifecycle before UDP

The next acceptance milestone is **create → walk/stop → authenticated removal →
remote absent → local CJ/game still functional**, in a single GTA without UDP.
This document plans that work; it does not implement or authorize a new smoke.

1. **Normal removal first.** Reuse the existing CLEO pre-world phase and native
   `remove`/`removed` path. Check owner, key/generation, reference, pool tag, private
   dispatch, resources and absence of an active borrowed control scope before
   destruction. Review world unlinking, registered references and data/group
   teardown. The CLEO scheduler's own pre-world scope is distinct from an active
   pad/control borrow. Do not add task cancellation or an alternative destructor.
2. **Authenticated completion.** After native destruction, never dereference the
   former PED. Correlate authenticated pool metadata, reference state and owned
   resource postconditions with the previous record. No confirmation based only
   on clearing a pointer. A partial/unknown result stays terminal and prevents
   readmission or repeated destruction.
3. **Dispatch/lifetime closure.** Demonstrate that normal future world processing
   no longer reaches the removed entity. A late/stale/foreign invocation must not
   forward the original or use released game memory. Keep module, private table,
   Binding and copied diagnostic storage resident until the existing lifetime
   contract permits release; no hot unload or new hook.
4. **Bounded failures and focused tests.** Cover repeated removal, removal while
   walking, old generation, replaced pool slot/reference, ownership loss, wrong
   phase, reentrancy, partial cleanup and failed restoration. Preserve the current
   neutral-input policy. Processing failure is not permission to destroy an
   entity; unknown cases retain the normal-game-exit fallback.
5. **Separate physical review.** Start with removal after the neutral phase, then
   review removal during walking as its own case. Require disappearance, verified
   cleanup, no further original dispatch and a functioning local CJ. Preserve
   reversible installation and the Alpha 0.11.2 baseline.

Only after lifecycle acceptance should a subsequent slice reconnect the existing
local capture/UDP/Controller pipeline to this independent backend. Disconnect
must then request the authenticated removal path with identity/generation intact.
Do not redesign UDP, revive the cooperative factory, reuse the NO-GO instruction
writer or introduce pose correction merely to make that integration pass.
Keep physical Scene presentation disabled: selecting an older network target
must not silently reconnect its legacy `Api::present` path.

## Review and source-control recommendation

The closeout reruns the selected 115 Debug/Win32 offline regressions against the
unchanged candidate sources. These tests complement, rather than replace, the
physical result. The code/worktree remains uncommitted pending explicit review.

Suggested feature commit: `feat(native): validate alpha 0.11.2 independent player control`.
Review an explicit dependency-complete source/test/documentation list first: the
CLEO runtime, independent factory and several required headers are still untracked,
so the semantic helper alone is not a reproducible commit. Exclude private
installers/receipts unless separately reviewed, and never stage builds, plugins,
game files, logs, backups or evidence archives. A clean-checkout configure/build
is required before approving that final commit scope. No commit is made here.

See [status](STATUS.md), [architecture](architecture.md),
[proof contract](independent-native-control-proof.md) and [roadmap](ROADMAP.md).
