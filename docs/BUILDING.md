# Building GrandMP

## Closed Windows development build

Use Visual Studio 2026 with the C++ workload, x86 MSVC tools, Windows SDK, CMake and Python 3. GTA is not required for the following focused build. Use a **new build directory** so an earlier cache cannot retain physical options.

```powershell
cmake -S . -B build/dev-win32 -G "Visual Studio 18 2026" -A Win32 -DBUILD_TESTING=ON -DGRANDMP_BUILD_REMOTE_PLAYER_CONTROLLER_HOST=ON
cmake --build build/dev-win32 --config Debug --target grandmp_server grandmp_synthetic_client grandmp_remote_player_controller_host grandmp_remote_player_controller_tests grandmp_remote_player_binding_tests grandmp_native_second_player_tests grandmp_native_player_replay_tests grandmp_native_player_replay_compile grandmp_native_player_identity_compile
ctest --test-dir build/dev-win32 -C Debug --output-on-failure -j 1 -R "^(remote_player_controller_|remote_player_binding_|native_second_player_|native_player_replay_)"
```

These are existing targets. The two `*_compile` targets compile the actual native runtime branches as object files with execution gates closed; they do not link or load a physical ASI. The tests exercise portable state, disposable host memory/context behavior and the real UDP server/synthetic source with a non-GTA receiver. None starts GTA or proves gameplay.

The regex intentionally selects tests whose targets were built. To run additional suites, first build their corresponding targets. Optional offline executable-file audits are disabled unless their cache file paths are explicitly provided. Test counts therefore depend on platform and configuration.

## Experimental target selection

| CMake option | Target exposed | Default |
| --- | --- | --- |
| `GRANDMP_BUILD_REMOTE_PLAYER_CONTROLLER_HOST` | `grandmp_remote_player_controller_host` (inspection executable, no GTA backend) | OFF |
| `GRANDMP_BUILD_NATIVE_REMOTE_PLAYER_BINDING` | `grandmp_native_remote_player_binding` (closed static library) | OFF |
| `GRANDMP_BUILD_NATIVE_SECOND_PLAYER_SLOT` | `grandmp_native_second_player_slot` (closed static library) | OFF |
| `GRANDMP_BUILD_NATIVE_PLAYER_EXPERIMENTAL` | `grandmp_native_player_experimental` (factory experiment) | OFF |
| `GRANDMP_BUILD_NATIVE_PLAYER_INPUT_EXPERIMENTAL` | `grandmp_native_player_input_experimental` (factory + input replay experiment) | OFF |

The experimental ASI targets require MSVC/Win32 and are `EXCLUDE_FROM_ALL`. Exposing a target does not install it or authorize execution. The replay target selects `GRANDMP_CPLAYER_VARIANT=1`, `GRANDMP_NATIVE_SECOND_PLAYER_EXECUTION_BUILD=1`, `GRANDMP_PED_SMOKE_PHYSICAL_BUILD=1` and `GRANDMP_NATIVE_INPUT_REPLAY_BUILD=1` together. Its output is isolated under `client/windows/native-player-input/Debug/GrandMPNativePlayerExperimental.asi` inside the chosen build directory. The factory-only target has its own output directory and does not enable replay.

Legacy options `GRANDMP_BUILD_PED_SMOKE_ENABLED`, `GRANDMP_BUILD_PED_MOVE_ENABLED`, `GRANDMP_BUILD_REMOTE_PED_ENABLED`, `GRANDMP_BUILD_REMOTE_PED_MOVE_ENABLED`, `GRANDMP_BUILD_REMOTE_PED_TASK_DIAGNOSTIC_ENABLED` and `GRANDMP_ENABLE_PASSIVE_MOVEMENT_DIAGNOSTIC` remain OFF by default. They are separate experiments, not prerequisites for the CPlayerPed replay target. Environment variables alone cannot open physical build gates.

No physical build or installation command is part of the default workflow above. Native admission additionally requires the exact supported executable/code, isolated lab path/opt-in contract, admitted gameplay/thread and a separately reviewed installation session. The current source retains strict lab admission; changing it is a compatibility change, not an installation shortcut. Never co-install native variants sharing hook/dispatch sites, copy experimental ASIs into a live game, or hot-unload them.

## Runtime and portability

Debug experimental modules require the matching **x86** MSVC Debug runtime and Debug Universal CRT on the development machine. They are not standalone redistributable releases. Review the generated PE imports for the actual toolchain before any separately authorized smoke.

The portable `shared/` core, server and synthetic client use C++20 and have platform-neutral contracts. Windows native integration is x86-specific; Android remains a separate future adapter. A Windows build does not establish Linux or Android validation. The top-level CMake package version is independent of Alpha milestone numbering.

Generated binaries, maps, logs, dumps, backups and private installation/session manifests belong outside Git. See [status](STATUS.md) and [architecture](architecture.md).
