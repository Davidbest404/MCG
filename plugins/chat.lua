function on_message(msg)
    local text = msg.text or ""
    local from = msg.from or "Игрок"

    if text == "!hello" then
        return { sender_text = "Вы поздоровались.", others_text = from .. " со всеми здоровается!" }
    elseif text == "!dance" then
        return { sender_text = "Вы начали танцевать 💃", others_text = from .. " начал танцевать 💃" }
    elseif text == "!help" then
        return { sender_text = "Доступно: !hello, !dance" }
    end

    return { type = "chat", from = from, text = text, broadcast = true }
end