# UDP input fidelity - Alpha 0.14 Slice 3

Baseline: `20a33b7ddb52f4a6893aaf43a1322ffb7f20fba4`. Unidirectional real GTA sender B -> existing relay -> receiver A. Physical validation of this increment is pending. No position reconciliation, pose writer, new protocol, background mechanism or native lifecycle change is introduced.

## Findings

Five Hz alone does not truncate a five-second held input. The receiver already accepts the same sequence across successive native dispatches, until its original 500 ms deadline. However, `Binding::process` used to replace fresh controls with neutral whenever its nonblocking Controller read contended with the network producer. This occurred in the real transport test with simulated native reads. It is an observed offline continuity defect, not proof of the exact cause of the earlier physical trajectory.

The sender also had only one send opportunity per revision, captured at >=200 ms and eligible for sending for <200 ms. A failed try-lock publication invalidated a still-valid previous sample. There was little loss/jitter margin: two consecutive losses can separate deliveries by more than 500 ms. Extending receiver timeout would hide lost input; this slice keeps it at 500 ms.

## Isolated policy

`GRANDMP_ENABLE_INPUT_FIDELITY` defaults OFF and requires the existing independent UDP profile. Enable it for both Receiver A and Sender B. It defines `GRANDMP_INPUT_FIDELITY_BUILD=1` only on the existing isolated target, still EXCLUDE_FROM_ALL. Sender B still requires `GRANDMP_ENABLE_REAL_PLAYER_SENDER=ON` and its existing explicit runtime opt-ins. Receiver A remains sender-OFF. Output paths and the authenticated CLEO script are unchanged, in separate new build directories.

Sender B uses the existing native-thread capture at >=100 ms. The copied-value mailbox permits sends no closer than 120 ms (server's 100 ms floor plus one nominal 20 ms worker tick). Late service skips opportunities; there are no catch-up bursts or movement retries. Reusing the latest fresh sample is legitimate repeated input and obtains a new wire sequence in the unchanged transport. Neither reuse nor a publication miss renews its original capture timestamp. It becomes unusable at 200 ms, or immediately on invalidation/session change. Native read/identity/context failures retain their existing invalidation/terminal rules. No worker reads game memory.

Receiver A retains a successfully read input copy only during Controller mutex contention. Retention requires the same Controller instance, player/generation, nondecreasing sequence, an unrevoked epoch and the original receiver timestamp younger than 500 ms. A successful read reporting missing, unknown, stale or expired input remains authoritative and never falls back to the old copy. Clock reversal and epoch saturation deny retention.

Epoch changes are linearized under the existing Controller mutex before publishing session invalidation or ending an entry. All existing removal paths use `rememberEnd`; `beginSession` also revokes before resetting the Scene. There is no epoch wrap. An atomic epoch lookup lets the native consumer reject a revoked copy without waiting for the mutex. Like an ordinary successful snapshot read, this check is not a reservation through the later native call: an already admitted dispatch can overlap a subsequent disconnect. The next observation sees revocation. It never authorizes native ownership, creation, cleanup or readmission.

The original native context/identity/readiness, dispatch serial, capture, pad application, original call, restoration and final authentication still run. Input retention cannot bypass them. The remote stays independent of Players[1]; F6 and UDP's F8/destruction block are unchanged. No destructor, animation, streaming, pad-COW or factory code changes.

Gate OFF uses the legacy 200 ms / one-send-per-revision mailbox and neutral-on-contention Binding policy. Added Controller fields are value-only timestamp/epoch metadata; they do not change the wire representation. Existing OFF tests are compared with the same tests compiled from the pristine baseline.

## Input semantics

The existing conversion is unchanged: raw horizontal /60 maps right, negative raw vertical /60 maps forward; vectors longer than one normalize, including diagonals. Walk uses the existing +0x2A word. Releasing Walk yields neutral; holding Walk without direction yields zero axes with the walk bit retained. These values pass through the same original ProcessControl path. Running, jump, weapons and vehicles are unsupported and remain neutral under the collector's existing guards.

The existing foreground predicate is untouched. Focus loss publishes neutral on the next admitted capture, focus return requires a fresh valid sample. No Sandboxie, focus, WindowedMode or background configuration is changed. Keep B foreground for the primary smoke.

## Evidence boundaries and risks

The test suite reuses the capture body with simulated native reads, the actual UDP server/transport, Controller and Binding with the existing test Api. Five-second holds distinguish successful publications, wire sequences observed in Controller and controls actually applied at native test dispatches. Counts differ because capture, send and native processing have independent cadences. Deterministic codec/SessionTable tests separately inject irregular service and datagram loss. They are not physical movement proof.

The receiver deadline is still 500 ms from the last accepted packet. Source sample age is independently bounded to <200 ms; source age and network latency are not the same as the receiver deadline. The wire has no source capture timestamp, so arbitrary network delay cannot be authenticated as end-to-end freshness by this increment. Repeated fresh packets do not extend a sample indefinitely. Known disconnect/session changes revoke retained receiver input. Readmission and UDP destructive cleanup remain blocked.

Higher input cadence improves input continuity, not spatial alignment. Different initial placement, native simulation, camera context, eligibility guards and drift can still produce divergent paths. Do not infer matching coordinates or a physically resolved trajectory from these tests. Strict readiness can still refuse processing; it is not relaxed here.

## Build and future smoke

Use the Slice 2 build options in `real-player-sender-slice2.md`, add `-DGRANDMP_ENABLE_INPUT_FIDELITY=ON` to both roles, and use fresh directories such as `build/alpha014-s3-receiver-win32` and `build/alpha014-s3-sender-win32`. Compile only `grandmp_independent_native_control_proof`. The review package records actual target definitions, hashes, PE inspections and full test results.

Only after review: make separate authenticated backups/inventories for A and B, with both games closed. Install only the matching candidate plugin and unchanged script in its own reviewed session; never coinstall bootstrap variants or reuse an installer pinned to another hash. Preserve WindowedMode, CLEO, loaders, old plugins and evidence.

Run the existing server; set the existing `GRANDMP_REMOTE_PED_SERVER` and bootstrap opt-in in both game environments, plus `GRANDMP_REAL_INPUT_SENDER_ENABLE=1` only for B. Use F6 A then F6 B. Confirm distinct real sessions, receiver live and sender publication, with no remote created in B. Hold Walk + W in foreground B for five seconds, release, then test back, sides and curves. Record both views and logs. Observe continuous remote input/movement in A, prompt stop after release and independent local CJ. Test a brief focus loss/return separately, without altering focus policy.

Stop on terminal/restore failures, unexpected movement, local interference or duplicate entities. No F8 cleanup or automatic retry/readmission. Close B/A normally, preserve logs, verify processes exited, then restore only each session's authenticated files and compare complete inventories, hashes, sizes and timestamps. Never restore a loaded plugin. These instructions do not authorize installation or game launch.
