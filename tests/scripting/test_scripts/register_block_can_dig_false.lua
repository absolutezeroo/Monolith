-- Test script: registers a block where can_dig returns false (unbreakable via Lua).

voxel.register_block({
    id = "test:unbreakable",
    solid = true,
    has_collision = true,
    light_filter = 15,
    hardness = 1.0,
    texture_indices = {1, 1, 1, 1, 1, 1},
    drop = "",

    can_dig = function(pos, player)
        return false
    end,
})
