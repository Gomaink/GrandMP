# Server

`grandmp_server` is an experimental UDP server. It binds only to `127.0.0.1`, uses port 7777 by default and accepts `--port` for isolated tests. The session table is separate from the socket loop and uses the portable protocol in `shared/`.

The current pipeline relays position and on-foot input snapshots to observers. It preserves session/sequence checks and supports the persistent synthetic walk/turn/stop demo used for Alpha 0.8. It does not simulate authoritative gameplay or provide production authentication, public networking or persistence.

See [architecture](../docs/architecture.md), [status](../docs/STATUS.md) and [build instructions](../docs/BUILDING.md). The earlier transport notes are historical specifications for their respective slices.
