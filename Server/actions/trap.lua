-- функция, вызываемая при входе на тайл с id=3 (ловушка)
function trap_on_enter(player_id, x, y, tile_id)
    local hp = get_hp(player_id)
    set_hp(player_id, hp - 10)
    send_to_player(player_id, "You stepped on a trap! -10 HP")
    if get_hp(player_id) <= 0 then
        send_to_player(player_id, "You died!")
        -- можно телепортировать на старт
        set_x(player_id, 0)
        set_y(player_id, 0)
        set_hp(player_id, get_max_hp(player_id))
    end
end

function get_description()
    return "Trap tile: damages player on entry"
end