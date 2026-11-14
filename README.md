# MCG — Multi Console Game

Кроссплатформенный MUD-движок с расширяемостью через Lua.

## Сборка

### Windows (Visual Studio 2022)
1. Открыть папку проекта в VS: `File → Open → Folder`.
2. VS автоматически подхватит `CMakePresets.json`.
3. Build.

Или из терминала:
cmake --preset windows-vs
cmake --build --preset windows-debug


### Linux / macOS
cmake --preset unix-ninja
cmake --build --preset unix-debug


### Результат
- `build/bin/Debug/mcg_server`
- `build/bin/Debug/mcg_client`

## Структура проекта

См. `docs/architecture.md` и `docs/save_format.md`.

## Лицензия

(добавим позже)