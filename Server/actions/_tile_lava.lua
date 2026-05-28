function tile_lava(player_id, event_type, x, y)
    if event_type == "enter" or event_type == "stay" then
        local hp = get_hp(player_id)
        set_hp(player_id, hp - 5)
        send_to_player(player_id, "You are burned by lava! -5 HP")
    end
end

-- Функция для описания команды (для /help)
function get_description()
    return "Lava damage effect (called automatically on tile enter)"
end