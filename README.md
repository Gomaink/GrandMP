<div align="center">

# GrandMP

**Experimental multiplayer research and development for Grand Theft Auto: San Andreas**

*C++20 · Windows x86 · CMake · Native engine integration · UDP*

[Features](#development-status) · [Architecture](#architecture) · [Building](#building-from-source) · [Contributing](#contributing)

</div>

> [!WARNING]
> **Early development / research prototype.** GrandMP is not a playable multiplayer replacement. Native hooks are version-specific, experimental and disabled in default builds. Do not use experimental binaries with an unverified game installation.

## About

GrandMP is an independent multiplayer project for the original **GTA: San Andreas**. Its current Windows prototype receives remote input over UDP and replays it through the game's native player processing. A portable C++20 core separates networking and player state from platform-specific engine integration.

The goal is a maintainable foundation for multiplayer features. GrandMP is **not** a decompilation of GTA: San Andreas and does not include the game, proprietary assets, or a modified executable.

## Development status

| Capability | Status |
| --- | --- |
| Game version identification and guarded native integration | Physically validated on an isolated GTA SA 1.0 US Hoodlum executable |
| Native PED creation and removal | Physically validated (Alpha 0.4) |
| Native walking task for a locally controlled PED | Physically validated (Alpha 0.5) |
| UDP-driven remote PED spawn and despawn | Physically validated (Alpha 0.6) |
| Network-driven relative PED walking | Physically validated (Alpha 0.7 FU6); retained as a separate legacy experiment |
| Persistent second CPlayerPed with remote on-foot input | Physically validated (Alpha 0.8, October 22, 2026): walk forward, move laterally, stop, then remove on disconnect |
| Two real GTA clients synchronizing with each other | Not validated; the remote source is currently a synthetic client |
| Snapshot-based physical reconciliation | Not enabled |
| General-purpose playable multiplayer | Not implemented |

These statuses refer to **limited, isolated tests**, not production-ready features. The Alpha 0.8 operator also confirmed successful restoration of both original ASIs. See [Current status](docs/STATUS.md), [Changelog](CHANGELOG.md) and [Roadmap](docs/ROADMAP.md).

## Architecture

```text
Synthetic input source → UDP server (loopback)
                              ↓
                  ObserverNetwork / roster / Scene
                              ↓
                    RemotePlayerController
                              ↓
                        Native Binding
                              ↓
                Native Player Factory → CPlayerPed (slot 1)
                              ↓
                Scoped input replay → original ProcessControl
```

Network workers publish copied values, never GTA pointers. Creation and removal run in the admitted game-thread phase. Input replay wraps the remote player's normal `ProcessControl` dispatch, restores the borrowed pad state and leaves local CJ on the original path. Snapshots remain references for future reconciliation; this path does not write position, velocity or matrices. See [Architecture](docs/architecture.md).

## Building from source

### Requirements

- Windows x86 build environment
- Visual Studio 2026 with MSVC C++ tools and Windows SDK (or a compatible toolchain after validation)
- CMake and Python for relevant development tests
- A legitimate, independently installed copy of GTA: San Andreas is **not required to compile the core tests**; native experiments require a separately owned compatible game installation

Example closed development build (no GTA installation required; physical gates remain off in a fresh directory):

```powershell
cmake -S . -B build/dev-win32 -G "Visual Studio 18 2026" -A Win32 -DBUILD_TESTING=ON -DGRANDMP_BUILD_REMOTE_PLAYER_CONTROLLER_HOST=ON
cmake --build build/dev-win32 --config Debug --target grandmp_server grandmp_synthetic_client grandmp_remote_player_controller_host grandmp_remote_player_controller_tests grandmp_remote_player_binding_tests grandmp_native_second_player_tests grandmp_native_player_replay_tests grandmp_native_player_replay_compile grandmp_native_player_identity_compile
ctest --test-dir build/dev-win32 -C Debug --output-on-failure -j 1 -R "^(remote_player_controller_|remote_player_binding_|native_second_player_|native_player_replay_)"
```

> [!NOTE]
> This selects pipeline, binding, lifecycle and replay regressions, including disposable hosts and a real loopback UDP test with a non-GTA backend. It does not run the entire repository suite or demonstrate gameplay. Python is required for the process-level loopback tests. See [Build instructions](docs/BUILDING.md) for target and gate details.

Do not co-install multiple experimental ASIs: several historical experiments target the same native call site. Debug binaries may require the MSVC Debug runtime.

## Project layout

| Path | Purpose |
| --- | --- |
| `shared/` | Portable C++20 protocol, player state, controller and Scene |
| `client/windows/` | Windows native integration and experimental client runtime |
| `client/android/` | Reserved Android adapter; no functional native client yet |
| `server/` | UDP server implementation |
| `tests/` | Unit, regression, build-gate and loopback tests |
| `docs/` | Technical documentation and design notes |
| `CMakeLists.txt` | Top-level CMake configuration |

Generated files in `build/`, diagnostic logs, dumps, local backups and GTA files are **not distributed**.

## Roadmap

- [x] Guarded native execution and one-PED lifecycle
- [x] Task-based walking validated locally
- [x] One remote PED spawned and removed via UDP
- [x] One network-driven native walk (Alpha 0.7 FU6)
- [x] Persistent remote CPlayerPed with native input replay (Alpha 0.8)
- [ ] Capture real player controls and validate two GTA clients together
- [ ] Reconcile remote snapshots without unsafe pose or velocity writes
- [ ] Multiple remotes, gameplay authority, vehicles and combat
- [ ] Pawn, modding and a separately authenticated Android adapter

The native experiment supports one additional player in the original cooperative slot. It is not a general slot-expansion or full multiplayer implementation. See the [Roadmap](docs/ROADMAP.md) for remaining acceptance criteria.

## Contributing

Bug reports, focused improvements and reviews are welcome. Read [CONTRIBUTING.md](CONTRIBUTING.md). Please avoid posting proprietary game files, private paths, crash dumps containing personal data or undisclosed vulnerabilities in public issues.

## License

See [LICENSE](LICENSE) for the license covering the original GrandMP code. Any third-party components retain their own licenses. **Review ownership and third-party notices before publishing.**

## Disclaimer

GrandMP is an independent community/research project. It is **not affiliated with, sponsored by or endorsed by Rockstar Games or Take-Two Interactive**. GTA: San Andreas and related trademarks belong to their respective owners. A legitimate copy of the game is required for any game integration; no Rockstar source code, binaries, assets or game files are provided.
