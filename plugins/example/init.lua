-- SPDX-License-Identifier: AGPL-3.0-or-later
-- Пример плагина: отвечает на !hello

function on_message(msg)
    if msg.text and msg.text:sub(1, 6) == "!hello" then
        return {
            type = "chat",
            from = "Lua-плагин",
            text = "Привет, " .. (msg.from or "игрок") .. "! Я живой и могу перезагружаться."
        }
    end
    return nil
end