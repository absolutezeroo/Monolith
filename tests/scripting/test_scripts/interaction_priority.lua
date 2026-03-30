voxel.register_block({
    id = "test:priority_block",
    on_interact_start = function(pos, player)
        test_which_fired = "interact_start"
        return true
    end,
    on_rightclick = function(pos, node, clicker, itemstack, pointed_thing)
        test_which_fired = "rightclick"
        return itemstack
    end,
})
