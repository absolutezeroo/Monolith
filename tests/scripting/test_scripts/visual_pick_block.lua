-- Test: on_pick_block returns custom item ID
voxel.register_block({
    id = "test:double_slab",
    solid = true,
    texture_indices = {0,0,0,0,0,0},

    on_pick_block = function(pos)
        return "test:single_slab"
    end,
})
