# MCG CDI-AI — Руководство по созданию модов (Lua API)

Движок использует модульную архитектуру на Lua. Каждый скрипт отвечает за свою задачу (DOD/OOP). 
Сервер автоматически загружает все `.lua` файлы из папки `plugins/`.

## 1. Система Тиков (Tick System)
Сервер вызывает функцию `on_tick()` 20 раз в секунду (каждые 50 мс). Это идеально для FPS, NPC и кулдаунов.
```lua
local tick_count = 0
function on_tick()
    tick_count = tick_count + 1
    if tick_count % 20 == 0 then
        -- Выполняется раз в секунду
        print("Прошла секунда")
    end
end

## 2. Генерация Мира (Модульная)
Главный скрипт terrain.lua вызывает другие генераторы. Он должен возвращать массив 32x32.
-- plugins/generation/terrain.lua
function generate_chunk(cx, cy, size, seed)
    local chunk = {}
    -- 1. Базовая генерация (шум)
    for y=1,size do
        chunk[y] = {}
        for x=1,size do
            chunk[y][x] = 0 -- 0 = Трава
        end
    end
    -- 2. Вызов биомов (если нужно)
    -- chunk = call_biome(chunk, cx, cy)
    return chunk
end

## 3. Спрайты и Скины
Каждый игрок имеет свойство skin_url (ссылка на изображение). Если оно задано, движок отрисует картинку вместо цветного кружка.
В Lua можно изменить скин игрока:
function on_message(msg)
    if msg.text == "!skin_hero" then
        return { type = "update_skin", skin_url = "https://example.com/hero.png" }
    end
end

## 4. Обработка Ввода (Клавиатура и Кнопки)
Клиент отправляет сырые нажатия клавиш (input) и команды кнопок (chat).
function on_message(msg)
    if msg.type == "input" and msg.key == "KeyF" then
        return { sender_text = "Вы нажали F!" }
    end
end

## 5. Рендер (2D, Псевдо-3D, 3D)
В конфиге server_config.json задается render_mode:
 @ "topdown" — вид сверху (как в Zelda).
 @ "raycast" — псевдо-3D (как в Doom, Танчики).
 @ "3d" — WebGL (Minecraft).

---

### ⚙️ Как мы будем реализовывать Тики (Следующий шаг)

Когда мы будем готовы писать код, это будет выглядеть так:

**1. В Go (`server/main.go`):**
Мы добавим горутину, которая будет тикать каждые 50мс.
```go
func (s *Server) startGameLoop() {
    ticker := time.NewTicker(50 * time.Millisecond)
    go func() {
        for range ticker.C {
            s.lua.OnTick() // Вызываем Lua
        }
    }()
}

**2. В Lua (plugins/mechanics/ticks.lua):**
Моддеры смогут ловить это событие.
function on_tick()
    -- Логика NPC, физика, проверка победы в стратегии
end