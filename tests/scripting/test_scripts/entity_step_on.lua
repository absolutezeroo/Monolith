voxel.register_block({
    id = "test:pressure_plate",
    on_entity_step_on = function(pos, entity)
        test_stepped_on = true
        test_step_pos_x = pos.x
        test_step_pos_y = pos.y
        test_step_pos_z = pos.z
    end,
})
