function get_description()
    return "Give an item to a player (admin only)"
end

function give_item(player_id, args)
    if #args < 2 then
        return "Usage: /give_item <target_id> <item_name> [quantity]"
    end
    local target_id = tonumber(args[1])
    local item_name = args[2]
    local quantity = tonumber(args[3]) or 1
    if not target_id or not item_name then
        return "Invalid arguments"
    end

    local current_quantity = get_attr(target_id, "item_" .. item_name .. "_quantity") or 0
    set_attr(target_id, "item_" .. item_name .. "_quantity", current_quantity + quantity)

    -- Если предмет имеет прочность и она не задана – установить по умолчанию
    local durability = get_attr(target_id, "item_" .. item_name .. "_durability")
    if durability == nil then
        if item_name == "wooden_axe" or item_name == "iron_axe" then
            set_attr(target_id, "item_" .. item_name .. "_durability", 100)
        end
    end
    return "Item '" .. item_name .. "' x" .. tostring(quantity) .. " given to player " .. tostring(target_id)
end