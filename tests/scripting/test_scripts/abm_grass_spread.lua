test_abm_fired = false
test_abm_pos_x = 0
test_abm_pos_y = 0
test_abm_pos_z = 0

voxel.register_block({
    id = "test:dirt",
    solid = true,
})

voxel.register_block({
    id = "test:grass",
    solid = true,
})

voxel.register_abm({
    label = "Grass spread test",
    nodenames = { "test:dirt" },
    neighbors = { "test:grass" },
    interval = 1.0,
    chance = 1, -- always fire in tests
    action = function(pos, node, active_object_count)
        test_abm_fired = true
        test_abm_pos_x = pos.x
        test_abm_pos_y = pos.y
        test_abm_pos_z = pos.z
    end,
})
