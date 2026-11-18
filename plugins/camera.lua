function on_message(msg)
    if msg.type ~= "chat" then return nil end
    
    if msg.text == "!cam_left" then
        return { type = "camera_rotate", angle = -45 }
    elseif msg.text == "!cam_right" then
        return { type = "camera_rotate", angle = 45 }
    end
    return nil
end