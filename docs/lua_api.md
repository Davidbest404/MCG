# Lua API MCG

> Заполняется в Phase 3, когда появится движок Lua. Здесь будет
> справочник всех функций, доступных Lua-скриптам, с примерами.

## Планируемое ядро API

- `api.get_attr(player, name)` / `api.set_attr(player, name, value)`
- `api.has_attr(player, name)`
- `api.broadcast(text, color?)`
- `api.move(player, dx, dy, dz?)`
- `api.distance(a, b)`
- `api.entities_in_radius(pos, radius)`
- `api.damage(player, amount)` / `api.heal(player, amount)`
- `api.get_tile(x, y, z)` / `api.set_tile(x, y, z, tile_id)`