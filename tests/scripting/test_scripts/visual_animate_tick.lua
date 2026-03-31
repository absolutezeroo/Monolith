-- Test: on_animate_tick fires with pos and random function
test_animate_called = false
test_animate_pos_x = 0
test_random_works = false

voxel.register_block({
    id = "test:fire",
    solid = false,
    transparent = true,
    texture_indices = {0,0,0,0,0,0},

    on_animate_tick = function(pos, random)
        test_animate_called = true
        test_animate_pos_x = pos.x
        if type(random) == "function" then
            test_random_works = true
        end
    end,
})
