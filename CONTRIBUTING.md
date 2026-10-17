# Contributing to GrandMP

Thank you for your interest in GrandMP. This is an experimental, native-game integration project. Small, testable changes are especially valuable.

## Before contributing

- Open an issue explaining the observed behavior and reproduction steps.
- Never upload original GTA executable files, game assets, saves, memory dumps containing sensitive data, credentials or copyrighted third-party files.
- Keep game-engine mutations confined to the validated native callback/thread.
- Preserve version, build and ownership gates; never relax protections merely to make a smoke test pass.
- Do not modify or redistribute third-party code without checking its license.

## Pull requests

1. Explain the problem and scope.
2. Describe the relevant architecture and lifecycle assumptions.
3. Include focused tests; use a simulated backend where possible.
4. Run the applicable CMake build and CTest tests serially.
5. Clearly separate simulated evidence from physical gameplay validation.
6. Call out risks, limitations and any changes to native addresses or ABI.

Pull requests are reviewed before merge. Passing tests does not automatically authorize physical execution on a game installation.
