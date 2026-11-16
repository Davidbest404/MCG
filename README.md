# MCG — Multi Console Game

MCG is a cross-platform, highly extensible MUD (Multi-User Dungeon) engine with a focus on procedural world generation, ASCII/TUI rendering, and deep Lua scripting. 

The server is fully authoritative, while the client acts as a thin terminal. The engine features a "Zero-Core" philosophy: almost all game logic, world generation, and commands are written in Lua, allowing content creators to build entirely different games (from Minecraft-like voxel worlds to Space Simulators) without touching the C++ core.

## Features
- **Infinite Procedural Worlds:** Chunk-based generation powered by Lua and Perlin Noise.
- **Biomes & Decorators:** Modular Lua scripts for placing trees, flowers, and terrain.
- **TrueColor TUI:** 24-bit color rendering directly in the terminal.
- **Lock-Free Networking:** Asynchronous server architecture using `asio`.
- **Dynamic Rendering Modes:** Top-Down (satellite view) and Level (first-person/roguelike view).

## Building the Project

### Prerequisites
- CMake 3.24+
- C++20 compatible compiler (MSVC 2022, GCC 13+, Clang 16+)
- Git

### Windows (Visual Studio 2022)
1. Open the project folder in Visual Studio (`File -> Open -> Folder`).
2. Visual Studio will automatically read `CMakePresets.json`.
3. Select the `windows-vs` preset and build.
*Alternatively, via command line:*
cmake --preset windows-vs
cmake --build --preset windows-debug

*Linux / macOS*
cmake --preset unix-ninja
cmake --build --preset unix-debug

Running
1) Start the server: ./build/bin/mcg_server (or .exe)

2) Start the client: ./build/bin/mcg_client (or .exe)

3) Enter the port (default 7777) and a world seed (leave blank for random).

Project Structure
@ src/ — C++ Source code (shared, server, client).

@ lua/ — All game logic, commands, and world generation.

@ docs/ — Detailed documentation.

@ tests/ — Unit tests (Catch2).

---

### 📄 2. `docs/architecture.md` (System Design)

```markdown
# MCG Architecture

This document is the source of truth for the engine's core design decisions.

## 1. Core Principles
- **Zero-Core Philosophy:** The C++ engine provides only primitives (networking, memory, basic I/O). Game logic, commands, and even world generation are written in Lua.
- **Server Authoritative:** The client sends intents (commands). The server validates them, changes the state, and broadcasts the results.
- **OOP for Orchestration, DOD for Hot Paths:** Classes are used for high-level systems (Server, Network, LuaEngine). Data-Oriented Design (SoA, flat arrays) is used for rendering and spatial queries.

## 2. Threading Model
### Server
- **LogicThread:** Handles world updates, Lua execution, and command processing.
- **NetworkThread:** Handles `asio` TCP connections, packet framing, and socket I/O.
- **PersistenceThread:** (Planned) Periodically saves the world to JSON.

### Client
- **MainThread:** Handles input, network callbacks, and UI rendering.
- **NetworkThread:** Non-blocking socket reads/writes.

## 3. Networking Protocol
Frames are length-prefixed to ensure reliable TCP transmission:
`[u32 frame_size][u16 msg_type][JSON payload]`
See `protocol.md` for message types.

## 4. Coordinate System & World
- **3D Coordinates (X, Y, Z):** Z represents elevation (floors/layers).
- **Chunk System:** The world is divided into `16x16x16` chunks, loaded dynamically.
- **Terrain vs Overlay:**
  - `Terrain`: The base land (dirt, stone, grass background). Does not block vision.
  - `Overlay`: Objects (trees, flowers, walls). May block vision and movement.

## 5. Rendering Modes
The engine supports multiple rendering modes, which can be toggled via `/render_mode`:
- **`topdown`:** Satellite view. Renders everything from the deepest floor up to `ceiling_height`. Lighting blends to white at high altitudes.
- **`level`:** Roguelike view. Renders *only* the player's current Z-level. Unclimbable walls are marked with a red `X`.
- **`auto`:** Automatically switches between `topdown` (if outside) and `level` (if in a cave/indoors).

## 6. Lighting & Colors
- The engine uses **TrueColor** (24-bit RGB). Tags follow the format `[cRRGGBB]` and `[bgRRGGBB]`.
- **Lighting Step:** Elevation changes colors. Going up blends towards white (`FFFFFF`), going down multiplies towards black (`000000`). The step size is customizable via the `light_step` attribute.