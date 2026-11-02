# Alpha 0.9: real-client on-foot synchronization candidate

Status: integrated source, closed Debug/Win32 validation; **no two-GTA physical validation yet**. Alpha 0.8 remains the physically validated baseline.

## Implemented path

```text
local CJ / native pad 0 (existing pre-world callback)
    → slot/pool/tag + gameplay/code/context authentication
    → copied LocalPlayerSnapshots value, bound to this UDP recipient session
    → existing UDP worker / ObserverNetwork::tickLocal / PlayerOnFoot
    → UDP server, loopback by default / explicit private LAN interface (exclude sender)
    → other ObserverNetwork / RemotePlayerController
    → existing Native Binding / CPlayerPed slot 1 / original ProcessControl
```

No new hook, task scheduler or worker was added. The factory, remote replay scope, pad-1 restoration and ownership/removal algorithms are retained. The network protocol is unchanged. Generation is the receiver's existing identity/lifetime key, not a newly invented wire field. Session IDs, endpoint checks, recipient-bound input, sequence checks and roster tombstones keep old lifetimes separate.

## Capture and publication

Capture runs only in the admitted pre-world phase, outside any remote pad borrowing. `Api::localIdentity` reuses the existing pool/tag and slot-0/playerData checks. Identity and matrix are rechecked after integral reads; a changed local identity stops the source without retry. No GTA pointer enters the mailbox or worker.

The collector reads the existing native pad-0 current-state words. It inverts the already authenticated walk-axis scale, bounds diagonal input and publishes walking only when the walk modifier is present. Running, jumping, ducking, targeting, weapons, vehicles, disabled input or a foreign foreground window do not become supported controls: the emitted control value is neutral. No local input, pad, focus, camera, animation or transform is written.

Position comes from the verified local matrix; heading comes from its horizontal forward basis, and view heading uses the replay's existing camera-heading field. Snapshot velocity is **measured displacement divided by monotonic seconds**, not a raw `m_vecMoveSpeed` value with an assumed timestep conversion. The first sample warms this measurement; gaps over 500 ms require another warmup. References exceeding the existing finite/bounds contract are refused. Capture is pre-world observation, not an assertion of atomic before/after-physics state.

One resident, fixed-size latest-value mailbox coalesces captures. Both publication and consumption use try-locks; neither blocks the game thread. The existing UDP worker owns the session fence and sends at most 5 Hz, consuming each revision once. Captures older than 200 ms, invalidated captures and previous-session values are refused. Missing callbacks stop fresh emission; remote controls expire through the existing 500 ms timeout. Already transmitted UDP packets cannot be recalled.

Only values cross threads. World position, velocity and heading remain references: this increment does not physically correct divergence or align the remote to arbitrary UDP coordinates.

UDP v1 still has no sender timestamp: receive-side freshness is not an end-to-end packet-age proof. A delayed higher sequence in the same session can still arrive late; the local capture fence does not recall in-flight datagrams. No stronger latency/authentication guarantee is claimed.

## Target isolation

`GRANDMP_BUILD_NATIVE_PLAYER_SYNC_EXPERIMENTAL=OFF` by default exposes `grandmp_native_player_sync_experimental` only when explicitly requested. It is `EXCLUDE_FROM_ALL`, with isolated `native-player-sync/Debug/GrandMPNativePlayerSyncExperimental.asi` output. The target selects the existing CPLAYER, factory execution, physical spawn and input replay defines plus `GRANDMP_NATIVE_LOCAL_CAPTURE_BUILD=1`.

Alpha 0.8 targets do not select local capture. Existing physical options are not prerequisites. `grandmp_native_player_sync_compile` compiles the real new runtime as an object target with physical spawn/factory execution disabled. No enabled ASI was built, loaded or installed in this step.

The new variant alone admits two explicit lab roots: the existing `D:/Jogos/GrandMP-Smoke` and a separate `D:/Jogos/GrandMP-Smoke-B`, each with the new ASI in its own `scripts` directory and the exact previously admitted Hoodlum executable. The roots are not created or modified by this work. Original variants retain their original path checks. Exact executable hash, image base, code windows, opt-ins, F6, thread/phase and module residency remain required. Never co-install variants using the same callback or player dispatch cell.

## Closed evidence

The duplex test launches the real server on an ephemeral loopback port, then exercises two ObserverNetwork instances and the actual `tickLocal` seam with supplied captured values. It checks bidirectional walk/turn/stop, persistent identity, no self echo, input expiry, leave and a new identity/generation after reconnect. It does not simulate a native player or claim real input capture inside GTA.

Focused tests also cover the actual pad codec, measured kinematics, session fencing, age/cadence, coalescing, invalidation, concurrent copies and the reused local slot/pool reader. Existing replay, pad restoration, original dispatch, factory, ownership, controller, protocol and session regressions remain selected.

## Future supervised smoke, not executed

1. Separately review/build the enabled candidate and prepare an authenticated installer/rollback for **both** isolated roots. Existing Alpha 0.8 installers are not suitable for the new name or two-root inventory. Do not copy anything during source validation.
2. Prefer two physical computers, each keeping its own GTA focused. LAN configuration is now implemented, but this does **not** unblock native publication. The current GrandMP writer remains NO-GO and no enabled GrandMP client is prepared by the LAN increment. Do not reuse a prior installer or substitute the CoopAndreas reference client for GrandMP.
3. Use a clean server with exactly these two real clients, both in the same outdoor region, unarmed and on foot. Preserve each original ASI inventory and use no legacy experimental variants. Arm each client with the existing opt-ins/F6.
4. Wait for `local_snapshot_sender_ready` and owned remote creation on both sides. Hold the game's walk modifier: A walks/changes direction/stops while B observes; repeat with B. Both local CJs must remain independently controllable. Native spawning remains nearby with its existing offset; exact snapshot position agreement is not promised.
5. F8 requests owned removal and stops this client's source; the worker sends the existing Disconnect and the peer removes its owned remote. This experimental session is terminal and has no automatic respawn/retry. Abrupt game exit relies on the existing server timeout.
6. Stop on any identity/restore/ownership failure, unsupported context, absent callbacks or local-control interference. Close both games normally before restoring each session's exact original inventory. Preserve separate logs.

The immediate decision is **GO for independent source review**, not physical execution. A safe publication contract for the existing pre-world callback, actual local input capture timing and bidirectional gameplay remain outstanding.

## Explicit LAN endpoint increment

The portable IPv4 value/parser admits only numeric RFC1918 or loopback addresses
and a nonzero port. The server requires an explicit `--bind` to leave its legacy
loopback default. The existing `ObserverNetwork::start(port)` and legacy receiver
parser keep their loopback behavior. The new endpoint overload sends to and
accepts replies only from the configured IP **and** port; no DNS discovery or
wildcard listener is introduced. Alpha 0.9 local-capture workers alone opt into
the new parser for `GRANDMP_REMOTE_PED_SERVER=192.168.1.10:7777`.

No UDP message format, walking controls, native factory, replay, ABI or hook
publication is changed. The compiled Alpha 0.8 artifacts are not rebuilt.
Real UDP tests exercise two actual ObserverNetwork clients through the real
server at `127.0.0.1` and separately `127.0.0.2`, including walk/turn/stop, expiry,
disconnect/reconnect and wrong-source IP/port rejection. Alternate loopback
proves endpoint selection; it is **not** a physical two-computer or GTA test.
See `docs/two-machine-reference-lab.md` for the separate upstream reference lab.

Conceptual references only: [CoopAndreas CollectState](https://github.com/Tornamic/CoopAndreas/blob/main/client/src/CKeySync.cpp#L55-L121) and [player packet handlers](https://github.com/Tornamic/CoopAndreas/blob/main/client/src/PacketHandlers/players.cpp#L7-L69). Their code, offsets and direct pose/velocity writes were not copied.
