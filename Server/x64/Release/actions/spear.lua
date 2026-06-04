function spear(player_id, args)
    local target_id = tonumber(args[1])
    if not target_id then
        send_to_player(player_id, "Usage: /spear <player_id>")
        return
    end
    if get_hp(player_id) <= 0 then
        send_to_player(player_id, "You are dead and cannot attack.")
        return
    end
    if get_hp(target_id) <= 0 then
        send_to_player(player_id, "Target is already dead.")
        return
    end
    local my_x = get_x(player_id)
    local my_y = get_y(player_id)
    local tx = get_x(target_id)
    local ty = get_y(target_id)
    local dist = math.abs(my_x - tx) + math.abs(my_y - ty)
    if dist == 2 then
        local damage = 15
        local new_hp = math.max(0, get_hp(target_id) - damage)
        set_hp(target_id, new_hp)
        broadcast(player_id .. " stabs with a spear at " .. target_id .. " for " .. damage .. " damage!")
        if new_hp <= 0 then
            broadcast(target_id .. " has been killed by " .. player_id .. "!")
        end
    else
        send_to_player(player_id, "Target too far (distance " .. dist .. " > 2).")
    end
end

function get_description()
    return "Stab with spear, range up to 2 tiles (damage 15)."
end