voxel.register_block({
    id = "test:punchable_block",
    on_punch = function(pos, node, puncher, pointed_thing)
        test_punch_pos_x = pos.x
        test_punch_pos_y = pos.y
        test_punch_pos_z = pos.z
    end,
})
