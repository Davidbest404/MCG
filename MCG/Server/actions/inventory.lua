function get_description()
    return "Show your inventory"
end

function inventory(player_id, args)
    local attrs = get_all_attrs(player_id)
    if attrs == nil then
        return "Player not found!"
    end

    local result = "[cA]=== Your Inventory ===[/cA]\n"
    local has_items = false

    for key, value in pairs(attrs) do
        if type(key) == "string" and string.sub(key, 1, 5) == "item_" then
            local parts = {}
            for part in string.gmatch(key, "[^_]+") do
                table.insert(parts, part)
            end
            if #parts >= 4 and parts[#parts] == "quantity" then
                local item_name_parts = {}
                for i = 2, #parts - 1 do
                    table.insert(item_name_parts, parts[i])
                end
                local item_name = table.concat(item_name_parts, "_")
                local quantity = value
                local durability_key = "item_" .. item_name .. "_durability"
                local durability = attrs[durability_key] or -1

                local line = "- " .. item_name
                if quantity > 1 then
                    line = line .. " x" .. tostring(quantity)
                end
                if durability >= 0 then
                    line = line .. " (durability: " .. tostring(durability) .. ")"
                end
                result = result .. line .. "\n"
                has_items = true
            end
        end
    end

    if not has_items then
        result = result .. "You have no items.\n"
    end

    result = result .. "=========================\n"
    return result
end