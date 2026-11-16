# MCG Architecture

## 1. Core Principles
- **Zero-Core:** C++ provides only primitives (networking, memory, Lua VM). Game logic is Lua.
- **Server Authoritative:** Server validates everything. Client only renders what it receives.
- **Thin Client, Fat Server:** Server does FOV and data filtering. Client does rendering, lighting, colors.

## 2. Threading
### Server
- **LogicThread:** Lua, world updates, command processing.
- **NetworkThread:** asio IO.
- **PersistenceThread:** (planned) JSON saves.

### Client
- **MainThread:** Input, network callbacks, render loop.
- **NetworkThread:** Non-blocking sockets.

## 3. Networking Protocol
Frames: `[u32 size][u16 type][JSON]`. See `protocol.md`.

## 4. World & Coordinates
- 3D coordinate system (X, Y, Z).
- Chunks: `16x16x16`.
- Two layers per block: `terrain` (base) and `overlay` (objects).

## 5. FOV & Anti-Cheat
The server is the sole authority on visibility:
1. On `update_fov`, server casts rays from player position.
2. Only visible cells are included in the response.
3. Hidden cells (behind walls, beyond radius, or deep in fog) are **never sent**.
4. The client literally does not have the data to draw them.

Therefore:
- Modifying the client cannot reveal hidden content.
- Wallhacks, x-ray, and "see-through-fog" are impossible by design.

## 6. Lighting
The server does **not** calculate lighting. It only sends base colors (`fg`, `bg`).
The client computes brightness based on `light_step` (received from server config):
- Up: `color = mix(color, WHITE, (light_step^1 - 1) * distance)`.
- Down: `color = color * (1 / light_step)^distance`.

## 7. Rendering — Multi-Axis Slicing
The client requests a "slice" of the world along a chosen axis:
- **`xy`** (Top-Down): view from above (Minecraft, Mindustry).
- **`xz`** (Side-View): view from the side (Terraria).
- **`yz`** (Front-View): view from the front (Space sims).

Player's position + view axis + render distance = request sent to server.
Server returns a 2D grid of tiles (visible only) for that slice.

## 8. Server Config Sync
The client receives `RenderConfig` from the server:
1. On connect.
2. On every Lua reload (`/lua reload`).
3. When admin changes server settings.

This config includes:
- `light_step`, `ceiling_height`, `render_depth`
- `render_mode` (topdown, level, or axis)
- `max_render_distance`
- Biome color palette

Client **cannot** override this (for fairness). It may only adjust cosmetic things like font or terminal size.

## 9. Client Rendering Pipeline
1. Server sends `WorldRender` with visible tiles.
2. Client stores tiles in a `TileGrid`.
3. Client applies lighting (`light_step`).
4. Client converts to ANSI TrueColor and prints.
5. On player move, server sends update; client re-renders.

The client is a pure visualization layer. It has no world state of its own.