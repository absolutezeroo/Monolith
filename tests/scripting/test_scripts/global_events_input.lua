-- Test script: input-related global events
-- Used by TestGlobalEventHooks.cpp

test_key_pressed_called = false
test_key_pressed_name = ""

voxel.on("key_pressed", function(entity, key_name)
    test_key_pressed_called = true
    test_key_pressed_name = key_name
    -- Cancel 'e' key
    if key_name == "e" then
        return false
    end
    return true
end)

test_combo_fired = false
test_combo_name = ""

voxel.register_combo("dash", {"w", "w"}, 0.5, function(player, name)
    test_combo_fired = true
    test_combo_name = name
end)
