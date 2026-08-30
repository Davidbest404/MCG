function get_description()
    return "Use your axe to chop wood"
end

function axe(player_id, args)
    local has_axe = get_attr(player_id, "item_wooden_axe_quantity")
    if not has_axe or has_axe <= 0 then
        return "You don't have an axe!"
    end

    local durability = get_attr(player_id, "item_wooden_axe_durability")
    if durability == nil then
        durability = -1
    end

    if durability == 0 then
        return "Your axe is broken!"
    end

    if durability > 0 then
        local new_durability = durability - 1
        set_attr(player_id, "item_wooden_axe_durability", new_durability)
        if new_durability == 0 then
            set_attr(player_id, "item_wooden_axe_quantity", 0)
            return "Your axe broke!"
        end
    end

    local wood = get_attr(player_id, "wood") or 0
    set_attr(player_id, "wood", wood + 1)

    local result = "You chopped some wood! (wood: " .. tostring(wood + 1) .. ")"
    if durability > 0 then
        result = result .. ", axe durability left: " .. tostring(durability - 1)
    end
    return result
end