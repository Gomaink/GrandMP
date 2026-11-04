# Real GTA input sender - Alpha 0.14 Slice 2

Baseline: `e60bfca111bb2a0c82eda797ae9606f1ae1b804d`. This slice adds a sender-only role to the existing isolated independent-player target. Physical B-to-A movement is pending review and a supervised smoke; offline results are not native movement proof.

## Roles and data path

B: authenticated CLEO pre-world opcode -> existing `captureLocalPlayer` -> `LocalKinematics` and `LocalPlayerSnapshots` -> existing network worker `ObserverNetwork::tickLocal` -> GrandMP UDP relay.

A: unchanged Slice 1 receiver -> `RemotePlayerController` -> `Binding::consume/process` -> private CPlayerPed dispatch -> original native processing, once per dispatch.

`GRANDMP_ENABLE_REAL_PLAYER_SENDER` defaults to OFF and requires the existing independent UDP profile. ON defines `GRANDMP_REAL_PLAYER_SENDER_BUILD=1` and `GRANDMP_NATIVE_LOCAL_CAPTURE_BUILD=1` only on `grandmp_independent_native_control_proof`, still `EXCLUDE_FROM_ALL`. The sender requires both `GRANDMP_CLEO_BOOTSTRAP_ENABLE=1` and `GRANDMP_REAL_INPUT_SENDER_ENABLE=1`. The receiver needs only the existing bootstrap opt-in. Both require the existing authenticated Hoodlum/CLEO/script and exact installation-path contracts.

The sender does not prepare a Binding, stage a remote, create an entity, publish a private dispatch or call replay. Its existing Api is used only for read-only local identity checks and is constructed with a zero control address, which also prevents factory admission. The sender's script entity handle is always -1. The receiver's native factory, F6 admission, original processing, pad COW, restoration and strict readiness are unchanged. Neither role admits F8 or destructive cleanup in UDP mode.

## Capture and freshness

The existing capture body is reused unchanged. All game reads occur on the admitted native thread, inside the authenticated pre-world script phase. Identity is checked before and after reads. Position/orientation are read; velocity is a measured position delta, never a direct engine write. The worker receives immutable copied values through the existing thread-safe latest-snapshot interface and never calls a GTA reader or Api.

Capture and input publication are bounded to 5 Hz (200 ms); the worker services transport every 20 ms. There is no input before a valid network session and measurement warm-up. Revision, freshness, sequence, recipient and session checks are unchanged. Invalid/unavailable samples are invalidated, never replaced with invented movement. The receiver neutralizes stale input after 500 ms while retaining authenticated original processing.

Only unarmed, standing, on-foot walking is admitted by the existing collector/replay. Hold the configured walk modifier with the direction input. Running, sprint, jump, weapons, vehicles and unsupported camera contexts are not implemented. The existing collector publishes neutral controls when its process is not foreground; this slice adds no focus, window, pause or background mechanism. Leave B focused during the unidirectional smoke. WindowedMode remains external and untouched.

Reconnect creates a new session/key at the Controller. The retained receiver object cannot be reassigned to that identity: it remains neutral and no replacement or destructive removal is authorized. Restart a supervised session for a fresh physical admission. Automatic reconnect/readmission is not a delivered capability.

## Separate Debug/Win32 builds

Use Visual Studio 18 2026, Win32, authenticated CLEO4 4.4.4 SDK and the existing Sanny compiler. From the slice worktree, run each configuration in a new directory; never overwrite an earlier physical build:

```powershell
$common = @(
  '-DGRANDMP_BUILD_INDEPENDENT_NATIVE_CONTROL_PROOF=ON',
  '-DGRANDMP_ENABLE_AUTHENTICATED_PAD_COW=ON',
  '-DGRANDMP_ENABLE_REMOTE_TAIL_DIAGNOSTIC=ON',
  '-DGRANDMP_ENABLE_SEMANTIC_PAD_CONTEXT=ON',
  '-DGRANDMP_ENABLE_MINIMAL_NATIVE_LIFECYCLE=ON',
  '-DGRANDMP_ENABLE_INDEPENDENT_PLAYER_UDP=ON',
  "-DGRANDMP_CLEO4_SDK_DIR=$env:GRANDMP_CLEO4_SDK_DIR",
  "-DGRANDMP_SANNY_COMPILER=$env:GRANDMP_SANNY_COMPILER"
)
cmake -S . -B build/alpha014-s2-receiver-win32 -G 'Visual Studio 18 2026' -A Win32 @common -DGRANDMP_ENABLE_REAL_PLAYER_SENDER=OFF
cmake --build build/alpha014-s2-receiver-win32 --config Debug --target grandmp_independent_native_control_proof
cmake -S . -B build/alpha014-s2-sender-win32 -G 'Visual Studio 18 2026' -A Win32 @common -DGRANDMP_ENABLE_REAL_PLAYER_SENDER=ON
cmake --build build/alpha014-s2-sender-win32 --config Debug --target grandmp_independent_native_control_proof
```

Receiver output: `client/windows/independent-player-udp/Debug/GrandMPPlayerBootstrap.cleo` under its build directory. Sender output: `client/windows/independent-player-sender/Debug/GrandMPPlayerBootstrap.cleo` under its own directory. Each uses the unchanged generated `client/windows/cleo-player-script/GrandMPPlayerBootstrap.cs`. Never coinstall variants sharing this basename/opcode. These are CLEO plugins, not standalone executables.

## Tests and supervised smoke boundaries

```powershell
cmake --build build/alpha014-s2-receiver-win32 --config Debug --target grandmp_independent_udp_tests grandmp_real_client_sync_tests grandmp_minimal_lifecycle_tests grandmp_minimal_binding_tests grandmp_remote_player_binding_tests grandmp_semantic_pad_context_tests grandmp_server grandmp_synthetic_client
ctest --test-dir build/alpha014-s2-receiver-win32 -C Debug -j1 -R '^(real_sender_|independent_udp_|minimal_|remote_player_binding_|semantic_pad_context_|real_client_sync_)' --output-on-failure
```

The synthetic executable above is used only by the retained Slice 1 regression, not by the new sender test or the future B-to-A smoke.

The new test host compiles the existing capture body with simulated game reads, then uses actual snapshots, ObserverNetwork, relay, Controller and Binding with the existing simulated native backend. It checks four directions, four diagonals, neutral, timeout, reconnect fences, context/read/identity failure and a missing server. Existing gate-OFF, stale-sequence, wrong-recipient, lifecycle and semantic-pad regressions remain applicable. No test opens GTA.

After independent approval only: authenticate binaries/dependencies and create independent A/B backups and inventories first; install the receiver in A and sender in B through reviewed reversible sessions. Preserve existing CLEO, WindowedMode, loaders, other plugins and evidence. Start the existing server, then both games under their own environment. Set `GRANDMP_REMOTE_PED_SERVER=127.0.0.1:7777` for one host, or the authenticated private IPv4 endpoint for two hosts. The server supports `--bind <private-IPv4> --port 7777` for LAN. No client synthetic process is used in this smoke.

F6 in A arms only the receiver. F6 in B arms only local capture. The unchanged script has legacy F6-create/F8-remove UI wording: on the sender, F6 means capture, F8 is inert, no local remote appears, and no sender `tick.live` is expected. Confirm distinct server sessions and sender `network.local_sender` before walking B. Check forward/back/left/right, direction changes and stop in B while observing A. Confirm A local CJ remains independent. B must never create a remote. Do not press F8 for cleanup: UDP destruction is blocked.

Stop on any terminal diagnostic, local interference, duplicate entity, unexpected input, missing restoration evidence or loss of session. Do not retry F6 or try to force cleanup. After collecting both logs, close the games normally, verify processes exited, then restore only authenticated changed plugin/script files from their own session backups. Preserve logs and unknown files; never restore while a module is loaded. Compare full inventories, sizes, hashes and timestamps. No installation or game launch is authorized by this document.
