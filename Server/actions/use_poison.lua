-- Использование яда: отнимает половину текущего HP у цели (по умолчанию у себя)
function use_poison(player_id, args)
    -- Определяем цель: если передан ID, то его, иначе атакующего
    local target_id = tonumber(args[1]) or player_id
    
    -- Получаем HP цели
    local target_hp = get_hp(target_id)
    if target_hp == nil or target_hp <= 0 then
        return "Target not found or already dead"
    end
    
    -- Рассчитываем половину (целочисленное деление вниз)
    local damage = math.floor(target_hp / 2)
    if damage == 0 then
        return "Target has no HP to lose"
    end
    
    -- Применяем урон
    local new_hp = target_hp - damage
    set_hp(target_id, new_hp)
    
    -- Получаем имя цели для сообщений
    local target_name = get_player_name(target_id)
    
    -- Широковещательное сообщение (для всех игроков)
    broadcast("☠️ " .. target_name .. " loses " .. damage .. " HP from poison! Now HP: " .. new_hp)
    
    -- Ответ игроку, который использовал предмет
    return "You poison " .. target_name .. " (half HP removed). Remaining HP: " .. new_hp
end