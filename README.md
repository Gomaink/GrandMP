<div align="center">

# GrandMP

**Experimental multiplayer research and development for Grand Theft Auto: San Andreas**

*C++20 · Windows x86 · CMake · Native engine integration · UDP*

[Features](#development-status) · [Architecture](#architecture) · [Building](#building-from-source) · [Contributing](#contributing)

</div>

> [!WARNING]
> **Early development / research prototype.** GrandMP is not a playable multiplayer replacement. Native hooks are version-specific, experimental and disabled in default builds. Do not use experimental binaries with an unverified game installation.

## About

GrandMP is an independent experiment in integrating networked player state with the original **GTA: San Andreas** engine. The project investigates native entity lifecycle, task-based pedestrian movement, network observation and strict engine-thread boundaries.

The goal is a maintainable foundation for multiplayer features. GrandMP is **not** a decompilation of GTA: San Andreas and does not include the game, proprietary assets, or a modified executable.

## Development status

| Capability | Status |
| --- | --- |
| Game version identification and guarded native integration | Physically validated on an isolated GTA SA 1.0 US Hoodlum executable |
| Native PED creation and removal | Physically validated (Alpha 0.4) |
| Native walking task for a locally controlled PED | Physically validated (Alpha 0.5) |
| UDP-driven remote PED spawn and despawn | Physically validated (Alpha 0.6) |
| Network-driven relative PED walking | Experimental (Alpha 0.7); first physical attempt stopped by a collision-corridor check before task assignment |
| Continuous movement synchronization | Not implemented |
| General-purpose playable multiplayer | Not implemented |

These statuses refer to **limited, isolated tests**, not production-ready features.

## Architecture

```text
Synthetic UDP client ──> UDP server (loopback in lab)
                              │
                              ▼
                   Observer / remote roster
                              │
                              ▼
                    Remote player scene
                              │
                              ▼
                    Thread-safe intents
                              │
                              ▼
                 Validated GTA main-thread phase
                              │
                              ▼
                  Native PED / task lifecycle
```

Network workers do not create or remove GTA entities. Native engine actions are restricted to a validated game-thread phase, with explicit identity, lifecycle and ownership checks. See [Architecture](docs/ARCHITECTURE.md).

## Building from source

### Requirements

- Windows x86 build environment
- Visual Studio 2026 with MSVC C++ tools and Windows SDK (or a compatible toolchain after validation)
- CMake and Python for relevant development tests
- A legitimate, independently installed copy of GTA: San Andreas is **not required to compile the core tests**; native experiments require a separately owned compatible game installation

Example development build (experimental physical gates remain off):

```powershell
cmake -S . -B build/dev-win32 -G "Visual Studio 18 2026" -A Win32 -DBUILD_TESTING=ON
cmake --build build/dev-win32 --config Debug --target grandmp_server grandmp_synthetic_client grandmp_remote_ped_move_experimental
ctest --test-dir build/dev-win32 -C Debug --output-on-failure -j 1
```

> [!NOTE]
> Some tests and optional targets depend on configuration and development fixtures. Consult CMake target definitions and the test output; the command above is a starting point, not a guarantee of identical results on every machine.

Do not co-install multiple experimental ASIs: several historical experiments target the same native call site. Debug binaries may require the MSVC Debug runtime.

## Project layout

| Path | Purpose |
| --- | --- |
| `client/` | Windows integration and experimental client runtime |
| `server/` | UDP server implementation |
| `tests/` | Unit, regression, build-gate and loopback tests |
| `docs/` | Technical documentation and design notes |
| `CMakeLists.txt` | Top-level CMake configuration |

Generated files in `build/`, diagnostic logs, dumps, local backups and GTA files are **not distributed**.

## Roadmap

- [x] Guarded native execution and one-PED lifecycle
- [x] Task-based walking validated locally
- [x] One remote PED spawned and removed via UDP
- [ ] Diagnose Alpha 0.7 corridor collision rejection and validate one network-driven walk physically
- [ ] Continuous remote movement and interpolation
- [ ] Multi-player entity management, broader compatibility and hardening

## Contributing

Bug reports, focused improvements and reviews are welcome. Read [CONTRIBUTING.md](CONTRIBUTING.md). Please avoid posting proprietary game files, private paths, crash dumps containing personal data or undisclosed vulnerabilities in public issues.

## License

See [LICENSE](LICENSE) for the license covering the original GrandMP code. Any third-party components retain their own licenses. **Review ownership and third-party notices before publishing.**

## Disclaimer

GrandMP is an independent community/research project. It is **not affiliated with, sponsored by or endorsed by Rockstar Games or Take-Two Interactive**. GTA: San Andreas and related trademarks belong to their respective owners. A legitimate copy of the game is required for any game integration; no Rockstar source code, binaries, assets or game files are provided.
