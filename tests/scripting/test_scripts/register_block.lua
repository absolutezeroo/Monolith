-- Test script: registers a simple block via Lua API
voxel.register_block({
    id = "test:simple_block",
    solid = true,
    has_collision = true,
    light_filter = 15,
    hardness = 2.0,
    texture_indices = {1, 1, 1, 1, 1, 1},
    drop = "test:simple_block",
    groups = {cracky = 2, stone = 1},
})
