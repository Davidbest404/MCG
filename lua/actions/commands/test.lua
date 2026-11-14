function on_player_connect(player)
    print("Lua: Player connected! ID = " .. tostring(player:id()))
    
    local hp = api.get_attr(player, "hp")
    print("Lua: Current HP = " .. tostring(hp))
    
    -- Меняем HP через API
    api.set_attr(player, "hp", 42)
    
    local new_hp = api.get_attr(player, "hp")
    print("Lua: New HP = " .. tostring(new_hp))
end