function on_message(msg)
    -- 1. Получаем базовый вектор
    local baseDx, baseDy = 0, 0

    if msg.type == "input" then
        -- Дефолтные настройки (можно удалить и оставить пустым, если игроки всё биндят)
        if msg.key == "KeyW" then baseDy = -1 end
        if msg.key == "KeyS" then baseDy = 1 end
        if msg.key == "KeyA" then baseDx = -1 end
        if msg.key == "KeyD" then baseDx = 1 end
    elseif msg.type == "chat" then
        if msg.text == "!up" then baseDy = -1 end
        if msg.text == "!down" then baseDy = 1 end
        if msg.text == "!left" then baseDx = -1 end
        if msg.text == "!right" then baseDx = 1 end
    end

    if baseDx == 0 and baseDy == 0 then return nil end

    -- 2. Математика поворота камеры
    local angle = tonumber(msg.camera_angle) or 0
    local rad = -angle * math.pi / 180
    
    local finalDx = baseDx * math.cos(rad) - baseDy * math.sin(rad)
    local finalDy = baseDx * math.sin(rad) + baseDy * math.cos(rad)

    -- 3. Логика целочисленных координат (ходьба как ладья)
    -- Сервер передает настройку в Config, но мы можем просто спросить через костыль
    -- (В реальности лучше передавать coordinate_type в msg)
    local is_integer = (msg.coordinate_type == "integer")
    
    if is_integer then
        -- Округляем и оставляем только одну ось
        if math.abs(finalDx) > math.abs(finalDy) + 0.1 then
            finalDx = (finalDx > 0) and 1 or -1
            finalDy = 0
        elseif math.abs(finalDy) > math.abs(finalDx) + 0.1 then
            finalDx = 0
            finalDy = (finalDy > 0) and 1 or -1
        else
            -- Если угол ровно 45 градусов, выбираем ось X (или можно запоминать последнюю)
            finalDx = (finalDx > 0) and 1 or -1
            finalDy = 0
        end
    end

    -- 4. Эффект "пьяного" движения
    if msg.from == "Пьяный" and not is_integer then
        local drunk_offset = (math.random() - 0.5) * 0.4
        finalDx = finalDx + drunk_offset
        finalDy = finalDy + drunk_offset
    end

    return { type = "move", dx = finalDx, dy = finalDy }
end