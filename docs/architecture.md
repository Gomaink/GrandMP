# GrandMP architecture (experimental)

## Overview

GrandMP is a C++ project exploring networked entity state in GTA: San Andreas. The Windows native module is constrained to a compatible, explicitly verified executable and a carefully selected engine callback.

## Networking

An experimental UDP server runs on loopback during physical validation. The observer maintains remote state, rejects stale messages where supported, and passes remote-scene events to an intent sink. Network workers publish **values**, not GTA entity pointers.

## Engine boundary

A validated game-thread callback consumes intent snapshots. Version, caller, thread, game phase and code checks guard native access. PED lifecycle checks include strict identity and ownership, native pool metadata, and cleanup rules.

## Movement

Local movement has been physically validated using a native walking task. Experimental remote movement derives a **relative** horizontal offset from the remote position stream, validates the local spawn anchor and prospective path, then attempts one native task. The first Alpha 0.7 physical attempt was rejected at the corridor collision check; network-commanded walking has **not** been physically validated.

## Safety and limitations

- Only one remote PED is supported in the current experimental workflow.
- Compatibility is currently restricted to a specific GTA: SA 1.0 US Hoodlum executable in isolated tests.
- The UDP v1 experimental protocol lacks a full receiver epoch and is **not** a production-authenticated multiplayer protocol.
- Native ASI variants sharing a call site must never be installed together.
- Default physical build gates remain disabled.
- Simulated tests and static analysis are not evidence of successful gameplay execution.
