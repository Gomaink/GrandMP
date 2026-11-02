# Alpha 0.11 — Independent native control proof candidate

The Alpha 0.11.2 derivative passed a supervised single-GTA smoke on November 2,
2026: independent remote walking, direction change and stopping were visually
confirmed, with 339 completed dispatches and stable sampled local CJ state.
This validation applies to that reviewed derivative and executable, not every
earlier Alpha 0.11 candidate. See the [closeout and limits](alpha-0.11.2-closeout.md).
Independent removal and two-client UDP integration remain unvalidated.

Alpha 0.11.2 adds the OFF-by-default `GRANDMP_ENABLE_SEMANTIC_PAD_CONTEXT` option,
requiring the independent proof, `GRANDMP_ENABLE_AUTHENTICATED_PAD_COW` and
`GRANDMP_ENABLE_REMOTE_TAIL_DIAGNOSTIC`. It selects the same existing target with
its own `independent-native-control-proof-semantic-pad` output directory. Only a
fully clock-matched native `LastTimeTouched` update is recognized; the other 208
tail bytes and the 96-byte write/restore contract remain protected. No UDP or pose
presentation is enabled. The details below describe the retained proof design;
historical build/evidence paths are not interchangeable installation receipts.

## Scope and execution

```
authenticated CLEO script / pre-world scheduler
    -> deterministic local source -> existing RemoteRoster / RemotePlayerController
    -> existing Binding::consume -> Alpha 0.10.2 pool-owned factory
World's real owned-object dispatch -> private table -> Binding::process
    -> capture context -> borrow pad1 -> original CPed::ProcessControl once
    -> finally restore pad1 -> verify context -> copied deferred evidence
```

The factory, reference/pool identity, group, private player data, private vtable,
world publication and removal sequence are unchanged. `Players[1]` is still a
vacancy/witness check because pad1 must not belong to a native cooperative player;
no player is registered there. CJ keeps its original vtable and pad0. No shared
HookCell, inline CALL/JMP, additional scheduler hook, GoToPoint, SCM CREATE_PLAYER,
ClearSpaceForMissionEntity, or control-triggered task allocation is introduced.

F6 admits one lifetime. The local source commands forward walking for two
monotonic seconds, lateral walking for two, then neutral input. After another
two seconds the observation window closes; fresh neutral samples continue until
F8 uses the existing authenticated removal. The source cannot restart. Missing
updates retain Binding's existing 500 ms input timeout. Invalid code, context,
ownership, application or restoration remains terminal; no recovery is added.

Source XYZ stays fixed as a spawn/reference value. Initial placement is selected
once with the existing 2.5 m spawn probe and revalidated by the unchanged factory.
**The proof never calls `Api::present`**, even though the legacy methods remain in
the shared source. No post-spawn pose/orientation/velocity/animation setter is
connected. UDP startup, local network capture and ObserverNetwork are excluded
from this target; there is no server or synthetic client requirement.

## Pad refusal and context evidence

`writePadStates` preserves its original order and acceptance criteria: context,
VirtualQuery, committed IMAGE, exact allocation base, non-executable RW or COW,
overflow/region containment, 96-byte write, exact 96-byte readback and equality.
`PAGE_EXECUTE_WRITECOPY` remains refused. No page protection is changed.

Optional observations copy only the existing query/result: first stage, actual
Win32 error on API failure, page state/type/protection/allocation protection,
base-match boolean, query size, write/read transfer counts and success flags.
Application and restoration have separate records. Predicate refusal with error
zero is not proof that the write occurred. No pad bytes or addresses are logged.

In this target only, the otherwise untouched tail of pad1 is also a read-only
witness. Restoration writes only the original 96 bytes, then compares the full
pad0, pad1 state/tail, focus, camera mode and heading. A changed tail is terminal,
not repaired speculatively. The old Alpha 0.10 target retains its original layout
and checks when the new gate is off.

At most 16 dispatch-boundary samples (two lines each) are retained in a resident
fixed batch. Sampling occurs on phase changes or at least 500 ms apart. Before
and after snapshots reauthenticate pool/reference, generation, private table,
local identity and matrix continuity. Records contain remote/local positions,
heading, health, PED/move state, moving shifts, serial and elapsed milliseconds.
The local before/after equality is reported, alongside pad write/restore stages.

These are boundaries of Binding's actual world dispatch, **not** probes directly
around UpdatePosition. Move state and shifts are animation evidence, not proof
of a rendered animation clip. The operator must observe the gait and local CJ.
XYZ changes alone do not establish coherent physics or RenderWare presentation.

The existing worker is replaced by a deferred log-only worker in this target.
It reads the batch only after release/acquire sealing; no GTA pointer reaches it.
No producer completion, three seconds without an active producer heartbeat, or
180 seconds without completing preparation is inconclusive. Timeout requests
stop but never frees in-flight storage. Module, API, Binding and batch remain
resident until process exit. A collected history explicitly says it is not
physical proof. It never authorizes another task or another lifetime.

## Build and manual acceptance boundary

`GRANDMP_BUILD_INDEPENDENT_NATIVE_CONTROL_PROOF=OFF` by default. ON exposes only
`grandmp_independent_native_control_proof`, EXCLUDE_FROM_ALL, with isolated output
`client/windows/independent-native-control-proof/Debug/GrandMPPlayerBootstrap.cleo`.
It cannot be selected with either previous CLEO player target.

The target defines POOLED_REMOTE_PLAYER, CLEO_PLAYER_SYNC and
INDEPENDENT_NATIVE_CONTROL_PROOF to 1. NATIVE_LOCAL_CAPTURE,
NATIVE_SECOND_PLAYER_EXECUTION and NATIVE_INPUT_REPLAY are 0. Other physical
options remain off. The shared sync macro selects admitted runtime seams; it
does not enable network code in this variant. The existing opt-in
`GRANDMP_CLEO_BOOTSTRAP_ENABLE=1`, exact Hoodlum/CLEO identities, paths, script
image/name, stack, thread/caller, streaming and module pinning remain required.

The plugin is a CLEO module, not an ASI with InitializeASI. Keep the exact
authenticated 193-byte script. Never coinstall previous variants. Debug requires
the x86 MSVC Debug runtime. Review artifacts, hashes and commands are in
`build/alpha11-independent-native-control-review/`.

Only after independent review and separate authorization: use one restored,
authenticated installation, no server or other GTA, open flat space, unarmed CJ,
and one F6. Require real displacement, visible walking, a change of direction,
then a genuine stop while local CJ stays independently controllable. Native
context witnesses must remain equal. F8 must remove only the owned remote.
Creation, `processed`, or `history.collected` alone is insufficient acceptance.

Any terminal/unknown condition, crash, local interference, absent movement or
incomplete history means stop without retries. Do not force cleanup after lost
ownership/restoration; close normally without saving, preserve logs, then restore
only the authenticated files/timestamps from that test's own installation receipt.
Never restore while a module is loaded or overwrite an unknown file. No installer,
installation or GTA execution is performed in this slice.
