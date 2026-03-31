-- Test script: cancelable global events
-- Used by TestGlobalEventHooks.cpp

test_interact_called = false
test_interact_action = ""
test_interact_pos_x = 0
test_interact_block_id = ""

voxel.on("player_interact", function(player_id, action, pos, block_id)
    test_interact_called = true
    test_interact_action = action
    test_interact_pos_x = pos.x
    test_interact_block_id = block_id
    -- Cancel "break" actions, allow "place"
    if action == "break" then
        return false
    end
    return true
end)

test_dig_start_called = false
test_dig_start_cancelled = false

voxel.on("block_dig_start", function(player_id, pos, block_id)
    test_dig_start_called = true
    -- Cancel dig if test flag is set
    if test_cancel_dig then
        test_dig_start_cancelled = true
        return false
    end
    return true
end)
