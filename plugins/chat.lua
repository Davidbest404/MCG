-- Отвечает только за чат и базовые команды
function on_message(msg)
    -- Если это не чат - игнорируем
    if msg.type ~= "chat" then return nil end

    local text = msg.text or ""
    local from = msg.from or "Игрок"

    if text == "!hello" then
        return { sender_text = "Вы поздоровались.", others_text = from .. " со всеми здоровается!" }
    elseif text == "!help" then
        return { sender_text = "Доступно: !hello, !dance, !up, !down, !left, !right" }
    elseif text:sub(1, 1) == "!" then
        -- Если команда начинается с !, но мы её не знаем - отдаем эстафету другим плагинам
        return nil 
    end

    -- Обычный чат
    return { type = "chat", from = from, text = text, broadcast = true }
end