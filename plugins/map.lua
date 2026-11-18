-- plugins/map.lua
function generate_map(width, height, seed)
    -- Устанавливаем сид (используем остаток от деления, чтобы не переполнить)
    math.randomseed(seed)
    local offset = seed % 1000 -- Сдвиг для шума
    
    local map = {}
    for y = 1, height do
        map[y] = {}
        for x = 1, width do
            -- Генерация шума с учетом сида
            local n = math.sin((x + offset) * 0.2) + math.cos((y + offset) * 0.25) + (math.random() * 0.2)
            
            -- Биомы (пока в одном скрипте, но логически это отдельные зоны)
            if n > 1.1 then
                map[y][x] = 1 -- Вода
            elseif n < -1.1 then
                map[y][x] = 2 -- Камень
            else
                map[y][x] = 0 -- Трава
            end
        end
    end
    return map
end

-- Эта функция вызывается для каждого игрока при входе.
-- Ты можешь менять карту как угодно (например, для слепых рас).
function get_player_map(username, map)
    -- Пока возвращаем карту без изменений.
    -- Но если имя "Слепой":
    if username == "Слепой" then
        local blind_map = {}
        for y = 1, #map do
            blind_map[y] = {}
            for x = 1, #map[y] do
                -- Всё черное, кроме стен (камень = 2)
                if map[y][x] == 2 then
                    blind_map[y][x] = 2
                else
                    blind_map[y][x] = 0 -- Заменяем всё на пустоту
                end
            end
        end
        return blind_map
    end
    
    return map
end