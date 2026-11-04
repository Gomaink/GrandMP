# Independent player UDP input â€” Alpha 0.14 Slice 1

Baseline: `271f6aa` (Alpha 0.13.1). This experimental variant replaces only the deterministic input source. It has not been run in GTA.

## Data path

`server UDP â†’ ObserverNetwork â†’ RemotePlayerController â†’ Binding::consume/process â†’ existing private CPlayerPed dispatch â†’ original CPed::ProcessControl`

The existing `network()` worker attaches `bootstrapController`, subscribes to remote on-foot frames and calls `net.tick(std::nullopt)`. It does not capture GTA memory, call a native backend or publish local controls in this slice. The Controller transfers copied frames through its existing mutex/try-read interface. Neither the wire protocol nor the relay was changed.

F6 still authorizes admission on the authenticated SCM/native thread. A fresh Controller frame with a valid session/key/generation/sequence is required for the single factory attempt. An additional remote cannot replace the selected live identity. The native factory, private vtable, original processing, pad COW and semantic restoration retain their existing contracts.

The 500 ms input timeout, missing/contended input neutralization and stale-packet rejection remain in the Controller/Binding. The experimental path never calls `presentRemote()`, `Api::present()` or a per-packet pose/velocity/heading writer. Snapshot coordinates remain a reconciliation reference; controls reach the original native processing. For the first fresh frame only, the native host stages a copy with the same near-CJ spawn candidate produced by the existing Alpha 0.13.1 Probe. The Controller's received pose/reference is not modified; once live, staging never repositions the PED.

## Deliberately closed lifecycle

F8 is ignored in this variant. `Binding::requestRemoval()` is explicitly closed without consulting a native cleanup provider. A leave, generation/session change or receiver reset can reach `retire()`, but the UDP build returns `no_input` before `removalProven/remove/removed`. It retains the same live owned record, continues authenticated original processing with neutral controls and does not admit a replacement identity. It does **not** report successful native removal. The PED is retained until game/process shutdown; missing input, leave and a new session/generation cannot give another identity control of that object. Native readiness/identity/restoration failures still remain terminal.

No deterministic movement history, neutral-completion token or armed-F8 proof is reused to authorize UDP cleanup. Equivalent admission and physical validation are required in a later slice. Destructors and native ownership/authentication checks are unchanged.

## Isolated build

`GRANDMP_ENABLE_INDEPENDENT_PLAYER_UDP` defaults to `OFF`. It requires the complete Alpha 0.13.1 native profile. The existing target remains `EXCLUDE_FROM_ALL` and gains only `observer_network.cpp`, Winsock and the UDP definition when explicitly enabled. Its output is isolated in `client/windows/independent-player-udp/Debug/GrandMPPlayerBootstrap.cleo`. Never coinstall another bootstrap variant.

```powershell
cmake -S . -B build/alpha014-s1-udp-win32 -G "Visual Studio 18 2026" -A Win32 `
  -DGRANDMP_BUILD_INDEPENDENT_NATIVE_CONTROL_PROOF=ON `
  -DGRANDMP_ENABLE_AUTHENTICATED_PAD_COW=ON -DGRANDMP_ENABLE_REMOTE_TAIL_DIAGNOSTIC=ON `
  -DGRANDMP_ENABLE_SEMANTIC_PAD_CONTEXT=ON -DGRANDMP_ENABLE_MINIMAL_NATIVE_LIFECYCLE=ON `
  -DGRANDMP_ENABLE_INDEPENDENT_PLAYER_UDP=ON `
  -DGRANDMP_CLEO4_SDK_DIR=<authenticated-CLEO4-SDK> -DGRANDMP_SANNY_COMPILER=<sanny.exe>
cmake --build build/alpha014-s1-udp-win32 --config Debug --target grandmp_independent_native_control_proof grandmp_independent_udp_tests grandmp_server grandmp_synthetic_client
ctest --test-dir build/alpha014-s1-udp-win32 -C Debug -j1 -R "^independent_udp_" --output-on-failure
```

Runtime endpoint configuration remains `GRANDMP_REMOTE_PED_SERVER` (validated private IPv4/loopback and port); the existing CLEO opt-in remains `GRANDMP_CLEO_BOOTSTRAP_ENABLE=1`. No installation or execution is authorized by these build instructions.

## Evidence and limits

The loopback test starts the actual server and existing `grandmp_synthetic_client --on-foot-demo` on an ephemeral port. A separate receiver thread runs ObserverNetwork; the main test thread runs the actual Binding with its existing test backend. Assertions observe the controls delivered to `apply()`, not a programmed source in the receiver. They cover walk â†’ turn â†’ neutral, one stable key/record, packet loss/500 ms expiry and leave with no destructor call. Unit cases cover wrong recipient, stale sequence, absent controls, session changes, F8 and default-closed Binding. These tests prove the data path, not movement or native cleanup in Hoodlum.

For physical admission the host reuses the existing near-CJ Probe only before creation; the unchanged factory independently enforces its 4 m XY / 0.75 m Z bound, ground and full collision checks. Probe refusal is terminal before allocation. The stock demo coordinates around `(100, 200, 30)` stay unchanged as wire references; they do not command native placement. There is no teleport or per-packet pose correction. Controls can remain neutral if the demo already ended before admission; any future supervised test must coordinate receiver arming and sender timing.

Only controls admitted by the existing unarmed native replay are supported; this slice does not add buttons or movement capabilities. Readiness remains strict, including transient `loadingBigModel`; an existing native refusal remains terminal. There is no local GTA sender, two-GTA synchronization claim, automatic repositioning, removal/readmission, new hook or new diagnostic subsystem in this slice.
