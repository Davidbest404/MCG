-- Отвечает только за танец
function on_message(msg)
    if msg.type ~= "chat" then return nil end
    
    if msg.text == "!dance" then
        return { sender_text = "Вы начали танцевать 💃", others_text = msg.from .. " начал танцевать 💃" }
    end
    return nil
end