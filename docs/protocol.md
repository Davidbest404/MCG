## Message Types (expanded)

| ID | Name | Direction | Description |
|---|---|---|---|
| 1 | `Ping` | C→S | Keep-alive. |
| 2 | `Pong` | S→C | Ping response. |
| 20 | `ChatMessage` | Both | Chat with metadata. |
| 21 | `Command` | C→S | Player command. |
| **50** | **`RenderConfig`** | **S→C** | **Lighting, render mode, palette. Sent on connect + on Lua reload.** |
| **51** | **`WorldRender`** | **S→C** | **Visible tile grid for the current view.** |
| **52** | **`RenderRequest`** | **C→S** | **Player asks for a render (on move, on view rotate).** |

### `RenderConfig` (S→C)

```json
{
  "light_step": 1.25,
  "max_render_distance": 8,
  "max_render_depth": 10,
  "ceiling_height": 20,
  "fog_enabled": true,
  "default_render_mode": "topdown"
}

The client must use these values. It may not override them.

WorldRender (S→C)
Compact format for efficiency:
{
  "c": [0, 0, 0],
  "a": "xy",
  "o": [-8, -8],
  "w": 17,
  "h": 17,
  "g": [
    ["", "3E753B", "3E753B"],
    ["T", "008000", "3E753B"],
    null,
    ...
  ]
}

@ c: center (world coords of player)

@ a: axis (xy, xz, yz)

@ o: offset (top-left corner of grid in the slice plane)

@ w, h: grid dimensions

@ g: flat array of w*h cells, row-major.

  # null = fog (not visible).

  # [symbol, fg, bg] = visible cell.

RenderRequest (C→S)

{
  "axis": "xy",
  "center": [0, 0, 0]
}

The client tells the server where it wants to look.
Server responds with WorldRender.

Anti-cheat note: The server will only fill in cells reachable via FOV from center. The client cannot request a WorldRender for a position it doesn't control.

---

## 📄 Обновлённый `docs/lua_api.md` (Render API)

Добавь секцию:

```markdown
## Rendering API

### Server-side rendering hints

- `api.set_render_mode("topdown" | "level" | "auto" | "axis_xy" | "axis_xz" | "axis_yz")`
- `api.set_light_step(1.25)` — server can adjust global lighting.

### Client config refresh

When a Lua script calls `api.reload_config()`, the server re-sends `RenderConfig` to all connected clients.