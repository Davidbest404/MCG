# MCG Network Protocol

## Frame Format
All TCP traffic is wrapped in frames:

[u32 frame_size (Little Endian)][u16 msg_type (Little Endian)][JSON payload]

- `frame_size`: Total size of the frame including the 4-byte header.
- `msg_type`: Indicates how to parse the JSON.
- Max frame size is 16MB.

## Message Types
| ID | Name | Direction | Description |
|---|---|---|---|
| 1 | `Ping` | C -> S | Keep-alive or initial connection. |
| 2 | `Pong` | S -> C | Server response to Ping. |
| 10-13 | `Auth*` | Both | Authentication flow. |
| 20 | `ChatMessage` | Both | Global, radius, or direct chat. |
| 21 | `Command` | C -> S | Player command (e.g., `/move north`). |
| 30 | `EntityUpdate` | S -> C | Update entity position/attributes. |
| 40 | `WorldSnapshot` | S -> C | Sends the rendered ASCII grid. |

## Chat & Cards
Chat messages contain metadata:
```json
{
  "from": "Ivan",
  "text": "Hello!",
  "scope": "radius",
  "radius": 10,
  "form": "plain"
}

@ scope: global, radius, team, direct.

@ form: plain or card (custom ASCII art framing).

---

### 📄 4. `docs/lua_api.md` (Scripting Guide)

```markdown
# MCG Lua API Reference

## Command Definition
Every file in `lua/actions/commands/` must return a table:
```lua
return {
    name = "command_name",
    description = "Does something",
    args = "<arg1> [arg2]",
    category = "general",
    on_command = function(player, args) 
        -- logic here
    end
}

API Functions

Player Attributes
@ api.get_attr(player, "hp") -> Returns value (int, float, bool, string).

@ api.set_attr(player, "hp", 100) -> Sets value.

Messaging
@ api.broadcast("Hello [cFF0000]World[/c]") -> Global message with TrueColor.

World Manipulation
@ api.is_walkable(x, y, z) -> boolean.

@ api.set_terrain(x, y, z, sym, color_hex, walkable, transparent)

  # sym: String (can be empty "" for air, or " " for background).

  # color_hex: "RRGGBB".

  # walkable: boolean.

  # transparent: boolean (does it block vision?).

@ api.set_overlay(x, y, z, sym, color_hex, transparent, walkable)

  # sym: e.g., "T" for tree, "*" for flower.

  # walkable: (optional) Defaults to transparent.

Math & Noise
@ api.noise2d(x, y) -> Returns Perlin noise (-1.0 to 1.0).

@ api.noise3d(x, y, z) -> Returns 3D Perlin noise for caves.

Rendering
@ api.update_fov(player) -> Forces the server to recalculate and send the map to the client.

---

### 📄 5. `docs/world_generation.md` (Biomes & Terrain)

```markdown
# World Generation Architecture

World generation is split into two phases to maintain high performance and modularity: **Base Terrain** and **Decorators (Biomes)**.

## 1. Base Terrain (`lua/world/generator.lua`)
This script is triggered automatically when a new `16x16x16` chunk needs to be created. 
It uses `lua/world/common.lua` to calculate the height of the land.
- **`get_height(x, y)`**: Uses Fractal Brownian Motion (fBm) to generate realistic, non-repeating hills.
- It sets the `Terrain` layer (dirt, stone, grass background).

## 2. Biomes & Decorators (`lua/world/biomes/*.lua`)
Every `.lua` file inside the `biomes/` folder is loaded as a **Decorator**. 
After the base terrain is generated, the server loops through all loaded decorators and calls them with the chunk coordinates.

### Example `forest.lua`:
```lua
return function(cx, cy, cz)
    -- Loop through chunk, call api.set_overlay() to place trees
end

Example flowers.lua:

return function(cx, cy, cz)
    -- Loop through chunk, call api.set_overlay() to place flowers
end

Creating a New Biome
1) Create a new file in lua/world/biomes/, e.g., swamp.lua.

2) Define a function that loops x = 0..15, y = 0..15.

3) Use get_biome(gx, gy) in common.lua to check if the current area is a swamp.

4) Use api.set_overlay(...) to place swamp-specific objects (e.g., vines, muddy water).

---

### 📄 6. `docs/save_format.md` (Data Persistence)

```markdown
# Save File Structure

*(Note: This system is planned for Phase 8)*

To ensure easy editing by administrators and content creators, the world and player data will be saved in standard, formatted JSON files.

## Planned Files
- `saves/config.json`: Server settings.
- `saves/world/chunk_X_Y_Z.json`: Generated/modified chunks.
- `saves/players.json`: Player positions, inventory, and attributes.
- `saves/scripts_state.json`: Which custom scripts/presets are active.

## Chunk JSON Structure
```json
{
  "terrain": [
    ["grass", "grass", "stone"], 
    ...
  ],
  "overlays": [
    {"type": "tree", "id": "T"},
    ...
  ]
}

---

### 🚀 Next Step
You can now safely commit these files to your `docs/` folder:
```bash
git add README.md docs/*.md
git commit -m "docs: actualized full project documentation in English"
git push origin Rebuild-with-AI