-- Test script: block-related global events
-- Used by TestGlobalEventHooks.cpp

test_block_placed_called = false
test_block_placed_pos_x = 0
test_block_placed_id = ""

voxel.on("block_placed", function(pos, block_id)
    test_block_placed_called = true
    test_block_placed_pos_x = pos.x
    test_block_placed_id = block_id
end)

test_block_broken_called = false
test_block_broken_pos_y = 0
test_block_broken_id = ""

voxel.on("block_broken", function(pos, block_id)
    test_block_broken_called = true
    test_block_broken_pos_y = pos.y
    test_block_broken_id = block_id
end)

test_tick_called = false
test_tick_dt = 0.0

voxel.on("tick", function(dt)
    test_tick_called = true
    test_tick_dt = dt
end)
