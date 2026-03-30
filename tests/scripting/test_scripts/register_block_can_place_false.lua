-- Test script: registers a block where can_place returns false.

voxel.register_block({
    id = "test:no_place",
    solid = true,
    has_collision = true,
    light_filter = 15,
    hardness = 1.0,
    texture_indices = {1, 1, 1, 1, 1, 1},
    drop = "",

    can_place = function(pos, player)
        return false
    end,
})
