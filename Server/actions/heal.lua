function heal(player_id, args)
    local hp = get_hp(player_id)
    local max_hp = get_max_hp(player_id)
    local new_hp = math.min(max_hp, hp + 30)
    set_hp(player_id, new_hp)
    return "[cE]You[/cE] healed 30 HP! Now " .. new_hp .. "/" .. max_hp
end