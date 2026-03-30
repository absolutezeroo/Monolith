voxel.register_block({
    id = "test:interactive_block",
    on_rightclick = function(pos, node, clicker, itemstack, pointed_thing)
        test_rightclick_pos_x = pos.x
        test_rightclick_pos_y = pos.y
        test_rightclick_pos_z = pos.z
        return itemstack
    end,
})
