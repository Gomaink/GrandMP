# Windows client

The Alpha 0.8 experimental pipeline represents one remote player with an additional native `CPlayerPed` in cooperative slot 1. `ObserverNetwork` feeds `RemotePlayerController`; the native binding creates/removes the owned player on the admitted game-thread phase and replays input around that player's original `ProcessControl` dispatch.

The October 22, 2026 operator-supervised smoke demonstrated forward/lateral walking, stop, persistence until disconnect and removal, followed by restoration of both original ASIs. The input source was synthetic; two real GTA clients have not been validated together.

`grandmp_windows` still produces the separate `GrandMP.asi` diagnostic baseline. It is not automatically upgraded by building or exposing the input experiment. `grandmp_native_player_input_experimental` produces the isolated replay ASI only when its OFF-by-default CMake option is explicitly selected. No experimental binary is supplied here.

Native admission is restricted to the exact supported GTA SA 1.0 US Hoodlum image and reviewed code windows. Do not infer support for Compact or other revisions. The bootstrap uses `InitializeASI` outside loader-lock gameplay work, admitted phase/thread checks and module residency. No engine operation runs on the UDP worker. Experiments must not share an installation, and files may only be changed after the game has exited.

See [build instructions](../../docs/BUILDING.md), [architecture](../../docs/architecture.md) and [current status](../../docs/STATUS.md). Earlier native research notes describe their individual experiments; they are not a current installation guide.
