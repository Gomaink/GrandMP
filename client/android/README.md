# Android client

This directory is reserved for the Android NDK/JNI integration with GTA San Andreas. There is no functional native Android client in the Alpha 0.8 milestone.

Protocol, domain state and controller contracts live in the portable C++20 core. Android requires its own authenticated game-version, ABI, loading and lifecycle contracts; Windows addresses and internal layouts are not reusable assumptions. See the [architecture](../../docs/architecture.md) and [roadmap](../../docs/ROADMAP.md).
