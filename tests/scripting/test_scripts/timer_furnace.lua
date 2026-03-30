test_timer_fired = false
test_timer_restart = true
test_timer_elapsed = 0.0

voxel.register_block({
    id = "test:furnace",
    solid = true,
    on_timer = function(pos, elapsed)
        test_timer_fired = true
        test_timer_elapsed = elapsed
        return test_timer_restart -- controlled by test
    end,
})
