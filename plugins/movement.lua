-- Вся логика перемещения и математика
function on_message(msg)
    -- 1. Получаем базовый вектор намерения
    local baseDx, baseDy = 0, 0

    if msg.type == "input" then
        -- Если пришло с клавиатуры
        if msg.key == "KeyW" then baseDy = -1 end
        if msg.key == "KeyS" then baseDy = 1 end
        if msg.key == "KeyA" then baseDx = -1 end
        if msg.key == "KeyD" then baseDx = 1 end
    elseif msg.type == "chat" then
        -- Если нажата кнопка на экране
        if msg.text == "!up" then baseDy = -1 end
        if msg.text == "!down" then baseDy = 1 end
        if msg.text == "!left" then baseDx = -1 end
        if msg.text == "!right" then baseDx = 1 end
    end

    -- Если это не движение - выходим
    if baseDx == 0 and baseDy == 0 then return nil end

    -- 2. МАТЕМАТИКА: Поворот вектора в зависимости от угла камеры
    local angle = tonumber(msg.camera_angle) or 0
    local rad = -angle * math.pi / 180
    
    local finalDx = baseDx * math.cos(rad) - baseDy * math.sin(rad)
    local finalDy = baseDx * math.sin(rad) + baseDy * math.cos(rad)

    -- 3. "Пьяное" движение
    if msg.from == "Пьяный" then
        local drunk_offset = (math.random() - 0.5) * 0.4
        finalDx = finalDx + drunk_offset
        finalDy = finalDy + drunk_offset
    end

    -- 4. Отправляем готовый вектор на сервер
    return { type = "move", dx = finalDx, dy = finalDy }
end